/*
 * Shimmer (REVERB): the original's Shimmer reverb (QME-200 V1.2.2, library entry 10.10, id 0x0C000009) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine's lines (rvb_fdn.h: 2048, 2187, 2401,
 * 3125 samples, Decay = 0.5 + 0.45 pow0_5(Decay), the reads fed back as they are), with an octave-up shifter in its
 * input: each channel's input plus its last output goes through a two-tap pitch shifter (a 4096-sample line read by
 * two 20-bit fixed-point phases stepping 256 per sample, half a period apart, each tap windowed by the sine curve
 * table, then a 1st-order low pass); the pre-delay line takes ((L + R) 0.6 + (shifted L + R) 0.4) / 2, read
 * 2205 + 4410 Pre Delay - 110.25 lfo back (the lfo's phase wrapping by 2047.95). The outputs: the damping (low and
 * high shelves by Lo End and Hi End) on lines 0 + 2 (left) and 1 + 3 (right), with no low pass; they are also the
 * shifter's feedback.
 *
 * Out: below Mix 0.5, left x + 1.6 Mix wet, right x + 2 Mix wet; from 0.5, 0.8 wet + 2 (1 - Mix) x on the left,
 * wet + 2 (1 - Mix) x on the right (bypassed with Trail on: 0.8 wet + x, wet + x). Mix and Pre Delay glide 0.00022676
 * of the way to the knobs each sample. Bypassed with Trail on the lines run on without input (the shifter stands
 * still); with Trail at 0 everything is cleared once and the signal passes.
 */
#include "../fx/shift.h"
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x73, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Lo End", -50.0f, 50.0f, 1.0f, 0.0f, 3, 0x5d, 0x00, 0, 0, 0 },
    { "Hi End", -50.0f, 50.0f, 1.0f, 0.0f, 4, 0x5d, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model SHIMMER = {
    0.44999998807907104f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 2, 1, 0,
    { 2048.0f, 2187.0f, 2401.0f, 3125.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
};

#define PS_LINE 4096
#define BUFFER (RVB_FDN_BUFFER + 2 * PS_LINE * (int)sizeof(float))

struct state {
    struct rvb_fdn f;
    float *ps;
    int wp[2], ph[2][2];                /* the shifter's write positions and phases */
    float x1[2], y1[2];                 /* its low pass */
    float fb[2];                        /* the outputs, fed back into the shifter */
};

static void damp_set(struct state *s, float dl, float dh)
{
    rvb_fdn_lohi_damp(&s->f, dl, dh, 0.49828922748565674f, 0.07126807421445847f, 0.455511212348938f);
}

static int phase_step(int ph)
{
    int p = (int)((float)ph + -256.0f);
    return p > 0x100000 ? p : p < 0 ? p + 0x100000 : p;  /* the original adds 0 above 2^20 (never reached) */
}

static float shift(struct state *s, int c, float x)
{
    float *b = s->ps + c * PS_LINE;
    int w = s->wp[c], nw = w + 1 >= PS_LINE ? 0 : w + 1;
    b[w] = x;
    s->wp[c] = nw;
    int p1 = s->ph[c][0] = phase_step(s->ph[c][0]);
    int p2 = s->ph[c][1] = phase_step(s->ph[c][1]);
    float t1 = shift_read(b, nw, p1) * 0.00390625f, t2 = shift_read(b, nw, p2) * 0.00390625f;
    float y = st_fma(t2, (float)shift_window(p2), t1 * (float)shift_window(p1)) * 4.57763671875e-05f;
    float o = st_fma(-0.06956888735294342f, s->y1[c], st_fma(0.5347844362258911f, s->x1[c], 0.5347844362258911f * y));
    s->x1[c] = y;
    s->y1[c] = o;
    return o;
}

static void clear(struct state *s)
{
    rvb_fdn_clear(&s->f);
    __builtin_memset(s->ps, 0, 2 * PS_LINE * sizeof(float));
    s->wp[0] = s->wp[1] = 0;
    s->ph[0][0] = s->ph[1][0] = 0;
    s->ph[0][1] = s->ph[1][1] = 0x80000;
    s->x1[0] = s->x1[1] = s->y1[0] = s->y1[1] = 0.0f;
    s->fb[0] = s->fb[1] = 0.0f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->ps = (float *)buffer + RVB_FDN_BUFFER / (int)sizeof(float);
    rvb_fdn_init(&s->f, &SHIMMER, buffer, k[0], k[1], k[2]);
    clear(s);
    s->f.ld = st_fma(-k[3], 0.01f, 0.5f);
    s->f.hd = st_fma(-k[4], 0.01f, 0.5f);
    damp_set(s, s->f.ld, s->f.hd);
    return 0;
}

/*
 * The block, stage by stage (the same operations on the same values as the loop in process()): the lines (2048+
 * samples) read samples written before the block and feed back as they are, so the outputs (their sums through the
 * damping) are known for the whole block first; then per sample the shifter (fed the output of the sample before),
 * the pre-delay, the matrix from the reads of the sample before and the writes; then the mix.
 */
static void block(struct state *s, const struct wvfx_io *io, float trail, float dp, float dm)
{
    struct rvb_fdn *f = &s->f;
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES, have_x = io->enabled != 0;
    float *p = io->io, o[4][WVFX_MAX_FRAMES], yl[WVFX_MAX_FRAMES], yr[WVFX_MAX_FRAMES], mv[WVFX_MAX_FRAMES];
    /* the reads (rvb_fdn_line: each line a ring of its whole length) */
    for (int k = 0; k < 4; k++) {
        float len = SHIMMER.len[k], frac = len - (float)(int)len, prev = f->prev[k];
        const float *b = f->line + k * RVB_FDN_LINE;
        const int whole = (int)len;
        int r = f->w[k] >= whole - 1 ? 0 : f->w[k] + 1;
        for (int i = 0; i < n; i++) {
            float a = b[r];
            o[k][i] = st_fma(prev - a, frac, a);
            prev = a;
            r = r >= whole - 1 ? 0 : r + 1;
        }
        f->prev[k] = prev;
    }
    for (int i = 0; i < n; i++) {
        yl[i] = o[2][i] + o[0][i];
        yr[i] = o[3][i] + o[1][i];
    }
    sos_block(&f->damp_c, &f->damp[0], yl, n);
    sos_block(&f->damp_c, &f->damp[1], yr, n);
    /* per sample */
    float g = f->g, ng = -g, q0 = f->out[0], q1 = f->out[1], q2 = f->out[2], q3 = f->out[3];
    float fl = s->fb[0], fr = s->fb[1];
    for (int i = 0; i < n; i++) {
        float o4 = f->osc + 0.04643990844488144f, o0 = f->osc + -2047.95361328125f;
        f->osc = 2048.0f > o4 ? o4 : o0;
        f->predelay += dp;
        f->mix += dm;
        mv[i] = f->mix;
        float in[4];
        if (have_x) {
            float xl = p[2 * i], xr = p[2 * i + 1];
            unsigned u = (unsigned)f->osc;
            float t0 = RVB_SINE[u], lfo = st_fma(RVB_SINE[(u + 1) & 0x7ff] - t0, f->osc - __builtin_truncf(f->osc), t0);
            float sl = shift(s, 0, fl + xl), sr = shift(s, 1, fr + xr);
            float xm = st_fma(sr + sl, 0.4000000059604645f, (xr + xl) * 0.6000000238418579f) * 0.5f;
            int w = f->pw, nw = w + 1;
            f->pre[w] = xm;
            f->pw = w >= RVB_FDN_PRE - 1 ? 0 : nw;
            float d = st_fma(f->predelay, 4410.0f, st_fma(lfo, -110.25f, 2205.0f));
            int j = nw - (int)d;
            if (j < 0)
                j += RVB_FDN_PRE;
            int older = j < 1 ? j + RVB_FDN_PRE - 1 : j - 1;
            float a = f->pre[j], x = st_fma(f->pre[older] - a, d - __builtin_truncf(d), a + xm) * 0.15000000596046448f;
            in[0] = st_fma(q2, g, st_fma(q1, g, x));
            in[1] = st_fma(ng, q3, st_fma(ng, q0, x));
            in[2] = st_fma(q3, ng, st_fma(g, q0, x));
            in[3] = st_fma(ng, q2, st_fma(g, q1, x));
        } else {
            in[0] = st_fma(q2, g, q1 * g);
            in[1] = st_fma(ng, q3, ng * q0);
            in[2] = st_fma(q3, ng, g * q0);
            in[3] = st_fma(ng, q2, g * q1);
        }
        for (int k = 0; k < 4; k++) {
            int w = f->w[k], whole = (int)SHIMMER.len[k];
            f->line[k * RVB_FDN_LINE + w] = rvb_fdn_clip(in[k]);
            f->w[k] = w >= whole - 1 ? 0 : w + 1;
        }
        q0 = o[0][i];
        q1 = o[1][i];
        q2 = o[2][i];
        q3 = o[3][i];
        fl = yl[i];
        fr = yr[i];
    }
    f->out[0] = q0;
    f->out[1] = q1;
    f->out[2] = q2;
    f->out[3] = q3;
    s->fb[0] = fl;
    s->fb[1] = fr;
    for (int i = 0; i < n; i++) {
        float xl = p[2 * i], xr = p[2 * i + 1];
        if (mv[i] < 0.5f) {
            p[2 * i] = st_fma(yl[i] * 1.600000023841858f, mv[i], xl);
            p[2 * i + 1] = st_fma(yr[i] + yr[i], mv[i], xr);
        } else {
            float l = yl[i] * 0.800000011920929f;
            if ((int)trail == 1 && !io->enabled) {
                p[2 * i] = l + xl;
                p[2 * i + 1] = yr[i] + xr;
            } else {
                float m = 1.0f - mv[i];
                p[2 * i] = st_fma(xl + xl, m, l);
                p[2 * i + 1] = st_fma(xr + xr, m, yr[i]);
            }
        }
    }
    if (io->enabled)
        f->cleared = 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    struct rvb_fdn *f = &s->f;
    const float *k = io->knobs;
    float decay = rvb_fdn_decay(&SHIMMER, k[2]), ld = st_fma(-k[3], 0.01f, 0.5f), hd = st_fma(-k[4], 0.01f, 0.5f);
    float trail = k[5];
    if (io->frames == 0)
        return;
    const float c = 0.0002267600066261366f;
    float dp = st_fma(k[1], 0.01f, -f->predelay) * c;                                  /* VFNMS */
    float dm = st_fma(k[0], 0.01f, -f->mix) * c;
    float *p = io->io;
    if (io->enabled || trail == 1.0f) {
        /* what the first sample sets (the knobs are the block's) */
        if (f->decay != decay) {
            f->decay = decay;
            f->g = decay * 0.707099974155426f;
        }
        if (f->ld != ld || f->hd != hd) {
            f->ld = ld;
            f->hd = hd;
            damp_set(s, ld, hd);
        }
        block(s, io, trail, dp, dm);
        return;
    }
    for (int i = 0; i < io->frames; i++, p += 2) {
        if (f->decay != decay) {
            f->decay = decay;
            f->g = decay * 0.707099974155426f;
        }
        if (f->ld != ld || f->hd != hd) {
            f->ld = ld;
            f->hd = hd;
            damp_set(s, ld, hd);
        }
        float o4 = f->osc + 0.04643990844488144f, o0 = f->osc + -2047.95361328125f;
        f->osc = 2048.0f > o4 ? o4 : o0;
        f->predelay += dp;
        f->mix += dm;
        float xl = p[0], xr = p[1];
        if (io->enabled) {
            unsigned u = (unsigned)f->osc;
            float t0 = RVB_SINE[u], lfo = st_fma(RVB_SINE[(u + 1) & 0x7ff] - t0, f->osc - __builtin_truncf(f->osc), t0);
            float sl = shift(s, 0, s->fb[0] + xl), sr = shift(s, 1, s->fb[1] + xr);
            float xm = st_fma(sr + sl, 0.4000000059604645f, (xr + xl) * 0.6000000238418579f) * 0.5f;
            int w = f->pw, nw = w + 1;
            f->pre[w] = xm;
            f->pw = w >= RVB_FDN_PRE - 1 ? 0 : nw;
            float d = st_fma(f->predelay, 4410.0f, st_fma(lfo, -110.25f, 2205.0f));
            int j = nw - (int)d;
            if (j < 0)
                j += RVB_FDN_PRE;
            int older = j < 1 ? j + RVB_FDN_PRE - 1 : j - 1;
            float a = f->pre[j], x = st_fma(f->pre[older] - a, d - __builtin_truncf(d), a + xm);
            rvb_fdn_lines(f, 1, x * 0.15000000596046448f);
        } else if (trail == 1.0f) {
            rvb_fdn_lines(f, 0, 0.0f);
        } else {
            if (trail == 0.0f && !f->cleared)
                clear(s);
            continue;
        }
        float *o = f->out;
        float yl = sos_run(&f->damp_c, &f->damp[0], o[2] + o[0]);
        float yr = sos_run(&f->damp_c, &f->damp[1], o[3] + o[1]);
        s->fb[0] = yl;
        s->fb[1] = yr;
        if (f->mix < 0.5f) {
            p[0] = st_fma(yl * 1.600000023841858f, f->mix, xl);
            p[1] = st_fma(yr + yr, f->mix, xr);
        } else {
            float l = yl * 0.800000011920929f;
            if ((int)trail == 1 && !io->enabled) {
                p[0] = l + xl;
                p[1] = yr + xr;
            } else {
                float m = 1.0f - f->mix;
                p[0] = st_fma(xl + xl, m, l);
                p[1] = st_fma(xr + xr, m, yr);
            }
        }
        if (io->enabled)
            f->cleared = 0;
    }
}

const struct wvfx_desc rvb_shimmer_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x10A),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Shimmer",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
