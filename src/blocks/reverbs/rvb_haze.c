/*
 * Haze (REVERB): the original's Haze reverb (QME-200 V1.2.2, library entry 10.11, id 0x0C000015) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). An 8-line FDN in the Studio's frame (rvb_studio.h: the
 * pre-delay, the damping, high and low pass forms). Single precision.
 *
 * The input, (L + R) / 4 plus 0.025 x the sum of the eight lines' last reads, x w (w fading in by 0.0001 per sample),
 * goes into the 14000-sample pre-delay read 1 + 44.1 Pre Delay + 100 Mod' (0.5 + 0.5 sin) samples back (Mod' = 0.3 +
 * 0.007 Mod). Each of eight 10000-sample lines (3114, 3278, 3485, 3691, 3956, 4175, 4497, 4793 samples) takes 0.75
 * (pre + the 8 x 8 Hadamard matrix (+-(0.3 + 0.145 Decay)) of the lines' outputs); its read (between two samples)
 * through a 2nd-order damping section (Low / Hi Damp) is its output. L = the even lines' outputs, R = the odd ones',
 * each through a 2nd-order low pass (Hi Damp) and high pass (Low Damp), x 0.5 x w.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x (bypassed with Trail on: wet + x). Mix and Mod
 * glide 0.0001 per sample, Pre Delay 0.0002267 of the way per sample; Decay, Low and Hi Damp 0.003 per process call,
 * the matrix and filters recomputed while they glide (not at the last step: kept). Bypassed with Trail on the lines
 * run on without the dry input; with Trail at 0 everything is cleared once (the pre-delay back to the Init's Pre
 * Delay) and the signal passes. The Init reads Decay's taper at the knob's value instead of x 2.55 and sets Low / Hi
 * Damp's filters from the raw knobs with the x 0.01 folded into the constants (kept).
 */
#include "rvb_studio.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 25.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 300.0f, 1.0f, 100.0f, 1, 0x88, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 90.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Low Damp", 0.0f, 100.0f, 1.0f, 0.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Hi Damp", 0.0f, 100.0f, 1.0f, 0.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Mod", 0.0f, 100.0f, 1.0f, 100.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

#define LINE 10000
#define BUFFER ((RVB_ST_PRE + 8 * LINE) * (int)sizeof(float))

static const float LENS[8] = { 3114.0f, 3278.0f, 3485.0f, 3691.0f, 3956.0f, 4175.0f, 4497.0f, 4793.0f };

struct state {
    float mix, mod, pre_len, pre_knob, decay, hd, ld, wet, n, g;
    int cleared;
    struct rvb_st_filt c;
    float *pre, *line;
    int pw, w[8];
    float fb[8], out[8], xd[8][3], lp[2][3], hp[2][3];
};

static void filters_set(struct state *s, float wh, float wl, float hp_w, float lp_w)
{
    rvb_st_filt_set(&s->c, wh, wl, hp_w, lp_w, 0.667721152305603f, 1.332278847694397f);
}

static void clear(struct state *s)
{
    s->pre_len = st_fma(s->pre_knob, 44.0966682434082f, 1.0f);
    s->n = 0.0f;
    s->pw = 0;
    for (int k = 0; k < 8; k++) {
        s->w[k] = 0;
        s->fb[k] = s->out[k] = 0.0f;
        s->xd[k][0] = s->xd[k][1] = s->xd[k][2] = 0.0f;
    }
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 3; i++)
            s->lp[c][i] = s->hp[c][i] = 0.0f;
    s->wet = 0.0f;
    __builtin_memset(s->pre, 0, BUFFER);
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->pre = buffer;
    s->line = s->pre + RVB_ST_PRE;
    s->mix = k[0] * 0.01f;
    s->ld = k[3] * 0.01f;
    s->pre_knob = k[1];
    s->decay = st_pow0_5[(int)k[2]];
    s->mod = st_fma(k[5], 0.00699999975040555f, 0.30000001192092896f);
    s->hd = k[4] * 0.01f;
    clear(s);
    s->g = st_fma(s->decay, 0.14499999582767487f, 0.30000001192092896f);
    filters_set(s, st_fma(k[4], -0.0056990343146026134f, 0.9260930418968201f),
                st_fma(k[3], 9.973309352062643e-05f, 0.0007123792311176658f),
                st_fma(k[3], 0.0002849517040885985f, 0.014247585088014603f),
                st_fma(k[4], -0.008548551239073277f, 1.8521860837936401f));
    return 0;
}

/* 2nd order, poles first: w = x - (a1 w1 + a2 w2), y = b0 w + b1 w1 + b2 w2 */
static float df2(float *h, const float *b, const float *a, float x)
{
    float w = x - st_fma(h[2], a[1], h[1] * a[0]);
    float y = st_fma(b[0], w, st_fma(b[1], h[1], b[2] * h[2]));
    h[2] = h[1];
    h[1] = h[0] = w;
    return y;
}

/* one sample: the wet pair before w and the mix */
static void run(struct state *s, int have_x, float xl, float xr, float *wl, float *wr)
{
    float sum = s->fb[1] + s->fb[0];
    for (int k = 2; k < 8; k++)
        sum = s->fb[k] + sum;
    float x = s->wet * st_fma(sum, 0.02500000037252903f, have_x ? (xr + xl) * 0.25f : 0.0f);
    s->wet = st_glide(s->wet, 1.0f);
    float ph = s->n * 0.0002279613836435601f, next = s->n + 1.0f;
    s->n = 27562.498046875f > next ? next : 0.0f;
    int pw = s->pw, npw = pw >= RVB_ST_PRE - 1 ? 0 : pw + 1;
    s->pre[pw] = x;
    s->pw = npw;
    float len = st_fma(s->mod * 100.0f, st_fma(st_sinf(ph), 0.5f, 0.5f), s->pre_len);
    int i = npw - (int)len;
    if (i < 0)
        i += RVB_ST_PRE;
    int older = i < 1 ? i + RVB_ST_PRE - 1 : i - 1;
    float a = s->pre[i], pre = st_fma(s->pre[older] - a, len - __builtin_truncf(len), a);   /* VRINTZ */
    /* the matrix: entry (k, j) is +-g by the parity of k & j */
    float g = s->g, in[8];
    for (int k = 0; k < 8; k++) {
        /* rows 4-7 sum onto a 0.0 the original keeps on its stack */
        float acc = k < 4 ? s->out[0] * g : st_fma(s->out[0], g, 0.0f);
        for (int j = 1; j < 8; j++)
            acc = st_fma(s->out[j], __builtin_popcount(k & j) & 1 ? -g : g, acc);
        in[k] = acc;
    }
    for (int k = 0; k < 8; k++) {
        float *b = s->line + k * LINE;
        int w = s->w[k], nw = w > LINE - 2 ? 0 : w + 1, whole = (int)LENS[k], j = nw - whole;
        b[w] = (pre + in[k]) * 0.75f;
        s->w[k] = nw;
        if (j < 0)
            j += LINE;
        float r0 = b[j], r = st_fma(b[j <= 0 ? j + LINE - 1 : j - 1] - r0, LENS[k] - __builtin_truncf(LENS[k]), r0);
        s->fb[k] = r;
        s->out[k] = df2(s->xd[k], s->c.nd, s->c.dd, r);
    }
    float *o = s->out;
    float l = ((o[2] + o[0]) + o[4]) + o[6], rr = ((o[3] + o[1]) + o[5]) + o[7];
    l = df2(s->hp[0], s->c.nh, s->c.dh, df2(s->lp[0], s->c.nl, s->c.dl, l));
    rr = df2(s->hp[1], s->c.nh, s->c.dh, df2(s->lp[1], s->c.nl, s->c.dl, rr));
    *wl = l * 0.5f;
    *wr = rr * 0.5f;
}

/*
 * run() over a whole block, stage by stage (the same operations on the same values): the lines (3114+ samples)
 * read samples written before the block, so their reads and damping run over the block first; then per sample the
 * pre-delay (its read may be in this block), the matrix from the outputs one sample before and the writes; then the
 * output filters over the block. The glides of process() per sample as there.
 */
static void run_block(struct state *s, const struct wvfx_io *io, float mod, float mix, float dp)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES, have_x = io->enabled != 0;
    float *p = io->io, fb[8][WVFX_MAX_FRAMES], o[8][WVFX_MAX_FRAMES], mv[WVFX_MAX_FRAMES], wv[WVFX_MAX_FRAMES];
    /* 1: each line's reads (its fb) and their damping (its out), the coefficients in registers */
    const float a0 = s->c.dd[0], a1 = s->c.dd[1], b0 = s->c.nd[0], b1 = s->c.nd[1], b2 = s->c.nd[2];
    for (int k = 0; k < 8; k++) {
        const float *b = s->line + k * LINE;
        const int whole = (int)LENS[k];
        const float frac = LENS[k] - __builtin_truncf(LENS[k]);
        int w = s->w[k];
        float *h = s->xd[k], h1 = h[1], h2 = h[2];
        for (int i = 0; i < n; i++) {
            int nw = w > LINE - 2 ? 0 : w + 1, j = nw - whole;
            if (j < 0)
                j += LINE;
            float r0 = b[j], r = st_fma(b[j <= 0 ? j + LINE - 1 : j - 1] - r0, frac, r0);
            fb[k][i] = r;
            float x = r - st_fma(h2, a1, h1 * a0);                  /* df2 */
            o[k][i] = st_fma(b0, x, st_fma(b1, h1, b2 * h2));
            h2 = h1;
            h1 = x;
            w = nw;
        }
        h[2] = h2;
        h[1] = h[0] = h1;
    }
    /* 2: per sample the glides, the input from the feedback before, the pre-delay, the matrix from the outputs
       before, the writes */
    float g = s->g, q[8], fp[8];                /* the outputs and the feedback of the sample before */
    for (int k = 0; k < 8; k++) {
        q[k] = s->out[k];
        fp[k] = s->fb[k];
    }
    for (int i = 0; i < n; i++) {
        s->mod = st_glide(s->mod, mod);
        s->mix = st_glide(s->mix, mix);
        mv[i] = s->mix;
        s->pre_len += dp;
        float sum = fp[1] + fp[0];
#pragma clang loop unroll(full)
        for (int k = 2; k < 8; k++)
            sum = fp[k] + sum;
#pragma clang loop unroll(full)
        for (int k = 0; k < 8; k++)
            fp[k] = fb[k][i];
        float x = s->wet * st_fma(sum, 0.02500000037252903f, have_x ? (p[2 * i + 1] + p[2 * i]) * 0.25f : 0.0f);
        s->wet = st_glide(s->wet, 1.0f);
        wv[i] = s->wet;
        float ph = s->n * 0.0002279613836435601f, next = s->n + 1.0f;
        s->n = 27562.498046875f > next ? next : 0.0f;
        int pw = s->pw, npw = pw >= RVB_ST_PRE - 1 ? 0 : pw + 1;
        s->pre[pw] = x;
        s->pw = npw;
        float len = st_fma(s->mod * 100.0f, st_fma(st_sinf(ph), 0.5f, 0.5f), s->pre_len);
        int ii = npw - (int)len;
        if (ii < 0)
            ii += RVB_ST_PRE;
        int older = ii < 1 ? ii + RVB_ST_PRE - 1 : ii - 1;
        float a = s->pre[ii], pre = st_fma(s->pre[older] - a, len - __builtin_truncf(len), a);  /* VRINTZ */
        float in[8], ng = -g;
#pragma clang loop unroll(full)                     /* the signs constant */
        for (int k = 0; k < 8; k++) {
            /* rows 4-7 sum onto a 0.0 the original keeps on its stack */
            float acc = k < 4 ? q[0] * g : st_fma(q[0], g, 0.0f);
#pragma clang loop unroll(full)
            for (int j = 1; j < 8; j++)
                acc = st_fma(q[j], __builtin_popcount(k & j) & 1 ? ng : g, acc);
            in[k] = acc;
        }
#pragma clang loop unroll(full)
        for (int k = 0; k < 8; k++) {
            int w = s->w[k];
            s->line[k * LINE + w] = (pre + in[k]) * 0.75f;
            s->w[k] = w > LINE - 2 ? 0 : w + 1;
            q[k] = o[k][i];
        }
    }
    for (int k = 0; k < 8; k++) {
        s->fb[k] = fb[k][n - 1];
        s->out[k] = q[k];
    }
    /* 3: the two sums through the low and high pass, x 0.5, the mix */
    for (int c = 0; c < 2; c++) {
        const float la0 = s->c.dl[0], la1 = s->c.dl[1], lb0 = s->c.nl[0], lb1 = s->c.nl[1], lb2 = s->c.nl[2];
        const float ha0 = s->c.dh[0], ha1 = s->c.dh[1], hb0 = s->c.nh[0], hb1 = s->c.nh[1], hb2 = s->c.nh[2];
        float *lh = s->lp[c], *hh = s->hp[c], l1 = lh[1], l2 = lh[2], q1 = hh[1], q2 = hh[2];
        for (int i = 0; i < n; i++) {
            float v = c ? ((o[3][i] + o[1][i]) + o[5][i]) + o[7][i] : ((o[2][i] + o[0][i]) + o[4][i]) + o[6][i];
            float x = v - st_fma(l2, la1, l1 * la0);
            float u = st_fma(lb0, x, st_fma(lb1, l1, lb2 * l2));
            l2 = l1;
            l1 = x;
            float y = u - st_fma(q2, ha1, q1 * ha0);
            float r = st_fma(hb0, y, st_fma(hb1, q1, hb2 * q2));
            q2 = q1;
            q1 = y;
            fb[c][i] = r * 0.5f;                    /* the wet of channel c (fb no longer needed) */
        }
        lh[2] = l2;
        lh[1] = lh[0] = l1;
        hh[2] = q2;
        hh[1] = hh[0] = q1;
    }
    for (int i = 0; i < n; i++) {
        float xl = p[2 * i], xr = p[2 * i + 1], wl = wv[i] * fb[0][i], wr = wv[i] * fb[1][i];
        if (mv[i] < 0.5f) {
            p[2 * i] = st_fma(wl + wl, mv[i], xl);
            p[2 * i + 1] = st_fma(wr + wr, mv[i], xr);
        } else if (!io->enabled) {
            p[2 * i] = wl + xl;
            p[2 * i + 1] = wr + xr;
        } else {
            float m = 1.0f - mv[i];
            p[2 * i] = st_fma(xl + xl, m, wl);
            p[2 * i + 1] = st_fma(xr + xr, m, wr);
        }
    }
    if (io->enabled)
        s->cleared = 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float mix = k[0] * 0.01f, hd = k[4] * 0.01f, mod = st_fma(k[5], 0.00699999975040555f, 0.30000001192092896f);
    float decay = st_taper(st_pow0_5, k[2]), ld = k[3] * 0.01f;
    s->decay = st_glide_by(s->decay, decay, 0.003000000026077032f);
    s->hd = st_glide_by(s->hd, hd, 0.003000000026077032f);
    s->ld = st_glide_by(s->ld, ld, 0.003000000026077032f);
    if (s->decay != decay)
        s->g = st_fma(s->decay, 0.14499999582767487f, 0.30000001192092896f);
    if (s->ld != ld || s->hd != hd)
        filters_set(s, st_fma(s->hd, -0.5699034333229065f, 0.9260930418968201f),
                    st_fma(s->ld, 0.009973309934139252f, 0.0007123792311176658f),
                    st_fma(s->ld, 0.028495170176029205f, 0.014247585088014603f),
                    st_fma(s->hd, -0.8548551201820374f, 1.8521860837936401f));
    if (io->frames == 0)
        return;
    float dp = (st_fma(k[1], 44.0966682434082f, 1.0f) - s->pre_len) * 0.00022669999452773482f;
    int trail = (int)k[6];
    if (io->enabled || trail == 1) {
        run_block(s, io, mod, mix, dp);
        return;
    }
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mod = st_glide(s->mod, mod);
        s->mix = st_glide(s->mix, mix);
        s->pre_len += dp;
        float xl = p[0], xr = p[1], l, r;
        if (io->enabled) {
            s->cleared = 0;
            run(s, 1, xl, xr, &l, &r);
        } else if (trail == 1) {
            run(s, 0, xl, xr, &l, &r);
        } else {
            if (trail == 0 && !s->cleared)
                clear(s);
            continue;
        }
        float wl = s->wet * l, wr = s->wet * r;
        if (s->mix < 0.5f) {
            p[0] = st_fma(wl + wl, s->mix, xl);
            p[1] = st_fma(wr + wr, s->mix, xr);
        } else if (!io->enabled) {
            p[0] = wl + xl;
            p[1] = wr + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, wl);
            p[1] = st_fma(xr + xr, m, wr);
        }
    }
}

const struct wvfx_desc rvb_haze_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x10B),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Haze",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
