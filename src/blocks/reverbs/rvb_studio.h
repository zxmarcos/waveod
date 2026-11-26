/*
 * The original's Studio-type reverbs (Studio, Club; docs/stock_port.md): one engine, the models giving the output
 * mix of the early and late parts. Single precision.
 *
 * The input, (L + R) / 2 x w (w fading in by 0.0001 per sample), goes into a 14000-sample pre-delay read
 * 1 + 44.1 Pre Delay + 100 Mod (0.5 + 0.5 sin) samples back (between two samples; the sine's period 27562.5
 * samples). That read is written into a 13000-sample tap line: four taps (the model's lengths back, each x g^length)
 * are the early reflections, each added to the feedback of one of four 13000-sample lines (the model's lengths). Each line's read x -g^length through a 2nd-order damping section (Low / Hi Damp; poles first,
 * then zeros) is a late output; the four mixed by a Hadamard matrix / 2 are the lines' feedback. g = 2^(-9.9658 /
 * (66150 Decay + 22050)) (the time to -60 dB). The pre-delay read plus the model's mix of late and early outputs
 * goes through a 2nd-order low pass (Hi Damp) and high pass (Low Damp), then two allpasses per channel (0.75 and
 * 0.625, the model's lengths), x 0.7: the wet (the same on both sides before the
 * allpasses).
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x (bypassed with Trail on: wet + x). Mix and Mod
 * glide 0.0001 per sample, Pre Delay 0.0002267 of the way per sample; Decay, Low and Hi Damp 0.003 per process
 * call, the gains and filters recomputed while they glide (not at the last step: kept). The early taps keep the
 * Init's Decay. Bypassed with Trail on the lines run on without input; with Trail at 0 everything is cleared once
 * (the pre-delay back to the Init's Pre Delay) and the signal passes. The Init sets Low Damp's filters from the raw
 * knob with the x 0.01 folded into the constants (kept).
 */
#pragma once
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

#define RVB_ST_PRE 14000
#define RVB_ST_LINE 13000
#define RVB_ST_DIFF 4500                 /* the allpass buffer's stride (8 slots, 4 used) */
#define RVB_ST_BUFFER ((RVB_ST_PRE + 5 * RVB_ST_LINE + 8 * RVB_ST_DIFF) * (int)sizeof(float))

/* the output filters: the damping section, the high pass and the low pass (num b, den a with a0 = 1) */
struct rvb_st_filt {
    float nd[3], dd[2], nh[3], dh[2], nl[3], dl[2];
};

struct rvb_st_model {
    int taps[4], lens[4];               /* the early taps, the lines */
    int ap[4];                          /* the allpasses: left 1, 2, right 1, 2 */
    float hd_damp[2], hd_lp[2];         /* Hi Damp -> the damping's and the low pass's arguments (a Hi Damp + b) */
    /* the low pass's input: pre + late (late 0 + 2 - late 1 + 3) + early (early 0 + 2 - early 1 + 3), or with
       club ((pre + late 0 + late 2) - (late 1 + late 3)) late + early (...) */
    int club;
    float late, early;
};

struct rvb_st {
    const struct rvb_st_model *m;
    float mix, mod, pre_len, pre_knob, decay, hd, ld, wet, n;
    int cleared;
    float tap_g[4], fdn_g[4];
    struct rvb_st_filt c;
    float *pre, *line, *diff;
    int pw, idx[5], didx[4];
    float early[4], fb[4], xd[4][3], lp[3], hp[3];
};

/* f^n by squaring (the original's integer power) */
static inline float rvb_st_powi(float f, int n)
{
    float r = 1.0f;
    for (unsigned u = (unsigned)n; u; ) {
        if (u & 1u)
            r = r * f;
        u >>= 1;
        if (u)
            f = f * f;
    }
    return r;
}

static inline float rvb_st_g(float decay)
{
    return st_exp2f(-9.965784072875977f / st_fma(decay, 66150.0f, 22050.0f));
}

static inline void rvb_st_fdn_set(struct rvb_st *s, float decay)
{
    float g = rvb_st_g(decay);
    for (int k = 0; k < 4; k++)
        s->fdn_g[k] = -rvb_st_powi(g, s->m->lens[k]);
}

/* the damping section from tan(wh) and tan(wl) (ka, kc: its constants); the high and low pass from hp_w and lp_w */
static inline void rvb_st_filt_set(struct rvb_st_filt *f, float wh, float wl, float hp_w, float lp_w, float ka,
                                   float kc)
{
    float t1 = st_tanf(wh), t2 = st_tanf(wl);
    float a = t2 * ka + 1.0f, b = t2 * ka + -1.0f;
    float c = t2 * kc + 1.0f, d = t2 * kc + -1.0f;
    float r = 1.0f / (c * (t1 + 1.332278847694397f));
    f->nd[0] = (b * (t1 + 0.667721152305603f)) * r;
    f->nd[1] = st_fma(a, t1 + 0.667721152305603f, b * (t1 + -0.667721152305603f)) * r;
    f->nd[2] = (a * (t1 + -0.667721152305603f)) * r;
    f->dd[0] = st_fma(d, t1 + 1.332278847694397f, c * (t1 + -1.332278847694397f)) * r;
    f->dd[1] = (d * (t1 + -1.332278847694397f)) * r;
    float sn = st_sinf(hp_w), cs = st_cosf(hp_w), q = 1.0f / (sn * 0.7072135806083679f + 1.0f);
    f->nh[1] = -((cs + 1.0f) * q);
    f->nh[0] = f->nh[2] = ((cs + 1.0f) * 0.5f) * q;
    f->dh[0] = (cs * -2.0f) * q;
    f->dh[1] = st_fma(-(sn * 0.7072135806083679f), q, q);
    sn = st_sinf(lp_w);
    cs = st_cosf(lp_w);
    q = 1.0f / (sn * 0.7072135806083679f + 1.0f);
    float n1 = st_fma(-cs, q, q);
    f->nl[1] = n1;
    f->nl[0] = f->nl[2] = n1 * 0.5f;
    f->dl[0] = (cs * -2.0f) * q;
    f->dl[1] = st_fma(-(sn * 0.7072135806083679f), q, q);
}

static inline void rvb_st_filters_set(struct rvb_st *s, float wh, float wl, float hp_w, float lp_w)
{
    rvb_st_filt_set(&s->c, wh, wl, hp_w, lp_w, 0.7198700308799744f, 1.2801300287246704f);
}

static inline void rvb_st_clear(struct rvb_st *s)
{
    s->pre_len = st_fma(s->pre_knob, 44.0966682434082f, 1.0f);
    s->n = 0.0f;
    s->pw = 0;
    for (int k = 0; k < 5; k++)
        s->idx[k] = 0;
    for (int k = 0; k < 4; k++) {
        s->didx[k] = 0;
        s->early[k] = s->fb[k] = 0.0f;
        s->xd[k][0] = s->xd[k][1] = s->xd[k][2] = 0.0f;
    }
    s->lp[0] = s->lp[1] = s->lp[2] = s->hp[0] = s->hp[1] = s->hp[2] = 0.0f;
    s->wet = 0.0f;
    __builtin_memset(s->pre, 0, RVB_ST_BUFFER);
    s->cleared = 1;
}

/* k: Mix, Pre Delay, Decay, Low Damp, Hi Damp, Mod */
static inline void rvb_st_init(struct rvb_st *s, const struct rvb_st_model *m, void *buffer, const float *k)
{
    *s = (struct rvb_st){ 0 };
    s->m = m;
    s->pre = buffer;
    s->line = s->pre + RVB_ST_PRE;
    s->diff = s->line + 5 * RVB_ST_LINE;
    s->mix = k[0] * 0.01f;
    s->pre_knob = k[1];
    s->ld = k[3] * 0.01f;
    s->decay = st_taper(st_pow3, k[2]);
    s->mod = k[5] * 0.01f;
    s->hd = st_pow0_4[(int)k[4]];
    rvb_st_clear(s);
    float g = rvb_st_g(s->decay);
    for (int i = 0; i < 4; i++) {
        s->fdn_g[i] = -rvb_st_powi(g, m->lens[i]);
        s->tap_g[i] = rvb_st_powi(g, m->taps[i]);
    }
    rvb_st_filters_set(s, st_fma(s->hd, m->hd_damp[0], m->hd_damp[1]),
                       st_fma(k[3], 9.973309352062643e-05f, 0.0007123792311176658f),
                       st_fma(k[3], 0.00014247585204429924f, 0.014247585088014603f),
                       st_fma(s->hd, m->hd_lp[0], m->hd_lp[1]));
}

/* an allpass of the output: v in, buffer slot `slot`, `size` samples, coefficient c */
static inline float rvb_st_allpass(struct rvb_st *s, int slot, int size, float c, float v)
{
    float *b = s->diff + slot * RVB_ST_DIFF;
    int i = s->didx[slot == 1 ? 0 : slot == 2 ? 1 : slot == 5 ? 2 : 3];
    float old = b[i], u = st_fma(-c, old, v);                                         /* VFMS */
    b[i] = u;
    int next = i + 1;
    s->didx[slot == 1 ? 0 : slot == 2 ? 1 : slot == 5 ? 2 : 3] = next == size ? 0 : next;
    return st_fma(u, c, old);
}

/* one sample: xl, xr in (have_x 0: none), the wet pair out */
static inline void rvb_st_run(struct rvb_st *s, int have_x, float xl, float xr, float *wl, float *wr)
{
    float x = s->wet * (have_x ? (xr + xl) * 0.5f : 0.0f);
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
    /* the tap line and the early reflections */
    float *tap = s->line + 4 * RVB_ST_LINE;
    int t = s->idx[4], nt = t + 1 == RVB_ST_LINE ? 0 : t + 1;
    tap[t] = pre;
    s->idx[4] = nt;
    for (int k = 0; k < 4; k++) {
        int j = nt - s->m->taps[k];
        if (j < 0)
            j += RVB_ST_LINE;
        s->early[k] = tap[j] * s->tap_g[k];
    }
    /* the four lines */
    float late[4];
    for (int k = 0; k < 4; k++) {
        float *b = s->line + k * RVB_ST_LINE;
        int w = s->idx[k], nw = w + 1 == RVB_ST_LINE ? 0 : w + 1, j = nw - s->m->lens[k];
        b[w] = s->fb[k] + s->early[k];
        s->idx[k] = nw;
        if (j < 0)
            j += RVB_ST_LINE;
        float *h = s->xd[k];
        float x0 = st_fma(b[j], s->fdn_g[k], -st_fma(h[2], s->c.dd[1], h[1] * s->c.dd[0]));   /* VFNMS */
        late[k] = st_fma(s->c.nd[0], x0, st_fma(s->c.nd[1], h[1], s->c.nd[2] * h[2]));
        h[2] = h[1];
        h[1] = h[0] = x0;
    }
    float l02 = late[0] + late[2], e = (s->early[0] + s->early[2]) - (s->early[1] + s->early[3]), y;
    if (s->m->club)
        y = st_fma(((pre + late[0]) + late[2]) - (late[1] + late[3]), s->m->late, e * s->m->early);
    else
        y = st_fma(e, s->m->early, st_fma(l02 - (late[1] + late[3]), s->m->late, pre));
    s->fb[2] = ((late[1] - l02) + late[3]) * 0.5f;
    s->fb[1] = ((late[0] - (late[1] + late[2])) + late[3]) * 0.5f;
    s->fb[0] = ((late[1] + late[0]) - (late[3] + late[2])) * 0.5f;
    s->fb[3] = ((late[3] + late[2]) + (late[1] + late[0])) * 0.5f;
    /* low pass, high pass (both with their poles first) */
    float w0 = y - st_fma(s->lp[1], s->c.dl[0], s->lp[2] * s->c.dl[1]);
    float u = st_fma(s->c.nl[0], w0, st_fma(s->c.nl[1], s->lp[1], s->c.nl[2] * s->lp[2]));
    s->lp[2] = s->lp[1];
    s->lp[1] = s->lp[0] = w0;
    float h0 = u - st_fma(s->hp[1], s->c.dh[0], s->hp[2] * s->c.dh[1]);
    float o = st_fma(s->c.nh[0], h0, st_fma(s->c.nh[1], s->hp[1], s->c.nh[2] * s->hp[2]));
    s->hp[2] = s->hp[1];
    s->hp[1] = s->hp[0] = h0;
    const int *ap = s->m->ap;
    float l = rvb_st_allpass(s, 2, ap[1], 0.625f, rvb_st_allpass(s, 1, ap[0], 0.75f, o));
    float r = rvb_st_allpass(s, 6, ap[3], 0.625f, rvb_st_allpass(s, 5, ap[2], 0.75f, o));
    *wl = l * 0.699999988079071f;
    *wr = r * 0.699999988079071f;
}

/*
 * rvb_st_run over a whole block, stage by stage (the same operations on the same values): the lines (2295+ samples)
 * and the allpasses (252+) read samples written before the block, the early taps (5+) samples written before their
 * own sample, so each stage can run over the block in turn, its filters in registers:
 *   1. per sample: the glides, the pre-delay (its read may be a sample of this block), the tap line;
 *   2. the early taps; 3. each line's reads through its damping; 4. per sample: the lines' writes (the feedback of
 *   the sample before), the low pass's input, the next feedback; 5. the low and high pass, the allpasses; the mix.
 * m is the model itself (a constant where it is inlined).
 */
static inline void rvb_st_block(struct rvb_st *s, const struct rvb_st_model *m, const struct wvfx_io *io,
                                float mix, float mod, float dp)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES, have_x = io->enabled != 0;
    float *p = io->io, pr[WVFX_MAX_FRAMES], mv[WVFX_MAX_FRAMES], e[4][WVFX_MAX_FRAMES], lt[4][WVFX_MAX_FRAMES];
    float *tap = s->line + 4 * RVB_ST_LINE;
    const int t0 = s->idx[4];
    /* 1 */
    for (int i = 0; i < n; i++) {
        s->mod = st_glide(s->mod, mod);
        s->mix = st_glide(s->mix, mix);
        mv[i] = s->mix;
        s->pre_len += dp;
        float x = s->wet * (have_x ? (p[2 * i + 1] + p[2 * i]) * 0.5f : 0.0f);
        s->wet = st_glide(s->wet, 1.0f);
        float ph = s->n * 0.0002279613836435601f, next = s->n + 1.0f;
        s->n = 27562.498046875f > next ? next : 0.0f;
        int pw = s->pw, npw = pw >= RVB_ST_PRE - 1 ? 0 : pw + 1;
        s->pre[pw] = x;
        s->pw = npw;
        float len = st_fma(s->mod * 100.0f, st_fma(st_sinf(ph), 0.5f, 0.5f), s->pre_len);
        int j = npw - (int)len;
        if (j < 0)
            j += RVB_ST_PRE;
        int older = j < 1 ? j + RVB_ST_PRE - 1 : j - 1;
        float a = s->pre[j];
        pr[i] = st_fma(s->pre[older] - a, len - __builtin_truncf(len), a);                    /* VRINTZ */
        int t = s->idx[4];
        tap[t] = pr[i];
        s->idx[4] = t + 1 == RVB_ST_LINE ? 0 : t + 1;
    }
    /* 2: sample i's taps, nt = its write + 1 */
    for (int k = 0; k < 4; k++) {
        int j = t0 + 1 - m->taps[k];
        if (j < 0)
            j += RVB_ST_LINE;
        if (j >= RVB_ST_LINE)
            j -= RVB_ST_LINE;
        const float g = s->tap_g[k];
        for (int i = 0; i < n; i++) {
            e[k][i] = tap[j] * g;
            j = j + 1 == RVB_ST_LINE ? 0 : j + 1;
        }
        s->early[k] = e[k][n - 1];
    }
    /* 3 */
    const float d0 = s->c.dd[0], d1 = s->c.dd[1], n0 = s->c.nd[0], n1 = s->c.nd[1], n2 = s->c.nd[2];
    for (int k = 0; k < 4; k++) {
        const float *b = s->line + k * RVB_ST_LINE, fg = s->fdn_g[k];
        int j = s->idx[k] + 1 - m->lens[k];
        if (j < 0)
            j += RVB_ST_LINE;
        float *h = s->xd[k], h1 = h[1], h2 = h[2];
        for (int i = 0; i < n; i++) {
            float x0 = st_fma(b[j], fg, -st_fma(h2, d1, h1 * d0));                              /* VFNMS */
            lt[k][i] = st_fma(n0, x0, st_fma(n1, h1, n2 * h2));
            h2 = h1;
            h1 = x0;
            j = j + 1 == RVB_ST_LINE ? 0 : j + 1;
        }
        h[2] = h2;
        h[1] = h[0] = h1;
    }
    /* 4: pr becomes the low pass's input */
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 4; k++) {
            int w = s->idx[k];
            s->line[k * RVB_ST_LINE + w] = s->fb[k] + e[k][i];
            s->idx[k] = w + 1 == RVB_ST_LINE ? 0 : w + 1;
        }
        float l0 = lt[0][i], l1 = lt[1][i], l2 = lt[2][i], l3 = lt[3][i], pre = pr[i];
        float l02 = l0 + l2, ee = (e[0][i] + e[2][i]) - (e[1][i] + e[3][i]);
        if (m->club)
            pr[i] = st_fma(((pre + l0) + l2) - (l1 + l3), m->late, ee * m->early);
        else
            pr[i] = st_fma(ee, m->early, st_fma(l02 - (l1 + l3), m->late, pre));
        s->fb[2] = ((l1 - l02) + l3) * 0.5f;
        s->fb[1] = ((l0 - (l1 + l2)) + l3) * 0.5f;
        s->fb[0] = ((l1 + l0) - (l3 + l2)) * 0.5f;
        s->fb[3] = ((l3 + l2) + (l1 + l0)) * 0.5f;
    }
    /* 5: low pass, high pass (poles first), into pr */
    {
        float a0 = s->c.dl[0], a1 = s->c.dl[1], b0 = s->c.nl[0], b1 = s->c.nl[1], b2 = s->c.nl[2];
        float c0 = s->c.dh[0], c1 = s->c.dh[1], e0 = s->c.nh[0], e1 = s->c.nh[1], e2 = s->c.nh[2];
        float p1 = s->lp[1], p2 = s->lp[2], q1 = s->hp[1], q2 = s->hp[2];
        for (int i = 0; i < n; i++) {
            float w0 = pr[i] - st_fma(p1, a0, p2 * a1);
            float u = st_fma(b0, w0, st_fma(b1, p1, b2 * p2));
            p2 = p1;
            p1 = w0;
            float h0 = u - st_fma(q1, c0, q2 * c1);
            pr[i] = st_fma(e0, h0, st_fma(e1, q1, e2 * q2));
            q2 = q1;
            q1 = h0;
        }
        s->lp[2] = p2;
        s->lp[1] = s->lp[0] = p1;
        s->hp[2] = q2;
        s->hp[1] = s->hp[0] = q1;
    }
    /* the allpasses: left 1 then 2 into e[0], right 5 then 6 into e[1] (rvb_st_allpass's slots) */
    static const int slot[4] = { 1, 2, 5, 6 };
    static const float coef[4] = { 0.75f, 0.625f, 0.75f, 0.625f };
    for (int a = 0; a < 4; a++) {
        float *b = s->diff + slot[a] * RVB_ST_DIFF;
        const float *src = (a & 1) ? e[a >> 1] : pr;
        float *dst = e[a >> 1];
        const float c = coef[a];
        const int size = m->ap[a];
        int ix = s->didx[a];
        for (int i = 0; i < n; i++) {
            float old = b[ix], u = st_fma(-c, old, src[i]);                                   /* VFMS */
            b[ix] = u;
            ix = ix + 1 == size ? 0 : ix + 1;
            dst[i] = st_fma(u, c, old);
        }
        s->didx[a] = ix;
    }
    for (int i = 0; i < n; i++) {
        float xl = p[2 * i], xr = p[2 * i + 1], l = e[0][i] * 0.699999988079071f, r = e[1][i] * 0.699999988079071f;
        if (mv[i] < 0.5f) {
            float mm = mv[i] + mv[i];
            p[2 * i] = st_fma(mm, l, xl);
            p[2 * i + 1] = st_fma(mm, r, xr);
        } else if (!io->enabled) {
            p[2 * i] = l + xl;
            p[2 * i + 1] = r + xr;
        } else {
            float mm = 1.0f - mv[i];
            p[2 * i] = st_fma(xl + xl, mm, l);
            p[2 * i + 1] = st_fma(xr + xr, mm, r);
        }
    }
    if (io->enabled)
        s->cleared = 0;
}

/* k: Mix, Pre Delay, Decay, Low Damp, Hi Damp, Mod, Trail; m the model (the same as s->m, a constant) */
static inline void rvb_st_process(struct rvb_st *s, const struct rvb_st_model *m, const struct wvfx_io *io,
                                  const float *k)
{
    float mix = k[0] * 0.01f, mod = k[5] * 0.01f, decay = st_taper(st_pow3, k[2]), hd = st_pow0_4[(int)k[4]];
    float ld = k[3] * 0.01f;
    s->decay = st_glide_by(s->decay, decay, 0.003000000026077032f);
    s->hd = st_glide_by(s->hd, hd, 0.003000000026077032f);
    s->ld = st_glide_by(s->ld, ld, 0.003000000026077032f);
    if (s->decay != decay)
        rvb_st_fdn_set(s, s->decay);
    if (s->ld != ld || s->hd != hd)
        rvb_st_filters_set(s, st_fma(s->hd, s->m->hd_damp[0], s->m->hd_damp[1]),
                           st_fma(s->ld, 0.009973309934139252f, 0.0007123792311176658f),
                           st_fma(s->ld, 0.014247585088014603f, 0.014247585088014603f),
                           st_fma(s->hd, s->m->hd_lp[0], s->m->hd_lp[1]));
    if (io->frames == 0)
        return;
    float dp = (st_fma(k[1], 44.0966682434082f, 1.0f) - s->pre_len) * 0.00022669999452773482f;
    int trail = (int)k[6];
    if (io->enabled || trail == 1) {
        rvb_st_block(s, m, io, mix, mod, dp);
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
            rvb_st_run(s, 1, xl, xr, &l, &r);
        } else if (trail == 1) {
            rvb_st_run(s, 0, xl, xr, &l, &r);
        } else {
            if (trail == 0 && !s->cleared)
                rvb_st_clear(s);
            continue;
        }
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            p[0] = st_fma(m, l, xl);
            p[1] = st_fma(m, r, xr);
        } else if (!io->enabled) {
            p[0] = l + xl;
            p[1] = r + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l);
            p[1] = st_fma(xr + xr, m, r);
        }
    }
}
