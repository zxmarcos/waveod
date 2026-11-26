/*
 * What waveod's modulation blocks share (docs/modfx.md): math without libm, one-pole / biquad filters and their
 * bilinear designs, the LFO phase, the 4-point cubic read of a delay line, tempo sync, the soft clip and the
 * equal-power mix. Everything is static inline: a block takes what it uses.
 */
#pragma once

#include "sdk/wvfx.h"

#define MODFX_PI     3.14159265f
#define MODFX_TWO_PI 6.28318531f
#define MODFX_FS     44100.0f

/* ---------------------------------------------------------------- knobs */

/* a named option's index, clamped */
static inline int modfx_option(float v, int n)
{
    int i = (int)(v + 0.5f);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

/* the rate in Hz of a Rate knob: free, or with Sync on a note division (1/1 .. 1/16, model/param_format.c) of the
   tempo */
static inline float modfx_rate(float knob, int sync, uint32_t beat_ms, float lo, float hi)
{
    static const float beats[11] = {
        4.0f, 2.0f, 3.0f, 4.0f / 3.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f,
    };
    if (!sync)
        return wvfx_clamp(knob, lo, hi);
    float ms = (float)(beat_ms & 0xffffu) * beats[modfx_option(knob, 11)];
    return ms > 1.0f ? wvfx_clamp(1000.0f / ms, lo, hi) : lo;
}

/* ---------------------------------------------------------------- math */

/* sin and cos of 2 pi t (|error| < 1e-7 for |t| <= 1/4 after the fold) */
static inline void modfx_sincos(float t, float *sn, float *cs)
{
    t -= (float)(int)t;
    if (t >= 0.5f)
        t -= 1.0f;
    if (t < -0.5f)
        t += 1.0f;
    float sign = 1.0f;
    if (t > 0.25f) {
        t = 0.5f - t;
        sign = -1.0f;
    } else if (t < -0.25f) {
        t = -0.5f - t;
        sign = -1.0f;
    }
    float x = MODFX_TWO_PI * t, x2 = x * x;
    *sn = x * (1.0f - x2 * (1.0f / 6 - x2 * (1.0f / 120 - x2 * (1.0f / 5040 - x2 * (1.0f / 362880 - x2 * (1.0f / 39916800))))));
    *cs = sign * (1.0f - x2 * (0.5f - x2 * (1.0f / 24 - x2 * (1.0f / 720 - x2 * (1.0f / 40320 - x2 * (1.0f / 3628800))))));
}

/* tan (pi f / fs), the bilinear prewarp, f < fs / 2 */
static inline float modfx_prewarp(float f, float fs)
{
    float sn, cs;
    modfx_sincos(0.5f * f / fs, &sn, &cs);
    return sn / cs;
}

/* 2^x, relative error < 1e-8 on the fraction's polynomial (wvfx_exp2's Taylor terms leave 8.5e-5, too much for a
   coefficient that feeds back, such as an envelope's) */
static inline float modfx_exp2(float x)
{
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    int i = (int)x;
    if ((float)i > x)
        i--;
    float f = x - (float)i;
    float p = 1.0f + f * (0.69314682f + f * (0.24023110f + f * (0.055478912f + f * (0.0096861878f + f * (0.0012382410f + f * 0.00021871254f)))));
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return p * v.f;
}

/* x^y for x > 0 */
static inline float modfx_pow(float x, float y)
{
    return modfx_exp2(y * wvfx_log2(x));
}

/* ---------------------------------------------------------------- filters */

/* one-pole y = a x + s, s = c y + b x */
struct op { float a, b, c; };
/* biquad y = b0 x + s1, s1 = b1 x + s2 + a1 y, s2 = b2 x + a2 y (a1, a2 with the sign that adds) */
struct bq { float b0, b1, b2, a1, a2; };

static const struct op op_through = { 1.0f, 0.0f, 0.0f };
static const struct bq bq_through = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static inline struct op op_hp(float f, float fs)
{
    float k = modfx_prewarp(f, fs), a = 1.0f / (1.0f + k);
    return (struct op){ a, -a, (1.0f - k) * a };
}

static inline struct op op_lp(float f, float fs)
{
    float k = modfx_prewarp(f, fs), a = 1.0f / (1.0f + k);
    return (struct op){ k * a, k * a, (1.0f - k) * a };
}

/* (1 + s / wz) / (1 + s / wp) */
static inline struct op op_shelf(float fz, float fp, float fs)
{
    float k = modfx_prewarp(fz, fs), kp = modfx_prewarp(fp, fs), g = kp / k, n = 1.0f / (kp + 1.0f);
    return (struct op){ g * (k + 1.0f) * n, g * (k - 1.0f) * n, (1.0f - kp) * n };
}

/* a one-pole as a biquad section */
static inline struct bq bq_of_op(struct op o)
{
    return (struct bq){ o.a, o.b, 0.0f, o.c, 0.0f };
}

/* 2nd-order bilinear low-pass of quality q (the sections of a Butterworth) */
static inline struct bq bq_lp(float f, float q, float fs)
{
    float k = modfx_prewarp(f, fs), kk = k * k, n = 1.0f / (1.0f + k / q + kk);
    return (struct bq){ kk * n, 2.0f * kk * n, kk * n, -2.0f * (kk - 1.0f) * n, -(1.0f - k / q + kk) * n };
}

/* the audio cookbook's low-pass / high-pass (alpha = sin w / 2Q) */
static inline struct bq bq_rbj(int hp, float f, float q, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    float al = sn / (2.0f * q), n = 1.0f / (1.0f + al);
    float b0 = hp ? 0.5f * (1.0f + cs) : 0.5f * (1.0f - cs), b1 = hp ? -(1.0f + cs) : 1.0f - cs;
    return (struct bq){ b0 * n, b1 * n, b0 * n, 2.0f * cs * n, -(1.0f - al) * n };
}

static inline float op_run(const struct op *f, float *s, float x)
{
    float y = x * f->a + *s;
    *s = y * f->c + x * f->b;
    return y;
}

static inline float bq_run(const struct bq *f, float *s, float x)
{
    float y = f->b0 * x + s[0];
    s[0] = (f->b1 * x + s[1]) + f->a1 * y;
    s[1] = f->b2 * x + f->a2 * y;
    return y;
}

/* over a block, in place, the state in registers */
static inline void op_block(const struct op *f, float *state, float *x, int n)
{
    const float a = f->a, b = f->b, c = f->c;
    float s = *state;
    for (int i = 0; i < n; i++) {
        float y = x[i] * a + s;
        s = y * c + x[i] * b;
        x[i] = y;
    }
    *state = s;
}

static inline void bq_block(const struct bq *f, float *state, float *x, int n)
{
    const float b0 = f->b0, b1 = f->b1, b2 = f->b2, a1 = f->a1, a2 = f->a2;
    float s1 = state[0], s2 = state[1];
    for (int i = 0; i < n; i++) {
        float y = b0 * x[i] + s1;
        s1 = (b1 * x[i] + s2) + a1 * y;
        s2 = b2 * x[i] + a2 * y;
        x[i] = y;
    }
    state[0] = s1;
    state[1] = s2;
}

/* ---------------------------------------------------------------- LFO */

/* one phase step, wrapped at 2 pi (a float sum frame by frame) */
static inline float modfx_advance(float ph, float inc)
{
    ph = inc + ph;
    if (ph >= MODFX_TWO_PI)
        ph = (ph - MODFX_TWO_PI) + 1.7484555e-7f;   /* the float 2 pi is that much above 2 pi */
    return ph;
}

/* -cos (a) and sin (b) of the phase after each of n steps, by a rotation from the block's first; the new phase */
static inline float modfx_rotate(float ph, float inc, int n, float *a, float *b)
{
    float sn, cs, rs, rc;
    modfx_sincos(modfx_advance(ph, inc) * (1.0f / MODFX_TWO_PI), &sn, &cs);
    modfx_sincos(inc * (1.0f / MODFX_TWO_PI), &rs, &rc);
    for (int i = 0; i < n; i++) {
        a[i] = -cs;
        b[i] = sn;
        float c2 = cs * rc - sn * rs;
        sn = sn * rc + cs * rs;
        cs = c2;
        ph = modfx_advance(ph, inc);
    }
    return ph;
}

/* the triangle of a phase p in turns, -1 at 0, +1 at 1/2 */
static inline float modfx_tri(float p)
{
    return p < 0.5f ? -1.0f + 4.0f * p : 3.0f - 4.0f * p;
}

/* ---------------------------------------------------------------- delay line */

/*
 * A line of a power-of-two length with its first 3 frames mirrored past the end (write them with
 * modfx_mirror after the block's writes): the four taps of a read are contiguous, one address and four loads
 * (the VFP has no register-offset load).
 */
#define MODFX_MIRROR 3

static inline void modfx_mirror(float *line, uint32_t len)
{
    for (int i = 0; i < MODFX_MIRROR; i++)
        line[len + i] = line[i];
}

/* 4-point, 3rd-order Lagrange read `delay` frames (>= 1) behind frame w */
static inline float modfx_tap(const float *line, uint32_t mask, uint32_t w, float delay)
{
    int n = (int)delay;
    float f = delay - (float)n;
    const float *p = line + ((w - (uint32_t)n - 2u) & mask);
    float x3 = p[0], x2 = p[1], x1 = p[2], x0 = p[3];
    float c1 = x2 - (1.0f / 3.0f) * x0 - 0.5f * x1 - (1.0f / 6.0f) * x3;
    float c2 = 0.5f * (x0 + x2) - x1;
    float c3 = (1.0f / 6.0f) * (x3 - x0) + 0.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + x1;
}

/* the same read on a line of any length len (the first 3 frames mirrored past it), w < len, delay <= len - 4 */
static inline float modfx_tap_wrap(const float *line, uint32_t len, uint32_t w, float delay)
{
    int n = (int)delay;
    float f = delay - (float)n;
    int32_t i = (int32_t)w - n - 2;
    if (i < 0)
        i += (int32_t)len;
    const float *p = line + i;
    float x3 = p[0], x2 = p[1], x1 = p[2], x0 = p[3];
    float c1 = x2 - (1.0f / 3.0f) * x0 - 0.5f * x1 - (1.0f / 6.0f) * x3;
    float c2 = 0.5f * (x0 + x2) - x1;
    float c3 = (1.0f / 6.0f) * (x3 - x0) + 0.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + x1;
}

/* ---------------------------------------------------------------- shared stages */

/* soft clip around a bias, a bucket-brigade's input stage: x g + 0.2, clamped to +-1.5, v - 4/27 v^3, the
   bias's own clip taken out, / g */
static inline float modfx_bbd_clip(float x, float g, float rg)
{
    float v = __builtin_fminf(__builtin_fmaxf(0.2f + x * g, -1.5f), 1.5f);
    v = v - (4.0f / 27.0f) * (v * (v * v));
    return (v - 0.19881482f) * rg;
}

/* equal-power wet / dry of a mix 0..1 */
static inline void modfx_mix(float mix, float *wet, float *dry)
{
    modfx_sincos(0.25f * mix, wet, dry);
}

/* the delay / reverb mix law: the wet's gain over 2 x Mix (0..1), from 0 to full at Mix 50 % (a degree-6 fit) */
static inline float modfx_wet_curve(float x)
{
    return 2.43122421e-05f + x * (0.162042543f + x * (0.258689888f + x * (0.137863031f + x * (0.424911704f +
           x * (-0.221082798f + x * 0.234550510f)))));
}
