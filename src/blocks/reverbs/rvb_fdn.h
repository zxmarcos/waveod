/*
 * The original's FDN reverbs (Room, Hall, Church, Sky, Sea; docs/stock_port.md): one engine, the models giving its
 * constants. Single precision.
 *
 * The input, (L + R) / 2, goes into a 9000-sample line read 2205 + 4410 Pre Delay - 220.5 Depth lfo samples back
 * (between two samples; lfo from the 2049-point sine table, its phase stepping Rate x 0.04644 per sample through
 * 2048 points); that read plus the input, x 0.15, feeds four 4100-sample lines read their lengths back. Each line
 * takes the input plus two of the others' last outputs through the matrix (+-g, g = 0.7071 Decay), clipped to
 * +-1.7; its read, moved toward the line's previous read by the length's fraction, through a 2nd-order damping
 * filter is its output. L = low pass ((out 0 + out 2) x a gain), R = low pass (out 1 + out 3), the low pass divided by
 * its a0.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x (bypassed with Trail on: wet + x). Mix and Pre
 * Delay glide 0.00022676 of the way to the knobs each sample; the matrix follows Decay at once. Bypassed with Trail
 * on the lines run on without input (the pre-delay line stands still); with Trail at 0 everything is cleared once and
 * the signal passes.
 */
#pragma once
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"
#include "rvb_sine.h"

#define RVB_FDN_PRE 9000
#define RVB_FDN_LINE 4100
#define RVB_FDN_BUFFER ((RVB_FDN_PRE + 4 * RVB_FDN_LINE) * (int)sizeof(float))

struct rvb_fdn_model {
    float decay_scale, decay_offset;    /* Decay = scale pow0_5(knob) + offset */
    float depth;                        /* the pre-delay line's modulation depth */
    float predelay;                     /* 0: Pre Delay from its knob (gliding); < 0: none; else fixed at this */
    float l_gain;                       /* the left sum's scale */
    float init_g;                       /* 0: the Init's matrix from Decay; else pow0_5(knob) x this (folded) */
    int hi_damp;                        /* 1: the damping set by High Damp (the Plate); 2: by Lo / Hi End */
    int damp_sums;                      /* the damping on the two output sums (the lines feed back their reads) */
    int full;                           /* the full 4 x 4 Hadamard matrix (+-Decay / 2; the Spring) */
    float len[4];                       /* the four lines' lengths */
    struct sos damp, lowpass;           /* lowpass: divided by a0 */
};

struct rvb_fdn {
    const struct rvb_fdn_model *m;
    float mix, predelay, decay, g, osc, hd, ld;
    struct sos damp_c;
    int cleared;
    float *pre, *line;
    int pw, w[4];
    float prev[4], out[4];
    struct sos_state damp[4], lp[2];
};

static inline float rvb_fdn_decay(const struct rvb_fdn_model *m, float knob)
{
    return st_fma(st_taper(st_pow0_5, knob), m->decay_scale, m->decay_offset);
}

/* a high shelf from e = 2^x: from 2 / (e + 1) and 2 / (1 / e + 1), k the shelf's offset, np / nn the zeros' and
   dp / dn the poles' constants */
static inline void rvb_fdn_shelf(struct rvb_fdn *f, float x, float k, float np, float nn, float dp, float dn)
{
    float e = st_exp2f(x), r = 1.0f / e;
    float a = 2.0f / (e + 1.0f), b = 2.0f / (r + 1.0f);
    float p = a + k, q = k - a, u = k - b, v = b + k;
    float n1 = st_fma(v, np, u * nn), w = p * dp, d0 = st_fma(q, dp, p * dn);
    f->damp_c = (struct sos){ (v * nn) / w, n1 / w, (u * np) / w, 1.0f, d0 / w, (q * dn) / w };
}

/* the Plate's damping from High Damp */
static inline void rvb_fdn_hi_damp(struct rvb_fdn *f, float x)
{
    rvb_fdn_shelf(f, x, 0.17270000278949738f, 1.0515958070755005f, -0.9484041929244995f, 1.05100417137146f,
                  -0.94899582862854f);
}

/* the Spring's from Tone (t = 1 - Tone / 100) */
static inline void rvb_fdn_tone_damp(struct rvb_fdn *f, float t)
{
    rvb_fdn_shelf(f, t * -0.49828922748565674f, 0.21702805161476135f, 1.217028021812439f, -0.782971978187561f,
                  1.217028021812439f, -0.782971978187561f);
}

/* the Mod Reverb's and Shimmer's damping: low and high shelves from Lo End and Hi End (dl, dh = 0.5 - End / 100);
   lo the low shelf's exponent offset, lc and hc the shelves' constants */
static inline void rvb_fdn_lohi_damp(struct rvb_fdn *f, float dl, float dh, float lo, float lc, float hc)
{
    float eh = st_exp2f(-st_fma(dh, 0.9965784549713135f, 0.49828922748565674f));               /* VFNMA */
    float el = st_exp2f(-st_fma(dl, 0.9965784549713135f, lo));
    float h2 = 2.0f / (1.0f / eh + 1.0f), l2 = lc / (1.0f / el + 1.0f);
    float h1 = 2.0f / (eh + 1.0f), l1 = lc / (el + 1.0f);
    float u = hc - h2, v = h2 + hc, q = hc - h1, p = h1 + hc;
    float lm2 = l2 + -1.0f, lp2 = l2 + 1.0f, lm1 = l1 + -1.0f, lp1 = l1 + 1.0f;
    float w = p * lp1;
    f->damp_c = (struct sos){ (v * lm2) / w, st_fma(v, lp2, u * lm2) / w, (u * lp2) / w, 1.0f,
                              st_fma(p, lm1, q * lp1) / w, (q * lm1) / w };
}

/* the Mod Reverb's */
static inline void rvb_fdn_mod_damp(struct rvb_fdn *f, float dl, float dh)
{
    rvb_fdn_lohi_damp(f, dl, dh, -0.08304820209741592f, 0.1141042485833168f, 0.21702805161476135f);
}

/* x >= 1.7 ? 1.7 : x > -1.7 ? x : -1.7, as VMAXNM / VMINNM (no flags): the same for every x, NaN (-1.7) and -0
   included */
static inline float rvb_fdn_clip(float x)
{
    return __builtin_fminf(__builtin_fmaxf(x, -1.7000000476837158f), 1.7000000476837158f);
}

static inline void rvb_fdn_clear(struct rvb_fdn *f)
{
    f->osc = 0.0f;
    f->pw = 0;
    for (int k = 0; k < 4; k++) {
        f->w[k] = 0;
        f->prev[k] = f->out[k] = 0.0f;
        f->damp[k] = (struct sos_state){ 0 };
    }
    f->lp[0] = f->lp[1] = (struct sos_state){ 0 };
    __builtin_memset(f->pre, 0, RVB_FDN_BUFFER);
    f->cleared = 1;
}

static inline void rvb_fdn_init(struct rvb_fdn *f, const struct rvb_fdn_model *m, void *buffer, float mix,
                                float predelay, float decay)
{
    *f = (struct rvb_fdn){ 0 };
    f->m = m;
    f->pre = buffer;
    f->line = f->pre + RVB_FDN_PRE;
    f->decay = rvb_fdn_decay(m, decay);
    f->g = m->init_g != 0.0f ? st_taper(st_pow0_5, decay) * m->init_g
                             : f->decay * (m->full ? 0.5f : 0.707099974155426f);
    f->mix = mix * 0.01f;
    f->predelay = m->predelay > 0.0f ? m->predelay : m->predelay < 0.0f ? 0.0f : predelay * 0.01f;
    f->damp_c = m->damp;
    rvb_fdn_clear(f);
}

/*
 * Line k: write in, the read len back moved toward the previous read by len's fraction. The read is always the
 * sample written whole - 1 samples ago (whole = (int)len), so the line is a ring of exactly whole samples (in its
 * RVB_FDN_LINE): the read is the next write's place, whose cache line the read has just fetched (the lines are in
 * the SDRAM, where a missed write costs a line fetch too). The same samples as a ring of any larger size.
 */
static inline float rvb_fdn_line(struct rvb_fdn *f, int k, float in)
{
    float len = f->m->len[k], *b = f->line + k * RVB_FDN_LINE;
    int w = f->w[k], whole = (int)len, i = w >= whole - 1 ? 0 : w + 1;
    b[w] = rvb_fdn_clip(in);
    f->w[k] = i;
    float a = b[i], x = st_fma(f->prev[k] - a, len - (float)whole, a);
    f->prev[k] = a;
    return x;
}

/* the four lines: each the input (none with have_x 0) plus two outputs through the matrix */
static inline void rvb_fdn_lines(struct rvb_fdn *f, int have_x, float x)
{
    float g = f->g, ng = -g, *o = f->out, in[4];
    if (f->m->full) {
        static const signed char S[4][4] = { { 1, 1, 1, 1 }, { 1, -1, 1, -1 }, { 1, 1, -1, -1 }, { 1, -1, -1, 1 } };
        for (int k = 0; k < 4; k++) {
            float y = have_x ? st_fma(o[0], g, x) : o[0] * g;
            for (int j = 1; j < 4; j++)
                y = st_fma(o[j], S[k][j] > 0 ? g : ng, y);
            in[k] = y;
        }
    } else if (have_x) {
        in[0] = st_fma(o[2], g, st_fma(o[1], g, x));
        in[1] = st_fma(ng, o[3], st_fma(ng, o[0], x));
        in[2] = st_fma(o[3], ng, st_fma(g, o[0], x));
        in[3] = st_fma(ng, o[2], st_fma(g, o[1], x));
    } else {
        in[0] = st_fma(o[2], g, o[1] * g);
        in[1] = st_fma(ng, o[3], ng * o[0]);
        in[2] = st_fma(o[3], ng, g * o[0]);
        in[3] = st_fma(ng, o[2], g * o[1]);
    }
    float r[4];
    for (int k = 0; k < 4; k++)
        r[k] = rvb_fdn_line(f, k, in[k]);
    for (int k = 0; k < 4; k++)
        o[k] = f->m->damp_sums ? r[k] : sos_run(&f->damp_c, &f->damp[k], r[k]);
}

/*
 * rvb_fdn_process over a whole block, stage by stage (the same operations on the same values as sample by sample,
 * below): every line is at least 2187 samples long and the pre-delay at least ~1984, longer than a block, so every
 * read of the block comes from samples written before it.
 *   1. per sample: the lfo, the glides, the pre-delay line (write, read) -> the lines' input x;
 *   2. per line: the block's reads (each moved toward the one before), then its damping, in registers;
 *   3. per sample: the matrix from the outputs one sample before, clipped, written;
 *   4. the two sums, their damping, the low passes, the mix.
 * m is the model itself (a constant where it is inlined: its flags fold).
 */
static inline void rvb_fdn_block(struct rvb_fdn *f, const struct rvb_fdn_model *m, const struct wvfx_io *io,
                                 float mix, float predelay, float decay, float trail, float hd, float ld)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    const float c = 0.0002267600066261366f;
    float dp = m->predelay != 0.0f ? 0.0f : st_fma(predelay, 0.01f, -f->predelay) * c;         /* VFNMS */
    float dm = st_fma(mix, 0.01f, -f->mix) * c;
    float *p = io->io;
    /* what the first sample sets (the knobs are the block's) */
    if (f->decay != decay) {
        f->decay = decay;
        f->g = decay * (m->full ? 0.5f : 0.707099974155426f);
    }
    if (m->hi_damp == 3 && f->hd != hd) {
        f->hd = hd;
        rvb_fdn_tone_damp(f, hd);
    } else if (m->hi_damp == 1 && f->hd != hd) {
        f->hd = hd;
        rvb_fdn_hi_damp(f, st_fma(hd, -0.830482006072998f, -0.08304820209741592f));
    } else if (m->hi_damp == 2 && (f->ld != ld || f->hd != hd)) {
        f->ld = ld;
        f->hd = hd;
        rvb_fdn_mod_damp(f, ld, hd);
    }
    const int have_x = io->enabled != 0;
    float x[WVFX_MAX_FRAMES], mv[WVFX_MAX_FRAMES], o[4][WVFX_MAX_FRAMES];
    /* the lines live in the SDRAM: ask for the cache lines this block reads and writes now, so their fetches
       overlap (each line's 64 samples are contiguous but for a wrap, which only costs a wasted request) */
    for (int k = 0; k < 4; k++) {
        const float *b = f->line + k * RVB_FDN_LINE;
        int whole = (int)m->len[k], r = f->w[k] >= whole - 1 ? 0 : f->w[k] + 1;
        for (int j = 0; j < n + 8; j += 8)
            __builtin_prefetch(b + r + j);              /* the writes' lines too: slower on the pedal */
    }
    /* 1 */
    for (int i = 0; i < n; i++) {
        float osc = st_fma(1.0f, 0.04643990844488144f, f->osc);
        f->osc = 2048.0f > osc ? osc : osc + -2048.0f;
        if (m->predelay == 0.0f)
            f->predelay += dp;
        f->mix += dm;
        mv[i] = f->mix;
        if (have_x) {
            float xl = p[2 * i], xr = p[2 * i + 1];
            unsigned u = (unsigned)f->osc;
            float t0 = RVB_SINE[u], lfo = st_fma(RVB_SINE[(u + 1) & 0x7ff] - t0, f->osc - __builtin_truncf(f->osc), t0);
            float xm = (xr + xl) * 0.5f;
            int w = f->pw, nw = w + 1;
            f->pre[w] = xm;
            f->pw = w >= RVB_FDN_PRE - 1 ? 0 : nw;
            float d = st_fma(m->depth * -220.5f, lfo, 2205.0f);
            if (m->predelay >= 0.0f)
                d = st_fma(f->predelay, 4410.0f, d);
            int j = nw - (int)d;
            if (j < 0)
                j += RVB_FDN_PRE;
            int older = j < 1 ? j + RVB_FDN_PRE - 1 : j - 1;
            float a = f->pre[j];
            x[i] = st_fma(f->pre[older] - a, d - __builtin_truncf(d), a + xm) * 0.15000000596046448f;
        }
    }
    /* 2 */
    for (int k = 0; k < 4; k++) {
        float len = m->len[k], frac = len - (float)(int)len, prev = f->prev[k];
        const float *b = f->line + k * RVB_FDN_LINE;
        const int whole = (int)len;
        int r = f->w[k] >= whole - 1 ? 0 : f->w[k] + 1;     /* rvb_fdn_line: a ring of whole samples */
        for (int i = 0; i < n; i++) {
            float a = b[r];
            o[k][i] = st_fma(prev - a, frac, a);
            prev = a;
            r = r >= whole - 1 ? 0 : r + 1;
        }
        f->prev[k] = prev;
        if (!m->damp_sums)
            sos_block(&f->damp_c, &f->damp[k], o[k], n);
    }
    /* 3: line k's input from the outputs one sample before (f->out before the first) */
    {
        float g = f->g, ng = -g, q0 = f->out[0], q1 = f->out[1], q2 = f->out[2], q3 = f->out[3];
        int w[4], wh[4];                        /* each line a ring of its whole length (rvb_fdn_line) */
        for (int k = 0; k < 4; k++) {
            w[k] = f->w[k];
            wh[k] = (int)m->len[k];
        }
        for (int i = 0; i < n; i++) {
            float in[4];
            if (m->full) {
                static const signed char S[4][4] = { { 1, 1, 1, 1 }, { 1, -1, 1, -1 }, { 1, 1, -1, -1 },
                                                     { 1, -1, -1, 1 } };
                const float q[4] = { q0, q1, q2, q3 };
                for (int k = 0; k < 4; k++) {
                    float y = have_x ? st_fma(q[0], g, x[i]) : q[0] * g;
                    for (int j = 1; j < 4; j++)
                        y = st_fma(q[j], S[k][j] > 0 ? g : ng, y);
                    in[k] = y;
                }
            } else if (have_x) {
                in[0] = st_fma(q2, g, st_fma(q1, g, x[i]));
                in[1] = st_fma(ng, q3, st_fma(ng, q0, x[i]));
                in[2] = st_fma(q3, ng, st_fma(g, q0, x[i]));
                in[3] = st_fma(ng, q2, st_fma(g, q1, x[i]));
            } else {
                in[0] = st_fma(q2, g, q1 * g);
                in[1] = st_fma(ng, q3, ng * q0);
                in[2] = st_fma(q3, ng, g * q0);
                in[3] = st_fma(ng, q2, g * q1);
            }
            for (int k = 0; k < 4; k++) {
                f->line[k * RVB_FDN_LINE + w[k]] = rvb_fdn_clip(in[k]);
                w[k] = w[k] >= wh[k] - 1 ? 0 : w[k] + 1;
            }
            q0 = o[0][i];
            q1 = o[1][i];
            q2 = o[2][i];
            q3 = o[3][i];
        }
        for (int k = 0; k < 4; k++)
            f->w[k] = w[k];
        f->out[0] = q0;
        f->out[1] = q1;
        f->out[2] = q2;
        f->out[3] = q3;
    }
    /* 4 */
    float sl[WVFX_MAX_FRAMES], sr[WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++) {
        sl[i] = (o[2][i] + o[0][i]) * m->l_gain;
        sr[i] = o[3][i] + o[1][i];
    }
    if (m->damp_sums) {
        sos_block(&f->damp_c, &f->damp[0], sl, n);
        sos_block(&f->damp_c, &f->damp[1], sr, n);
    }
    sos_block_div(&m->lowpass, &f->lp[0], sl, n);
    sos_block_div(&m->lowpass, &f->lp[1], sr, n);
    const int trail_on = (int)trail == 1 && !io->enabled;
    for (int i = 0; i < n; i++) {
        float xl = p[2 * i], xr = p[2 * i + 1];
        if (mv[i] < 0.5f) {
            float mm = mv[i] + mv[i];
            p[2 * i] = st_fma(mm, sl[i], xl);
            p[2 * i + 1] = st_fma(mm, sr[i], xr);
        } else if (trail_on) {
            p[2 * i] = sl[i] + xl;
            p[2 * i + 1] = sr[i] + xr;
        } else {
            float mm = 1.0f - mv[i];
            p[2 * i] = st_fma(xl + xl, mm, sl[i]);
            p[2 * i + 1] = st_fma(xr + xr, mm, sr[i]);
        }
    }
    if (io->enabled)
        f->cleared = 0;
}

/* m: the model (the same as f->m; given as a constant, its flags fold) */
static inline void rvb_fdn_process(struct rvb_fdn *f, const struct rvb_fdn_model *m, const struct wvfx_io *io,
                                   float mix, float predelay, float decay, float trail, float hd, float ld)
{
    if (io->frames == 0)
        return;
    if (io->enabled || trail == 1.0f) {
        rvb_fdn_block(f, m, io, mix, predelay, decay, trail, hd, ld);
        return;
    }
    const float c = 0.0002267600066261366f;
    float dp = f->m->predelay != 0.0f ? 0.0f : st_fma(predelay, 0.01f, -f->predelay) * c;      /* VFNMS */
    float dm = st_fma(mix, 0.01f, -f->mix) * c;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        if (f->decay != decay) {
            f->decay = decay;
            f->g = decay * (f->m->full ? 0.5f : 0.707099974155426f);
        }
        if (f->m->hi_damp == 3 && f->hd != hd) {
            f->hd = hd;
            rvb_fdn_tone_damp(f, hd);
        } else if (f->m->hi_damp == 1 && f->hd != hd) {
            f->hd = hd;
            rvb_fdn_hi_damp(f, st_fma(hd, -0.830482006072998f, -0.08304820209741592f));
        } else if (f->m->hi_damp == 2 && (f->ld != ld || f->hd != hd)) {
            f->ld = ld;
            f->hd = hd;
            rvb_fdn_mod_damp(f, ld, hd);
        }
        float osc = st_fma(1.0f, 0.04643990844488144f, f->osc);
        f->osc = 2048.0f > osc ? osc : osc + -2048.0f;
        if (f->m->predelay == 0.0f)
            f->predelay += dp;
        f->mix += dm;
        float xl = p[0], xr = p[1];
        if (io->enabled) {
            unsigned u = (unsigned)f->osc;
            float t0 = RVB_SINE[u], lfo = st_fma(RVB_SINE[(u + 1) & 0x7ff] - t0, f->osc - __builtin_truncf(f->osc), t0);
            float xm = (xr + xl) * 0.5f;
            int w = f->pw, nw = w + 1;
            f->pre[w] = xm;
            f->pw = w >= RVB_FDN_PRE - 1 ? 0 : nw;
            float d = st_fma(f->m->depth * -220.5f, lfo, 2205.0f);
            if (f->m->predelay >= 0.0f)
                d = st_fma(f->predelay, 4410.0f, d);
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
                rvb_fdn_clear(f);
            continue;
        }
        float *o = f->out;
        float sl = (o[2] + o[0]) * f->m->l_gain, sr = o[3] + o[1];
        if (f->m->damp_sums) {
            sl = sos_run(&f->damp_c, &f->damp[0], sl);
            sr = sos_run(&f->damp_c, &f->damp[1], sr);
        }
        float yl = sos_run_div(&f->m->lowpass, &f->lp[0], sl);
        float yr = sos_run_div(&f->m->lowpass, &f->lp[1], sr);
        if (f->mix < 0.5f) {
            float m = f->mix + f->mix;
            p[0] = st_fma(m, yl, xl);
            p[1] = st_fma(m, yr, xr);
        } else if ((int)trail == 1 && !io->enabled) {
            p[0] = yl + xl;
            p[1] = yr + xr;
        } else {
            float m = 1.0f - f->mix;
            p[0] = st_fma(xl + xl, m, yl);
            p[1] = st_fma(xr + xr, m, yr);
        }
        if (io->enabled)
            f->cleared = 0;
    }
}
