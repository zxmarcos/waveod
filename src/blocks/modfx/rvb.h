/*
 * The Plate and Spring blocks' shared pieces (docs/plate.md): delay lines of any length, the front (the input
 * faded in / out, the predelay with its echo and the cuts), the 2x / 4x resamplers, the dispersion (a chain of
 * first-order all-passes), the type table's layout and the output mix.
 */
#pragma once
#include "modfx.h"
#include "rvb_fir.h"

#define RVB_N 16                          /* frames per step */

/* a x b + c, rounded once: VFMA on the module, as a builtin the compiler schedules (__builtin_fmaf would be a call
 * to fmaf under -fmath-errno; an asm statement cannot be scheduled), fmaf on the PC */
static inline float rvb_fma(float a, float b, float c)
{
#if defined(__arm__) && defined(__clang__)
    return __builtin_elementwise_fma(a, b, c);
#elif defined(__arm__)
    __asm__("vfma.f32 %0, %1, %2" : "+t"(c) : "t"(a), "t"(b));
    return c;
#else
    return __builtin_fmaf(a, b, c);
#endif
}

/* a type: flags 1 High Cut 12 dB/oct, 2 the reverb's input from the predelay's output, 4 Low Cut 12 dB/oct; the
 * all-passes per line; Spring: the tank's count and its kind; the line delays at the engine's rate at 48 kHz; the
 * Spring's tone filter as its analog prototype in s / 96000 (n2 n1 n0 d2 d1 d0) */
#define RVB_TYPE_FIELDS                                                                                          \
    uint8_t flags, nst, n, pairs;                                                                                \
    uint16_t delay[16];                                                                                          \
    float r13, p37, p38, eq1f, eq1q, eq1g, eq2f, eq2q, eq2g, lamp, lrate, width, efb, echo, ap, diff, tap, apd, \
        drive, cutq, lcq;                                                                                        \
    float post[6];

/* ---------------------------------------------------------------- lines */

struct rline { float *buf; uint32_t size, w; };

/* y[i] = the frame d before frame i of this step (frame i is at w + i) */
static inline void rl_read(const struct rline *l, uint32_t d, float *y, int n)
{
    int32_t r = (int32_t)l->w - (int32_t)d;
    if (r < 0)
        r += (int32_t)l->size;
    const float *b = l->buf;
    if ((uint32_t)r + (uint32_t)n <= l->size) {
        for (int i = 0; i < n; i++)
            y[i] = b[r + i];
    } else {
        for (int i = 0; i < n; i++) {
            y[i] = b[r];
            if ((uint32_t)++r >= l->size)
                r = 0;
        }
    }
}

/* the same frames in place when they do not wrap, else copied into tmp */
static inline const float *rl_span(const struct rline *l, uint32_t d, float *tmp, int n)
{
    int32_t r = (int32_t)l->w - (int32_t)d;
    if (r < 0)
        r += (int32_t)l->size;
    if ((uint32_t)r + (uint32_t)n <= l->size)
        return l->buf + r;
    rl_read(l, d, tmp, n);
    return tmp;
}

static inline void rl_write(struct rline *l, const float *x, int n)
{
    uint32_t w = l->w;
    float *b = l->buf;
    if (w + (uint32_t)n < l->size) {
        for (int i = 0; i < n; i++)
            b[w + i] = x[i];
        w += (uint32_t)n;
    } else {
        for (int i = 0; i < n; i++) {
            b[w] = x[i];
            if (++w >= l->size)
                w = 0;
        }
    }
    l->w = w;
}

/* ---------------------------------------------------------------- the front */

struct rvb_fdes {
    uint32_t pd;                          /* predelay, frames (>= 16: read before the write) */
    float efb, echo, in_r;
    int from_pd, hicut_in;                /* the reverb's input from the predelay's output; High Cut here */
    struct bq lowcut, hicut;
};

struct rvb_front { float in_g, hold_g, s_lc[2], s_hc[2]; };

/* 16 frames: (L + R) faded towards on / off, x the hold fade -> + echo x the predelay -> Low Cut (High Cut) -> the
 * predelay; x = the reverb's input, e = the echo heard */
static inline void rvb_front(struct rvb_front *f, struct rline *pd, const struct rvb_fdes *d, const float *io, int on,
                             float *x, float *e)
{
    float m[RVB_N];
    const float a = d->in_r, kk = on ? 1.0f - a : 0.0f;
    float g = f->in_g;
    for (int i = 0; i < RVB_N; i++) {
        g = kk + a * g;
        m[i] = (io[2 * i] + io[2 * i + 1]) * g;
    }
    f->in_g = g;
    f->hold_g = 0.1f + 0.9f * f->hold_g;
    for (int i = 0; i < RVB_N; i++)
        m[i] *= f->hold_g;
    rl_read(pd, d->pd, e, RVB_N);
    for (int i = 0; i < RVB_N; i++)
        m[i] = m[i] + e[i] * d->efb;
    bq_block(&d->lowcut, f->s_lc, m, RVB_N);
    if (d->hicut_in)
        bq_block(&d->hicut, f->s_hc, m, RVB_N);
    rl_write(pd, m, RVB_N);
    const float k1 = 1.0f - d->echo;
    const float *src = d->from_pd ? e : m;
    for (int i = 0; i < RVB_N; i++) {
        x[i] = src[i] * k1;
        e[i] = e[i] * d->echo;
    }
}

/* ---------------------------------------------------------------- resampling */

/* by m (2, 4): 16 frames in, 16 / m out; h holds the taps - m frames before the step, then the step's. Output k is
 * the filter over the taps frames ending at frame m (k + 1) - 1, c[0] on the oldest. Each coefficient is loaded
 * once for all the step's outputs */
static inline void rvb_down(float *h, int taps, int m, const float *c, const float *in, float *out)
{
#ifdef RVB_PROF_NODOWN
    for (int k = 0; k < RVB_N / m; k++) out[k] = in[k];
    return;
#endif
    const int hl = taps - m;
    for (int j = 0; j < RVB_N; j++)
        h[hl + j] = in[j];
    if (m == 2) {
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, a4 = 0.0f, a5 = 0.0f, a6 = 0.0f, a7 = 0.0f;
        for (int t = 0; t < taps; t++) {
            const float ct = c[t], *x = h + t;
            a0 = rvb_fma(ct, x[0], a0); a1 = rvb_fma(ct, x[2], a1); a2 = rvb_fma(ct, x[4], a2);
            a3 = rvb_fma(ct, x[6], a3); a4 = rvb_fma(ct, x[8], a4); a5 = rvb_fma(ct, x[10], a5);
            a6 = rvb_fma(ct, x[12], a6); a7 = rvb_fma(ct, x[14], a7);
        }
        out[0] = a0; out[1] = a1; out[2] = a2; out[3] = a3; out[4] = a4; out[5] = a5; out[6] = a6; out[7] = a7;
    } else {
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        for (int t = 0; t < taps; t++) {
            const float ct = c[t], *x = h + t;
            a0 = rvb_fma(ct, x[0], a0); a1 = rvb_fma(ct, x[4], a1);
            a2 = rvb_fma(ct, x[8], a2); a3 = rvb_fma(ct, x[12], a3);
        }
        out[0] = a0; out[1] = a1; out[2] = a2; out[3] = a3;
    }
    for (int j = 0; j < hl; j++)
        h[j] = h[RVB_N + j];
}

/* by m (2, 4): 16 / m frames in, 16 out; h holds the 31 frames before the step, then the step's; phase p of output
 * i is the filter c[m k + p] over the 32 frames ending at frame i. Each coefficient is loaded once for the step */
static inline void rvb_up(float *h, int m, const float *c, const float *in, float *out)
{
#ifdef RVB_PROF_NOUP
    for (int k = 0; k < RVB_N; k++) out[k] = in[k / m];
    return;
#endif
    const int n = RVB_N / m;
    for (int j = 0; j < n; j++)
        h[31 + j] = in[j];
    if (m == 2) {
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, a4 = 0.0f, a5 = 0.0f, a6 = 0.0f, a7 = 0.0f;
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f, b7 = 0.0f;
        for (int k = 0; k < 32; k++) {
            const float c0 = c[2 * k], c1 = c[2 * k + 1], *x = h + k;
            const float x0 = x[0], x1 = x[1], x2 = x[2], x3 = x[3], x4 = x[4], x5 = x[5], x6 = x[6], x7 = x[7];
            a0 = rvb_fma(c0, x0, a0); b0 = rvb_fma(c1, x0, b0); a1 = rvb_fma(c0, x1, a1); b1 = rvb_fma(c1, x1, b1);
            a2 = rvb_fma(c0, x2, a2); b2 = rvb_fma(c1, x2, b2); a3 = rvb_fma(c0, x3, a3); b3 = rvb_fma(c1, x3, b3);
            a4 = rvb_fma(c0, x4, a4); b4 = rvb_fma(c1, x4, b4); a5 = rvb_fma(c0, x5, a5); b5 = rvb_fma(c1, x5, b5);
            a6 = rvb_fma(c0, x6, a6); b6 = rvb_fma(c1, x6, b6); a7 = rvb_fma(c0, x7, a7); b7 = rvb_fma(c1, x7, b7);
        }
        out[0] = a0; out[1] = b0; out[2] = a1; out[3] = b1; out[4] = a2; out[5] = b2; out[6] = a3; out[7] = b3;
        out[8] = a4; out[9] = b4; out[10] = a5; out[11] = b5; out[12] = a6; out[13] = b6; out[14] = a7; out[15] = b7;
    } else {
        float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        float d0 = 0.0f, d1 = 0.0f, d2 = 0.0f, d3 = 0.0f, e0 = 0.0f, e1 = 0.0f, e2 = 0.0f, e3 = 0.0f;
        for (int k = 0; k < 32; k++) {
            const float c0 = c[4 * k], c1 = c[4 * k + 1], c2 = c[4 * k + 2], c3 = c[4 * k + 3], *x = h + k;
            const float x0 = x[0], x1 = x[1], x2 = x[2], x3 = x[3];
            a0 = rvb_fma(c0, x0, a0); a1 = rvb_fma(c1, x0, a1); a2 = rvb_fma(c2, x0, a2); a3 = rvb_fma(c3, x0, a3);
            b0 = rvb_fma(c0, x1, b0); b1 = rvb_fma(c1, x1, b1); b2 = rvb_fma(c2, x1, b2); b3 = rvb_fma(c3, x1, b3);
            d0 = rvb_fma(c0, x2, d0); d1 = rvb_fma(c1, x2, d1); d2 = rvb_fma(c2, x2, d2); d3 = rvb_fma(c3, x2, d3);
            e0 = rvb_fma(c0, x3, e0); e1 = rvb_fma(c1, x3, e1); e2 = rvb_fma(c2, x3, e2); e3 = rvb_fma(c3, x3, e3);
        }
        out[0] = a0; out[1] = a1; out[2] = a2; out[3] = a3; out[4] = b0; out[5] = b1; out[6] = b2; out[7] = b3;
        out[8] = d0; out[9] = d1; out[10] = d2; out[11] = d3; out[12] = e0; out[13] = e1; out[14] = e2; out[15] = e3;
    }
    for (int j = 0; j < 31; j++)
        h[j] = h[n + j];
}
/* ---------------------------------------------------------------- the dispersion */

/* nst first-order all-passes of coefficient g in a row, per frame y = s - g x, s = g y + x; stage by stage, the
 * frames in registers. RVB_P(s, u): s becomes the output y, u the new state, each in one FMA; so after a stage the
 * outputs sit one register over and the state where the last input was: the stages are unrolled over the cycle
 * of that rotation (5 registers for 4 frames), which leaves no moves */
#define RVB_P(s, u)                                                                                              \
    do {                                                                                                         \
        s = rvb_fma(-g, u, s);                                                                                   \
        u = rvb_fma(g, s, u);                                                                                    \
    } while (0)
#define RVB_S4(f, a, b, c, d, J)                                                                                 \
    do {                                                                                                         \
        f = st[J];                                                                                               \
        RVB_P(f, a); RVB_P(a, b); RVB_P(b, c); RVB_P(c, d);                                                      \
        st[J] = d;                                                                                               \
    } while (0)

/* rvb_disperse4 on four lines at once (states s[l], 4 frames y[l]): a stage's FMAs on one line are a chain, each
 * waiting for the one before (about 3 cycles on the M7); four chains interleaved fill those waits, in 20 registers
 * for the frames. Eight frames go through it as two runs of four, a stage's state carrying from frame 3 to frame 4
 * in its slot: the same operations as stage by stage over the eight */
#define RVB_P4(s0, u0, s1, u1, s2, u2, s3, u3)                                                                   \
    do {                                                                                                         \
        s0 = rvb_fma(-g, u0, s0);                                                                                \
        s1 = rvb_fma(-g, u1, s1);                                                                                \
        s2 = rvb_fma(-g, u2, s2);                                                                                \
        s3 = rvb_fma(-g, u3, s3);                                                                                \
        u0 = rvb_fma(g, s0, u0);                                                                                 \
        u1 = rvb_fma(g, s1, u1);                                                                                 \
        u2 = rvb_fma(g, s2, u2);                                                                                 \
        u3 = rvb_fma(g, s3, u3);                                                                                 \
    } while (0)
/* one stage J on the four lines; the rotation (f a b c d) is the same for every line: register names with a
 * suffix per line */
#define RVB_S4X4(f, a, b, c, d, J)                                                                               \
    do {                                                                                                         \
        f##0 = s0[J]; f##1 = s1[J]; f##2 = s2[J]; f##3 = s3[J];                                                 \
        RVB_P4(f##0, a##0, f##1, a##1, f##2, a##2, f##3, a##3);                                                  \
        RVB_P4(a##0, b##0, a##1, b##1, a##2, b##2, a##3, b##3);                                                  \
        RVB_P4(b##0, c##0, b##1, c##1, b##2, c##2, b##3, c##3);                                                  \
        RVB_P4(c##0, d##0, c##1, d##1, c##2, d##2, c##3, d##3);                                                  \
        s0[J] = d##0; s1[J] = d##1; s2[J] = d##2; s3[J] = d##3;                                                  \
    } while (0)

static inline void rvb_disperse4x4(float *s0, float *y0, float *s1, float *y1, float *s2, float *y2, float *s3,
                                   float *y3, int nst, float g)
{
#ifdef RVB_PROF_NODISP
    return;
#endif
    float A0 = y0[0], B0 = y0[1], C0 = y0[2], D0 = y0[3], E0;
    float A1 = y1[0], B1 = y1[1], C1 = y1[2], D1 = y1[3], E1;
    float A2 = y2[0], B2 = y2[1], C2 = y2[2], D2 = y2[3], E2;
    float A3 = y3[0], B3 = y3[1], C3 = y3[2], D3 = y3[3], E3;
    int j = 0;
    for (; j + 5 <= nst; j += 5) {
        RVB_S4X4(E, A, B, C, D, j);
        RVB_S4X4(D, E, A, B, C, j + 1);
        RVB_S4X4(C, D, E, A, B, j + 2);
        RVB_S4X4(B, C, D, E, A, j + 3);
        RVB_S4X4(A, B, C, D, E, j + 4);
    }
    for (; j < nst; j++) {
        RVB_S4X4(E, A, B, C, D, j);
        D0 = C0; C0 = B0; B0 = A0; A0 = E0;
        D1 = C1; C1 = B1; B1 = A1; A1 = E1;
        D2 = C2; C2 = B2; B2 = A2; A2 = E2;
        D3 = C3; C3 = B3; B3 = A3; A3 = E3;
    }
    y0[0] = A0; y0[1] = B0; y0[2] = C0; y0[3] = D0;
    y1[0] = A1; y1[1] = B1; y1[2] = C1; y1[3] = D1;
    y2[0] = A2; y2[1] = B2; y2[2] = C2; y2[3] = D2;
    y3[0] = A3; y3[1] = B3; y3[2] = C3; y3[3] = D3;
}

static inline void rvb_disperse4(float *st, float *y, int nst, float g)
{
    float r0 = y[0], r1 = y[1], r2 = y[2], r3 = y[3], r4;
    int j = 0;
    for (; j + 5 <= nst; j += 5) {
        RVB_S4(r4, r0, r1, r2, r3, j);
        RVB_S4(r3, r4, r0, r1, r2, j + 1);
        RVB_S4(r2, r3, r4, r0, r1, j + 2);
        RVB_S4(r1, r2, r3, r4, r0, j + 3);
        RVB_S4(r0, r1, r2, r3, r4, j + 4);
    }
    for (; j < nst; j++) {
        RVB_S4(r4, r0, r1, r2, r3, j);
        r3 = r2; r2 = r1; r1 = r0; r0 = r4;
    }
    y[0] = r0; y[1] = r1; y[2] = r2; y[3] = r3;
}

/* ---------------------------------------------------------------- design */

/* per line: the damping one-pole (from the decay at the engine's rate fe) and the low shelf (designed at fs) */
struct rvb_damp { float g, p, b0, b1, a1; };

static inline void rvb_fo_shelf(float fz, float fp, float fs, float *b0, float *b1, float *a1)
{
    const float kz = modfx_prewarp(fz, fs), kp = modfx_prewarp(fp, fs), g = 1.0f / (1.0f + kp);
    *b0 = (1.0f + kz) * g;
    *b1 = (kz - 1.0f) * g;
    *a1 = (1.0f - kp) * g;
}

/* returns the low shelf's DC gain (for the tone shelf's mean) */
static inline float rvb_damp_design(struct rvb_damp *d, uint32_t delay, float time, float r13, float p37, float p38,
                                    float fe, float fs)
{
    const float e = -3.0f * (float)delay / (fe * time) * 3.32192809f;     /* log2 of 10^e */
    const float g = modfx_exp2(e), ghf = modfx_exp2(e / r13), glf = modfx_exp2(e / p37);
    float pole = (g - ghf) / (g + ghf);
    if (pole > 0.9999f)
        pole = 0.9999f;
    d->g = g * (1.0f - pole);
    d->p = pole;
    const float r = wvfx_clamp(glf / g, 0.1f, 10.0f), sq = modfx_pow(r, 0.5f);
    rvb_fo_shelf(p38 * sq, p38 / sq, fs, &d->b0, &d->b1, &d->a1);
    return r;
}

/* the damping and the shelf over n frames in place; s[0] damping state, s[1] shelf state */
static inline void rvb_damp_run(const struct rvb_damp *d, float *s, float *y, int n)
{
    const float cg = d->g, cp = d->p, b0 = d->b0, b1 = d->b1, a1 = d->a1;
    float sd = s[0], ss = s[1];
    for (int i = 0; i < n; i++) {
        sd = sd * cp + y[i] * cg;
        const float v = b0 * sd + ss;
        ss = a1 * v + b1 * sd;
        y[i] = v;
    }
    s[0] = sd;
    s[1] = ss;
}

static inline struct bq rvb_peaking(float f, float q, float db, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    const float a = modfx_exp2(db * (0.16609640474f * 0.5f)), al = sn / (2.0f * q), n = 1.0f / (1.0f + al / a);
    return (struct bq){ (1.0f + al * a) * n, -2.0f * cs * n, (1.0f - al * a) * n, 2.0f * cs * n, -(1.0f - al / a) * n };
}

/* 12 dB/oct high-pass with alpha = w / 2q */
static inline struct bq rvb_hp12(float f, float q, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    const float al = MODFX_TWO_PI * f / fs / (2.0f * q), n = 1.0f / (1.0f + al);
    const float b0 = 0.5f * (1.0f + cs);
    return (struct bq){ b0 * n, -(1.0f + cs) * n, b0 * n, 2.0f * cs * n, -(1.0f - al) * n };
}

/* a filter from its analog prototype a (s' = s / 96000: n2 n1 n0 d2 d1 d0), bilinear at 2 fs */
static inline struct bq rvb_from_analog(const float *a, float fs)
{
    const float k = fs / 48000.0f, k2 = k * k;
    float b0, b1, b2, a0, a1, a2;
    if (a[0] == 0.0f && a[3] == 0.0f) {
        b0 = a[1] * k + a[2]; b1 = a[2] - a[1] * k; b2 = 0.0f;
        a0 = a[4] * k + a[5]; a1 = a[5] - a[4] * k; a2 = 0.0f;
    } else {
        b0 = (a[0] * k2 + a[1] * k) + a[2]; b1 = 2.0f * (a[2] - a[0] * k2); b2 = (a[0] * k2 - a[1] * k) + a[2];
        a0 = (a[3] * k2 + a[4] * k) + a[5]; a1 = 2.0f * (a[5] - a[3] * k2); a2 = (a[3] * k2 - a[4] * k) + a[5];
    }
    const float n = 1.0f / a0;
    return (struct bq){ b0 * n, b1 * n, b2 * n, -a1 * n, -a2 * n };
}

/* the front's design: predelay, echo, cuts */
static inline void rvb_front_design(struct rvb_fdes *f, const float *k, int kpd, int klc, int khc, uint8_t flags,
                                    float efb, float echo, float cutq, float lcq, float in_r, int hicut_in, float fs)
{
    const int pd = (int)(fs * (wvfx_clamp(k[kpd], 0.0f, 1000.0f) * 0.001f)) - 16;
    f->pd = (uint32_t)(pd < 0 ? 0 : pd) + 16u;
    f->efb = efb;
    f->echo = echo;
    f->in_r = in_r;
    f->from_pd = (flags & 2) != 0;
    f->hicut_in = hicut_in;
    const float lc = wvfx_clamp(k[klc], 20.0f, 2000.0f), hc = wvfx_clamp(k[khc], 200.0f, 20000.0f);
    f->lowcut = flags & 4 ? rvb_hp12(lc, lcq, fs) : bq_of_op(op_hp(lc, fs));
    f->hicut = flags & 1 ? bq_rbj(0, hc, cutq, fs) : bq_of_op(op_lp(hc, fs));
}

static inline uint32_t rvb_scaled(uint32_t d) { return (uint32_t)((float)d * (44100.0f / 48000.0f) + 0.5f); }
