/*
 * Ice Delay (DELAY): the original's Ice Delay (QME-200 V1.2.2, library entry 9.16, id 0x0B00002C) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). A modulated stereo delay in the style of a rack unit: per
 * channel the input (x i, i the input level) plus Feedback x the channel's last output x w into a 4 s line read
 * Time + 100 Depth (0.5 + 0.5 sin(2 pi Rate n / 44100)) samples back (between two samples, at most 4 s), through a
 * 1st-order low pass (High Cut: 20k, 8k, 4k, 2k Hz) and a 1st-order high pass (Low Cut: 20, 100, 200, 400 Hz), both
 * bilinear from tan: the channel's output. n counts samples up to 44100 / Rate. Phase Reverse 1 flips the left output
 * once per channel run (so twice per sample), 2 the right one, 3 each channel's own. Single precision.
 *
 * Out per channel: below Mix 0.5, (x i + 2 Mix w o) 2 Level; from 0.5, (w o + 2 (1 - Mix) x i) 2 Level; plus
 * (1 - i) x. Feedback, Mod Rate, Mod Depth, Mix and Level glide 0.0001 per sample, the time 0.0002267 of the way
 * (with Sync on it jumps); i glides 0.0001 per sample to 1 while enabled and to 0 bypassed, w likewise to 1, or to 0
 * bypassed with Trail at 0 (where Mix and Level glide to 0 and 0.5). Bypassed with Trail on the lines run on (the
 * input fading out); with Trail at 0 everything is cleared once (the time too: it glides up again from 0) and the
 * signal passes. The Init sets the filters to 20 Hz and 20 kHz whatever the cuts (kept) and reads the tempo: here the
 * first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x19, 0, 0, 0 },
    { "Feedback", -100.0f, 100.0f, 1.0f, 20.0f, 2, 0x1d, 0x00, 0, 0, 0 },
    { "Mod Rate", 0.10000000149011612f, 10.0f, 0.009999999776482582f, 1.0f, 3, 0x22, 0x00, 0, 0, 0 },
    { "Mod Depth", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Phase Reverse", 0.0f, 3.0f, 1.0f, 2.0f, 5, 0x24, 0x00, 0, 0, 0 },
    { "Low Cut", 0.0f, 3.0f, 1.0f, 0.0f, 6, 0x25, 0x00, 0, 0, 0 },
    { "High Cut", 0.0f, 3.0f, 1.0f, 0.0f, 7, 0x26, 0x00, 0, 0, 0 },
    { "Level", 0.0f, 100.0f, 1.0f, 50.0f, 8, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 9, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 10, 0x00, 0x00, 0, 0, 0 },
};

static const float LOW_CUT[4] = { 20.0f, 100.0f, 200.0f, 400.0f };
static const float HIGH_CUT[4] = { 20000.0f, 8000.0f, 4000.0f, 2000.0f };

struct state {
    float mix, feedback, rate, depth, level, in_level, wet, len, target, n;
    float low_cut, high_cut;            /* the knobs the filters were last set from */
    float hp_b0, hp_b1, hp_a1, lp_b0, lp_b1, lp_a1;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct dly_line line[2];
    float lp_x[2], lp_y[2], hp_x[2], hp_y[2];
    float out[2];
};

static int cut_index(float knob)
{
    int i = (int)knob;
    return i > 2 ? 3 : i < 0 ? 0 : i;
}

/* one channel: its line, filters and mix (x the input, len the modulated time, mode Phase Reverse) */
static float channel_run(struct state *s, int ch, float x, float len, int mode)
{
    float xi = s->in_level * x;
    dly_write(&s->line[ch], st_fma(s->out[ch] * s->feedback, s->wet, xi));
    float a = dly_read(&s->line[ch], (int)len, len - __builtin_truncf(len));          /* VRINTZ */
    float lp = st_fma(-s->lp_y[ch], s->lp_a1, st_fma(s->lp_x[ch], s->lp_b1, a * s->lp_b0));    /* VFMS */
    s->lp_x[ch] = a;
    s->lp_y[ch] = lp;
    float hp = st_fma(-s->hp_y[ch], s->hp_a1, st_fma(s->hp_x[ch], s->hp_b1, s->hp_b0 * lp));
    s->hp_x[ch] = lp;
    s->hp_y[ch] = hp;
    s->out[ch] = hp;
    if (mode == 3)
        s->out[ch] = -s->out[ch];
    else if (mode == 2)
        s->out[1] = -s->out[1];
    else if (mode == 1)
        s->out[0] = -s->out[0];
    float wet = s->out[ch] * s->wet, y;
    if (s->mix < 0.5f)
        y = st_fma(wet + wet, s->mix, xi);
    else
        y = st_fma(xi + xi, 1.0f - s->mix, wet);
    return s->level * (y + y);
}

/* channel_run after its read and filters (hp: the filtered read): the write before them, the outputs' sign flips
   (Phase Reverse), the mix */
static float channel_post(struct state *s, int ch, float x, float hp, int mode)
{
    float xi = s->in_level * x;
    dly_write(&s->line[ch], st_fma(s->out[ch] * s->feedback, s->wet, xi));
    s->out[ch] = hp;
    if (mode == 3)
        s->out[ch] = -s->out[ch];
    else if (mode == 2)
        s->out[1] = -s->out[1];
    else if (mode == 1)
        s->out[0] = -s->out[0];
    float wet = s->out[ch] * s->wet, y;
    if (s->mix < 0.5f)
        y = st_fma(wet + wet, s->mix, xi);
    else
        y = st_fma(xi + xi, 1.0f - s->mix, wet);
    return s->level * (y + y);
}

static void clear(struct state *s)
{
    s->line[0].w = s->line[1].w = 0;
    s->out[0] = s->out[1] = 0.0f;
    s->wet = 0.0f;
    s->target = s->len = 0.0f;
    for (int c = 0; c < 2; c++)
        s->lp_x[c] = s->lp_y[c] = s->hp_x[c] = s->hp_y[c] = 0.0f;
    __builtin_memset(s->line[0].buf, 0, DLY_BUFFER);
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->rate = k[3];
    s->depth = k[4] * 0.01f;
    s->low_cut = k[6];
    s->high_cut = k[7];
    s->level = k[8] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[9];
    s->line[0].buf = buffer;
    s->line[1].buf = (float *)buffer + DLY_LINE;
    __builtin_memset(buffer, 0, DLY_BUFFER);
    s->cleared = 1;
    s->hp_b0 = 0.9985772371292114f;                                                  /* 20 Hz */
    s->hp_b1 = -0.9985772371292114f;
    s->hp_a1 = -0.9971545338630676f;
    s->lp_b0 = s->lp_b1 = 0.8717750310897827f;                                       /* 20 kHz */
    s->lp_a1 = 0.7435501217842102f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time_free(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float mix = k[0] * 0.01f, fb = k[2] * 0.01f, rate = k[3], depth = k[4] * 0.01f, level = k[8] * 0.01f;
    int trail = (int)k[10], sync = (uint8_t)(int)k[9] == 1, mode = (int)k[5];
    float wet_to = 1.0f;
    if ((uint8_t)(trail | io->enabled) == 0) {
        wet_to = 0.0f;
        level = 0.5f;
        mix = 0.0f;
    }
    s->target = dly_time_free(k[1], k[9], io->beat_ms);
    if (s->low_cut != k[6]) {
        s->low_cut = k[6];
        float t = st_tanf(LOW_CUT[cut_index(k[6])] * 7.123792602214962e-05f), d = t + 1.0f;
        s->hp_b0 = 1.0f / d;
        s->hp_b1 = -s->hp_b0;
        s->hp_a1 = (t + -1.0f) / d;
    }
    if (s->high_cut != k[7]) {
        s->high_cut = k[7];
        float t = st_tanf(HIGH_CUT[cut_index(k[7])] * 7.123792602214962e-05f), d = t + 1.0f;
        s->lp_b0 = s->lp_b1 = t / d;
        s->lp_a1 = (t + -1.0f) / d;
    }
    if (io->frames == 0)
        return;
    float dl = (s->target - s->len) * 0.00022669999452773482f;
    float in_to = (float)(unsigned)io->enabled;
    float *p = io->io;
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if ((io->enabled || (uint8_t)trail == 1) && (int)s->len > n + 1 && (int)s->target > n + 1) {
        /* stage by stage over the block (the same operations on the same values as channel_run sample by
           sample): the delay (882+ samples, the modulation only adds) is longer than the block, so the reads and
           both filters run over it first, the past in registers; then per sample the writes (the feedback of the
           output before), the sign flips and the mix */
        float L[WVFX_MAX_FRAMES], G[2][WVFX_MAX_FRAMES];
        float fbv[WVFX_MAX_FRAMES], mxv[WVFX_MAX_FRAMES], lvv[WVFX_MAX_FRAMES], inv[WVFX_MAX_FRAMES];
        float wtv[WVFX_MAX_FRAMES];
        for (int i = 0; i < n; i++) {
            s->feedback = st_glide(s->feedback, fb);
            s->rate = st_glide(s->rate, rate);
            s->depth = st_glide(s->depth, depth);
            s->mix = st_glide(s->mix, mix);
            s->level = st_glide(s->level, level);
            s->in_level = st_glide(s->in_level, in_to);
            s->wet = st_glide(s->wet, wet_to);
            float glided = s->len + dl;
            s->len = sync ? s->target : glided;
            float phase = (s->rate * 0.00014247585204429924f) * s->n;
            float next = s->n + 1.0f;
            s->n = 44100.0f / s->rate > next ? next : 0.0f;
            float m = st_fma(st_sinf(phase), 0.5f, 0.5f);
            L[i] = __builtin_fminf(st_fma(s->depth * 100.0f, m, s->len), 176400.0f);    /* VMINNM */
            fbv[i] = s->feedback;
            mxv[i] = s->mix;
            lvv[i] = s->level;
            inv[i] = s->in_level;
            wtv[i] = s->wet;
        }
        if (io->enabled)
            s->cleared = 0;
        for (int c = 0; c < 2; c++) {
            const struct dly_line *ln = &s->line[c];
            float lx = s->lp_x[c], ly = s->lp_y[c], hx = s->hp_x[c], hy = s->hp_y[c];
            const float lb0 = s->lp_b0, lb1 = s->lp_b1, la1 = s->lp_a1, hb0 = s->hp_b0, hb1 = s->hp_b1, ha1 = s->hp_a1;
            for (int i = 0; i < n; i++) {
                int wi = ln->w + 1 + i;
                while (wi > DLY_LINE - 1)
                    wi -= DLY_LINE;
                struct dly_line at = { ln->buf, wi };
                float a = dly_read(&at, (int)L[i], L[i] - __builtin_truncf(L[i]));          /* VRINTZ */
                float lp = st_fma(-ly, la1, st_fma(lx, lb1, a * lb0));                        /* VFMS */
                lx = a;
                ly = lp;
                float hp = st_fma(-hy, ha1, st_fma(hx, hb1, hb0 * lp));
                hx = lp;
                hy = hp;
                G[c][i] = hp;
            }
            s->lp_x[c] = lx;
            s->lp_y[c] = ly;
            s->hp_x[c] = hx;
            s->hp_y[c] = hy;
        }
        for (int i = 0; i < n; i++) {
            /* the glided values of sample i, as channel_run reads them from the state */
            s->feedback = fbv[i];
            s->mix = mxv[i];
            s->level = lvv[i];
            s->in_level = inv[i];
            s->wet = wtv[i];
            float xl = p[2 * i], xr = p[2 * i + 1];
            float l = channel_post(s, 0, xl, G[0][i], mode), r = channel_post(s, 1, xr, G[1][i], mode);
            float dry = 1.0f - s->in_level;
            p[2 * i] = st_fma(dry, xl, l);
            p[2 * i + 1] = st_fma(dry, xr, r);
        }
        return;
    }
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->feedback = st_glide(s->feedback, fb);
        s->rate = st_glide(s->rate, rate);
        s->depth = st_glide(s->depth, depth);
        s->mix = st_glide(s->mix, mix);
        s->level = st_glide(s->level, level);
        s->in_level = st_glide(s->in_level, in_to);
        s->wet = st_glide(s->wet, wet_to);
        float glided = s->len + dl;
        s->len = sync ? s->target : glided;
        if (io->enabled) {
            s->cleared = 0;
        } else if (trail == 0) {
            if (!s->cleared)
                clear(s);
            continue;
        } else if ((uint8_t)trail != 1) {
            continue;
        }
        float phase = (s->rate * 0.00014247585204429924f) * s->n;
        float next = s->n + 1.0f;
        s->n = 44100.0f / s->rate > next ? next : 0.0f;
        float m = st_fma(st_sinf(phase), 0.5f, 0.5f);
        float len = __builtin_fminf(st_fma(s->depth * 100.0f, m, s->len), 176400.0f);  /* VMINNM */
        float xl = p[0], xr = p[1];
        float l = channel_run(s, 0, xl, len, mode), r = channel_run(s, 1, xr, len, mode);
        float dry = 1.0f - s->in_level;
        p[0] = st_fma(dry, xl, l);
        p[1] = st_fma(dry, xr, r);
    }
}

const struct wvfx_desc dly_ice_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x110),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Ice Delay",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
