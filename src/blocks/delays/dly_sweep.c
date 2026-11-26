/*
 * Sweep (DELAY): the original's Sweep delay (QME-200 V1.2.2, library entry 9.7, id 0x0B000006) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A stereo delay whose repeats go through a swept resonant band
 * pass: each input plus Feedback x its line's last read (clipped to +-1.7) into a 4 s line read Time back, the read
 * through a bilinear band pass (g = tan(pi fc / 44100), Q 5) x 4. fc = 100 + 2900 m Depth, m a triangle LFO ramping
 * by the sweep rate per sample (the two channels in opposite phase). Single precision.
 *
 * Out: below Mix 0.5, x + Mix wet; from 0.5, wet / 2 + 2 (1 - Mix) x (bypassed with Trail on: wet / 2 + x). The
 * sweep rate is 2.2676e-06 .. 0.00022676 by pow3(Sweep Rate), or with Sweep Sync on one LFO step per tempo division
 * (Sweep Rate's whole value). Mix, Feedback, Depth, the time and the sweep rate glide 0.00022676 of the way to the
 * knobs each sample; with a Sync on its value jumps. The Init reads the tempo: here the first process call does.
 * (The Init computes the rate with its constants folded: kept.)
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 2, 0x19, 0x16, 0, 0, 0 },
    { "Sweep Depth", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Sweep Rate", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x15, 0, 0, 0 },
    { "Sweep Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x24, 0, 0, 0 },
    { "Time Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 7, 0x00, 0x00, 0, 0, 0 },
};

#define RATE_MIN 2.2675999389321078e-06f
#define RATE_MAX 0.0002267600066261366f

struct channel {
    struct dly_line line;
    float repeat;                       /* the line's last read */
    float m, sign;                      /* the LFO */
    float x1, x2, y1, y2;               /* the band pass */
};

struct state {
    float mix, feedback, depth, len, rate;
    float k0[8];                        /* the Init's knobs, for the first process call */
    int started, cleared;
    struct channel l, r;
};

static float sweep_rate(float rate, float sync, uint32_t beat_ms)
{
    if ((int)sync == 1) {
        unsigned i = (unsigned)(int)rate;
        return 1.0f / (((float)(beat_ms & 0xffffu) * 44.099998474121094f) * (i <= 10 ? DLY_SYNC_BEATS[i] : 1.0f));
    }
    return st_fma(RATE_MAX - RATE_MIN, st_taper(st_pow3, rate), RATE_MIN);
}

/* the line's input held at +-1.7 (VMAXNM, VMINNM: no flags; the same as the compares for every x, NaN included) */
static inline float clip17(float in)
{
    return __builtin_fminf(__builtin_fmaxf(in, -1.7000000476837158f), 1.7000000476837158f);
}

/* channel_run's sweeping filter after the read x (the triangle, the tan, the section) */
static inline float sweep_filter(struct channel *c, float x, float rate, float depth)
{
    c->m = st_fma(c->sign, rate, c->m);
    if (c->m >= 1.0f) {
        c->sign = -1.0f;
        c->m = 1.0f;
    } else if (c->m < 0.0f) {
        c->sign = 1.0f;
        c->m = 0.0f;
    }
    float g = st_tanf(st_fma((3000.0f - 100.0f) * c->m, depth, 100.0f) * 7.123792602214962e-05f);
    float g2 = g * g, g5 = g2 * 5.0f;
    float y = c->y1 * st_fma(g2, -10.0f, 10.0f);
    y = st_fma(c->y2, (g - 5.0f) - g5, y);
    y = st_fma(x - c->x2, g, y) / ((g + 5.0f) + g5);
    c->x2 = c->x1;
    c->x1 = x;
    c->y2 = c->y1;
    c->y1 = y;
    return y * 4.0f;
}

/* one channel's sample: its line (the input held at +-1.7), the read is the repeat, then the sweeping filter */
static float channel_run(struct channel *c, float in, float len, float rate, float depth)
{
    dly_write(&c->line, clip17(in));
    float x = dly_read(&c->line, (int)len, len - (float)(int)len);
    c->repeat = x;
    return sweep_filter(c, x, rate, depth);
}

/*
 * The loop of process() over a block, stage by stage (the same operations on the same values as channel_run
 * sample by sample), while the delay (882+ samples) is longer than the block: the glides; both lines' reads (the
 * repeats: each sample's after its write); per sample the writes (the input plus the feedback of the repeat
 * before); the two sweeping filters side by side; the mix.
 */
static void block(struct state *s, const struct wvfx_io *io, const float *k, float target, float rate, float df,
                  float dm, float dd, float dl, float dr)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float *p = io->io, L[WVFX_MAX_FRAMES], R[WVFX_MAX_FRAMES], D[WVFX_MAX_FRAMES], F[WVFX_MAX_FRAMES];
    float M[WVFX_MAX_FRAMES], rep[2][WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++) {
        s->mix += dm;
        float glided = s->len + dl;
        s->len = k[6] == 1.0f ? target : glided;
        s->depth += dd;
        s->feedback += df;
        float rg = s->rate + dr;
        s->rate = k[5] == 1.0f ? rate : rg;
        M[i] = s->mix;
        L[i] = s->len;
        D[i] = s->depth;
        F[i] = s->feedback;
        R[i] = s->rate;
    }
    struct channel *cs[2] = { &s->l, &s->r };
    for (int c = 0; c < 2; c++) {
        struct dly_line *ln = &cs[c]->line;
        for (int i = 0; i < n; i++) {
            int wi = ln->w + 1 + i;
            while (wi > DLY_LINE - 1)
                wi -= DLY_LINE;
            struct dly_line at = { ln->buf, wi };
            rep[c][i] = dly_read(&at, (int)L[i], L[i] - (float)(int)L[i]);
        }
        float last = cs[c]->repeat;
        for (int i = 0; i < n; i++) {
            float x = p[2 * i + c];
            dly_write(ln, clip17(io->enabled ? st_fma(last, F[i], x) : last * F[i]));
            last = rep[c][i];
        }
        cs[c]->repeat = last;
    }
    for (int i = 0; i < n; i++) {
        rep[0][i] = sweep_filter(&s->l, rep[0][i], R[i], D[i]);
        rep[1][i] = sweep_filter(&s->r, rep[1][i], R[i], D[i]);
    }
    for (int i = 0; i < n; i++) {
        float xl = p[2 * i], xr = p[2 * i + 1], l = rep[0][i], r = rep[1][i];
        if (M[i] < 0.5f) {
            p[2 * i] = st_fma(M[i], l, xl);
            p[2 * i + 1] = st_fma(M[i], r, xr);
        } else if (!io->enabled && (int)k[7] == 1) {
            p[2 * i] = l * 0.5f + xl;
            p[2 * i + 1] = r * 0.5f + xr;
        } else {
            float m = 1.0f - M[i];
            p[2 * i] = st_fma(xl + xl, m, l * 0.5f);
            p[2 * i + 1] = st_fma(xr + xr, m, r * 0.5f);
        }
    }
    if (io->enabled)
        s->cleared = 0;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    for (int i = 0; i < 8; i++)
        s->k0[i] = k[i];
    s->mix = k[0] * 0.01f;
    s->feedback = k[1] * 0.01f;
    s->depth = k[3] * 0.01f;
    s->l = (struct channel){ { buffer, 0 }, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    s->r = (struct channel){ { (float *)buffer + DLY_LINE, 0 }, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    __builtin_memset(buffer, 0, DLY_BUFFER);
    s->cleared = 1;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time(s->k0[2], s->k0[6], io->beat_ms);
        s->rate = (int)s->k0[5] == 1 ? sweep_rate(s->k0[4], s->k0[5], io->beat_ms)
                                     : st_fma(st_taper(st_pow3, s->k0[4]), 0.00022449241077993065f, RATE_MIN);
        s->started = 1;
    }
    float target = dly_time(k[2], k[6], io->beat_ms), rate = sweep_rate(k[4], k[5], io->beat_ms);
    if (io->frames == 0)
        return;
    float df = (k[1] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dd = (k[3] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    if (io->enabled || k[7] == 1.0f) {
        const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
        if ((int)s->len > n + 1 && (int)target > n + 1) {                           /* always: 882+ samples */
            block(s, io, k, target, rate, df, dm, dd, dl, dr);
            return;
        }
    }
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float xl = p[0], xr = p[1];
        s->mix += dm;
        float glided = s->len + dl;
        s->len = k[6] == 1.0f ? target : glided;
        s->depth += dd;
        s->feedback += df;
        float rg = s->rate + dr;
        s->rate = k[5] == 1.0f ? rate : rg;
        float l, r;
        if (io->enabled) {
            l = channel_run(&s->l, st_fma(s->l.repeat, s->feedback, xl), s->len, s->rate, s->depth);
            r = channel_run(&s->r, st_fma(s->r.repeat, s->feedback, xr), s->len, s->rate, s->depth);
            s->cleared = 0;
        } else if (k[7] == 1.0f) {
            l = channel_run(&s->l, s->l.repeat * s->feedback, s->len, s->rate, s->depth);
            r = channel_run(&s->r, s->r.repeat * s->feedback, s->len, s->rate, s->depth);
        } else {
            if (k[7] == 0.0f && !s->cleared) {
                s->l.repeat = s->r.repeat = 0.0f;
                __builtin_memset(s->l.line.buf, 0, DLY_BUFFER);
                s->cleared = 1;
            }
            continue;
        }
        if (s->mix < 0.5f) {
            p[0] = st_fma(s->mix, l, xl);
            p[1] = st_fma(s->mix, r, xr);
        } else if (!io->enabled && (int)k[7] == 1) {
            p[0] = l * 0.5f + xl;
            p[1] = r * 0.5f + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l * 0.5f);
            p[1] = st_fma(xr + xr, m, r * 0.5f);
        }
    }
}

const struct wvfx_desc dly_sweep_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x107),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sweep",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
