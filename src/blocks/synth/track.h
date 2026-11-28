/*
 * The Synth's pitch tracker (docs/synth.md): the pitch of the guitar, monophonic, with a latency proportional to
 * the period (about 3 periods: ~5 ms on the high strings, ~20-30 ms on a low E).
 *
 * The input (44.1 kHz) through a 40 Hz high pass and a 1.2 kHz 4th-order Butterworth low pass, one sample in four
 * kept (11025 Hz), as integers (2^20 = full scale). For every lag t of 1..TRK_TMAX+1 the squared difference
 *   d(t) = sum over the last W(t) samples of (x[j] - x[j - t])^2,   W(t) = max(2 t, TRK_WMIN)
 * is kept up to date sample by sample (one term in, one out: integer sums, exact, no drift), and its energy
 *   m(t) = sum of x[j]^2 + x[j - t]^2 over the same samples
 * comes from a running energy sum. Every block, the normalised function n(t) = 1 - d(t) / m(t) (McLeod's NSDF,
 * 1 = periodic at t) is searched: after the lobe around t = 0, its local maxima; the first one at least
 * TRK_PICK of the highest is the period, refined by a parabola (a short period measured again over the whole
 * periods that fit in TRK_FINE samples, where the parabola is that many times finer). A window as long as two
 * periods of the lag itself is what keeps the latency proportional: a high note needs a short window, a low one a
 * long one.
 *
 * Measured on DI recordings against an offline reference (two methods agreeing): 96.6-99.8 % of the blocks on the
 * right semitone, 0-0.9 % octave errors, the rest note changes the tracker has not seen yet.
 */
#pragma once

#include "../modfx/modfx.h"

#define TRK_DEC   4                             /* 44.1 kHz / 4 = 11025 Hz */
#define TRK_RATE  (MODFX_FS / TRK_DEC)
#define TRK_TMIN  7                             /* 1575 Hz */
#define TRK_TMAX  180                           /* 61.3 Hz: a low B on a baritone is out, drop C is in */
#define TRK_WMIN  40                            /* 3.6 ms: the shortest window */
#define TRK_PICK  0.88f                         /* the first maximum this close to the highest */
#define TRK_VOICED 0.5f                         /* below this the highest maximum is not a pitch */
#define TRK_RING  1024                          /* > 3 TRK_TMAX + 3: the oldest sample a sum needs */
#define TRK_SCALE 1048576.0f                    /* 2^20 */
#define TRK_LEVEL_N 160                         /* the level: RMS of the last 14.5 ms */
#define TRK_FINE  32.0f                         /* periods shorter than half this are measured again over
                                                   whole periods up to this lag */

static inline float absf_trk(float x)
{
    return x < 0.0f ? -x : x;
}

struct trk {
    struct bq hp, lp1, lp2;
    float zhp[2], zlp1[2], zlp2[2];
    uint32_t phase;                             /* input samples since the last kept one */
    uint32_t n;                                 /* kept samples */
    int32_t x[2 * TRK_RING];                    /* the kept samples, twice (x[i] = x[i + TRK_RING]): reads
                                                   back from any point without wrapping */
    uint64_t e[TRK_RING];                       /* e[i & mask] = sum of x^2 of the samples before i (wraps) */
    int64_t d[TRK_TMAX + 2];
    /* the last estimate */
    float hz;                                   /* 0 = no pitch */
    float clarity;                              /* the chosen maximum of n(t), 0..1 */
    float level;                                /* RMS of the filtered input, full scale 1 */
};

static inline void trk_init(struct trk *t)
{
    for (unsigned i = 0; i < sizeof *t; i++)
        ((uint8_t *)t)[i] = 0;
    t->hp = bq_rbj(1, 40.0f, 0.70710678f, MODFX_FS);
    t->lp1 = bq_lp(1200.0f, 0.54119610f, MODFX_FS);
    t->lp2 = bq_lp(1200.0f, 1.30656296f, MODFX_FS);
}

/* a uint64 as a float without the runtime's conversion call */
static inline float trk_u2f(uint64_t v)
{
    return (float)(uint32_t)(v >> 32) * 4294967296.0f + (float)(uint32_t)v;
}

/* one kept sample into the ring and the energy sum */
static inline void trk_push(struct trk *t, float v)
{
    const uint32_t mask = TRK_RING - 1, i = t->n & mask;
    v *= TRK_SCALE;
    v = wvfx_clamp(v, -(TRK_SCALE - 1.0f), TRK_SCALE - 1.0f);
    const int32_t xn = (int32_t)v;
    t->x[i] = xn;
    t->x[i + TRK_RING] = xn;
    t->e[(t->n + 1) & mask] = t->e[i] + (uint64_t)((int64_t)xn * xn);
    t->n++;
}

/* the difference sums moved by the last `count` kept samples, one term in and one out each:
   d(t) += a^2 - b^2 = (a - b)(a + b), a the new term, b the one leaving the window. Lag by lag, the sum in
   registers over the new samples (one load and store of each sum per block) */
static inline void trk_update(struct trk *t, uint32_t count)
{
    const uint32_t mask = TRK_RING - 1;
    uint32_t first = t->n - count;
    int64_t *d = t->d;
    while (count) {
        /* the new samples in one piece of the mirrored ring (it ends at its top: at most two pieces) */
        const uint32_t i0 = first & mask, run = TRK_RING - i0 < count ? TRK_RING - i0 : count;
        const int32_t *h = &t->x[i0 + TRK_RING];                         /* h[j - m] = m samples before j */
        for (int k = 1; k <= TRK_TMAX + 1; k++) {
            const int w = 2 * k > TRK_WMIN ? 2 * k : TRK_WMIN;
            const int32_t *pk = h - k, *pw = h - w, *pwk = h - w - k;
            int64_t acc = d[k];
            for (uint32_t j = 0; j < run; j++) {
                const int32_t a = h[j] - pk[j], b = pw[j] - pwk[j];
                acc += (int64_t)(a - b) * (a + b);
            }
            d[k] = acc;
        }
        first += run;
        count -= run;
    }
}

/* the energy of the w samples ending `back` samples before the newest one (inclusive) */
static inline float trk_energy(const struct trk *t, uint32_t back, uint32_t w)
{
    const uint32_t mask = TRK_RING - 1, end = t->n - back;
    return trk_u2f(t->e[end & mask] - t->e[(end - w) & mask]);
}

/* the frames of a block (interleaved stereo, the two channels' mean): filter, keep one in four */
static inline void trk_feed(struct trk *t, const float *in, int frames)
{
    const uint32_t n0 = t->n;
    for (int f = 0; f < frames; f++) {
        float v = 0.5f * (in[2 * f] + in[2 * f + 1]);
        v = bq_run(&t->hp, t->zhp, v);
        v = bq_run(&t->lp1, t->zlp1, v);
        v = bq_run(&t->lp2, t->zlp2, v);
        if (++t->phase == TRK_DEC) {
            t->phase = 0;
            trk_push(t, v);
        }
    }
    trk_update(t, t->n - n0);
}

/* the estimate from the samples so far (hz 0 = no pitch) */
static inline void trk_estimate(struct trk *t)
{
    float nf[TRK_TMAX + 2];
    t->hz = 0.0f;
    t->clarity = 0.0f;
    t->level = 0.0f;
    float lv = trk_energy(t, 0, TRK_LEVEL_N) * (1.0f / (TRK_LEVEL_N * TRK_SCALE * TRK_SCALE));
    if (lv > 0.0f) {                            /* sqrt by the exponent and two Newton steps */
        union { float f; uint32_t u; } g = { lv };
        g.u = (g.u >> 1) + 0x1fc00000u;
        float r = g.f;
        r = 0.5f * (r + lv / r);
        r = 0.5f * (r + lv / r);
        t->level = r;
    }
    if (t->n < 3 * TRK_TMAX + 3)
        return;
    const uint32_t mask = TRK_RING - 1, n = t->n;
    const uint64_t *e = t->e;
    for (int k = 1; k <= TRK_TMAX + 1; k++) {
        const uint32_t w = (uint32_t)(2 * k > TRK_WMIN ? 2 * k : TRK_WMIN);
        /* m(t) as an integer, then d and m shifted alike to 30 bits: one conversion each (0 <= d <= 2 m) */
        const uint64_t m = (e[n & mask] - e[(n - w) & mask]) + (e[(n - k) & mask] - e[(n - k - w) & mask]);
        const uint32_t hi = (uint32_t)(m >> 32), lo = (uint32_t)m;
        if (!hi && !lo) {
            nf[k] = 0.0f;
            continue;
        }
        const int msb = hi ? 63 - __builtin_clz(hi) : 31 - __builtin_clz(lo);
        const int sh = msb > 29 ? msb - 29 : 0;
        const uint64_t dk = (uint64_t)t->d[k];
        uint32_t ms, ds;
        if (sh >= 32) {
            ms = (uint32_t)(m >> 32) >> (sh - 32);
            ds = (uint32_t)(dk >> 32) >> (sh - 32);
        } else if (sh > 0) {
            ms = (lo >> sh) | (hi << (32 - sh));
            ds = ((uint32_t)dk >> sh) | ((uint32_t)(dk >> 32) << (32 - sh));
        } else {
            ms = lo;
            ds = (uint32_t)dk;
        }
        nf[k] = 1.0f - (float)ds / (float)ms;
    }
    nf[0] = 1.0f;
    int z = 1;                                  /* past the lobe around 0 */
    while (z <= TRK_TMAX && nf[z] > 0.0f)
        z++;
    if (z > TRK_TMAX)
        return;
    float top = 0.0f;
    int lo = z > TRK_TMIN ? z : TRK_TMIN;
    for (int k = lo; k <= TRK_TMAX; k++)
        if (nf[k] > 0.0f && nf[k] > nf[k - 1] && nf[k] >= nf[k + 1] && nf[k] > top)
            top = nf[k];
    if (top < TRK_VOICED)
        return;
    int p = lo;
    for (int k = lo; k <= TRK_TMAX; k++)
        if (nf[k] > 0.0f && nf[k] > nf[k - 1] && nf[k] >= nf[k + 1] && nf[k] >= TRK_PICK * top) {
            p = k;
            break;
        }
    const float a = nf[p - 1], b = nf[p], c = nf[p + 1], den = a - 2.0f * b + c;
    const float frac = den < 0.0f ? wvfx_clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
    float period = (float)p + frac;
    /* a short period (a high note: few samples a period, the parabola too coarse) measured again over the whole
       periods that fit in TRK_FINE samples, where the fit is that many times finer */
    if (period < TRK_FINE / 2) {
        const int m = (int)(TRK_FINE / period);
        int q = (int)(m * period + 0.5f);
        if (nf[q - 1] > nf[q])
            q--;
        else if (nf[q + 1] > nf[q])
            q++;
        const float a2 = nf[q - 1], b2 = nf[q], c2 = nf[q + 1], den2 = a2 - 2.0f * b2 + c2;
        if (den2 < 0.0f && b2 >= a2 && b2 >= c2 && b2 >= TRK_PICK * b) {
            const float p2 = ((float)q + wvfx_clamp(0.5f * (a2 - c2) / den2, -0.5f, 0.5f)) / (float)m;
            if (absf_trk(p2 - period) < 0.5f)
                period = p2;
        }
    }
    t->hz = TRK_RATE / period;
    t->clarity = wvfx_clamp(b - 0.25f * (a - c) * frac, 0.0f, 1.0f);
}
