/*
 * Shared pieces of the blocks ported from the original's own effects (docs/stock_port.md), written to give the
 * original's output bit for bit:
 *
 *  - the arithmetic the original's compiler emitted: a x b + c fused (VFMA) wherever its source had a product
 *    added to something, in single and double precision; the C library it linked (expf, exp2f) rounds the product
 *    first (VMLA), which plain C does with -ffp-contract=off;
 *  - the knob glide most of the original's effects use (0.0001 per sample towards the knob);
 *  - the original's potentiometer tapers (stock_tapers.h, 256 points, the knob 0..100 times 2.55).
 */
#pragma once
#include <stdint.h>
#include "sdk/wvfx.h"
#include "stock_tapers.h"

/* a x b + c rounded once: VFMA on the M7, as an operation the compiler knows (it schedules it and loads into it,
   which an asm statement does not let it do); __builtin_fmaf would be a call to fmaf under -fmath-errno */
static inline float st_fma(float a, float b, float c)
{
#if defined(__clang__)
    return __builtin_elementwise_fma(a, b, c);
#else
    return __builtin_fmaf(a, b, c);
#endif
}

static inline double st_fmad(double a, double b, double c)
{
#if defined(__clang__)
    return __builtin_elementwise_fma(a, b, c);
#else
    return __builtin_fma(a, b, c);
#endif
}

/* c - a x b rounded once (VFMS) */
static inline float st_fms(float a, float b, float c)
{
    return st_fma(-a, b, c);
}

/* sqrt rounded once (VSQRT; no library call, no errno) */
static inline float st_sqrt(float x)
{
#if defined(__arm__)
    __asm__("vsqrt.f32 %0, %1" : "=t"(x) : "t"(x));
    return x;
#else
    return __builtin_sqrtf(x);
#endif
}

static inline float st_abs(float x)
{
    return __builtin_fabsf(x);
}

static inline float st_bits(uint32_t u)
{
    union { uint32_t u; float f; } v = { u };
    return v.f;
}

static inline uint32_t st_word(float f)
{
    union { float f; uint32_t u; } v = { f };
    return v.u;
}

/* a knob through one of the original's tapers: the table's point (int)(knob x 2.55) */
static inline float st_taper(const float *tab, float knob)
{
    int i = (int)(knob * 2.55f);
    return tab[i < 0 ? 0 : i > 255 ? 255 : i];
}

/* one step of the original's knob glide: `step` towards the target, landing on it */
static inline float st_glide_by(float cur, float target, float step)
{
    if (cur == target)
        return target;
    if (target > cur) {
        float v = cur + step;
        return v >= target ? target : v;
    }
    float v = cur + -step;
    return v <= target ? target : v;
}

/* the usual one: 0.0001 per sample */
static inline float st_glide(float cur, float target)
{
    return st_glide_by(cur, target, 0.0001f);
}

/* ---------------------------------------------------------------- the original's C library (armcc) */

/* 2^(i/4), i = 0..3: rounded to 12 bits (hi), the rest (lo), and in full */
static const float st_exp_hi[4] = { 1.0f, 1.18896484375f, 1.4140625f, 1.681640625f };
static const float st_exp_lo[4] = { 0.0f, 2.42271257e-4f, 1.51062370e-4f, 1.52205510e-4f };
static const float st_exp_full[4] = { 1.0f, 1.18920708f, 1.41421354f, 1.68179286f };

/* 2^j as a float's bits (j within the normal exponents) */
static inline float st_pow2i(int j)
{
    return st_bits((uint32_t)(j + 127) << 23);
}

/* the common end of expf and exp2f: x = (4j + i) / 4 + r (in powers of 2), p = 2^r - 1 from the polynomial;
   2^(i/4) (1 + p) scaled by 2^j, in two halves when 2^j alone would leave the floats */
static inline float st_exp_finish(int k, float p, int halves)
{
    int i = k & 3, j = k >> 2;
    float m = (st_exp_lo[i] + st_exp_full[i] * p) + st_exp_hi[i];
    if (!halves)
        return st_pow2i(j) * m;
    int j1 = j / 2;                                     /* toward zero */
    return st_pow2i(j - j1) * (st_pow2i(j1) * m);
}

/* the classes of a special argument: infinities and NaN, the tiny ones, the ones beyond the floats; 0 = normal,
   1 = in range but needs the two-half scale, 2 = handled (result in *out) */
static inline int st_exp_special(float x, uint32_t lim_normal, uint32_t lim_over, float tiny_result, float *out)
{
    uint32_t u = st_word(x), a2 = u << 1;
    if (a2 + 0x99000000u <= lim_normal)                 /* 2^-24 <= |x| <= the normal limit */
        return 0;
    if ((u & 0x7f800000u) == 0x7f800000u) {             /* inf, NaN */
        *out = u == 0xff800000u ? 0.0f : x + x;
        return 2;
    }
    if (a2 < 0x67000000u) {                             /* |x| < 2^-24 */
        *out = tiny_result;
        return 2;
    }
    if (a2 > lim_over) {                                /* the result over- or underflows */
        *out = (u & 0x80000000u) ? 0.0f : __builtin_inff();
        return 2;
    }
    return 1;
}

/* e^x: k = round(x 4 / ln 2), r = x - k ln 2 / 4 (ln 2 / 4 in two parts), e^r - 1 by a polynomial */
static inline float st_expf(float x)
{
    float out;
    int halves = st_exp_special(x, 0x1e500000u, 0x85a00000u, x + 1.0f, &out);
    if (halves == 2)
        return out;
    int k = (int)(x * 5.77078009f + (st_word(x) & 0x80000000u ? -0.5f : 0.5f));
    float kf = (float)k;
    float r = x - kf * 0.173278809f;
    r = r - kf * 7.98654582e-6f;
    float p = 0.166729242f + r * 0.0416823104f;
    p = 0.49999997f + r * p;
    p = 0.99999994f + r * p;
    return st_exp_finish(k, p * r, halves);
}

/* 2^x: k = round(4 x), r = x - k / 4, 2^r - 1 by a polynomial */
static inline float st_exp2f(float x)
{
    float out;
    int halves = st_exp_special(x, 0x1ef00000u, 0x862c0000u, 1.0f, &out);
    if (halves == 2)
        return out;
    int k = (int)(x * 4.0f + (st_word(x) & 0x80000000u ? -0.5f : 0.5f));
    float r = x - (float)k * 0.25f;
    float p = 0.0555249453f + r * 0.00962174032f;
    p = 0.240226492f + r * p;
    p = 0.693147123f + r * p;
    return st_exp_finish(k, p * r, halves);
}

/* armcc's tanf: below pi/4 an odd polynomial (products rounded), above it Cody-Waite reduction by pi/2 and -1 / t in
   the odd quadrants; tiny arguments return themselves. The huge-argument path (|x| >= 12867.57) is not ported, the
   ports never reach it */
static inline float st_tanf(float x)
{
    uint32_t u = st_word(x);
    int q = 0;
    if ((u << 1) >= 0x7e921fb6u) {
        float k = x * 0.6366197466850281f;
        float r = (u & 0x80000000u) ? (k - 8388608.0f) + 8388608.0f : (k + 8388608.0f) - 8388608.0f;
        q = (int)r & 3;
        x = x - r * 1.5703125f;
        x = x - r * 0.0004837512969970703f;
        x = x - r * 7.549533620476723e-08f;
        x = x - r * 2.5633440682570896e-12f;
    } else if ((u << 1) < 0x73000000u) {
        return x;
    }
    float x2 = x * x;
    float p = 0.0020511772017925978f + x2 * 0.009943421930074692f;
    p = 0.025203008204698563f + x2 * p;
    p = 0.05315177142620087f + x2 * p;
    p = 0.1334274560213089f + x2 * p;
    p = 0.3333294689655304f + x2 * p;
    float t = x + x * (p * x2);
    return (q & 1) ? -1.0f / t : t;
}

/* armcc's sinf and cosf share tanf's reduction (from pi/4: the quadrant q, x reduced by q pi/2; the huge-argument
   path is not ported) and two polynomials, products rounded: the sine's odd one and the cosine's even one */
static inline int st_trig_reduce(float *x)
{
    uint32_t u = st_word(*x);
    float k = *x * 0.6366197466850281f;
    float r = (u & 0x80000000u) ? (k - 8388608.0f) + 8388608.0f : (k + 8388608.0f) - 8388608.0f;
    float y = *x;
    y = y - r * 1.5703125f;
    y = y - r * 0.0004837512969970703f;
    y = y - r * 7.549533620476723e-08f;
    y = y - r * 2.5633440682570896e-12f;
    *x = y;
    return (int)r & 3;
}

static inline float st_sin_poly(float x, float x2)
{
    float p = 0.00833197869360447f + x2 * -0.000194956359337084f;
    p = -0.16666650772094727f + x2 * p;
    return x + x * (p * x2);
}

static inline float st_cos_poly(float x2)
{
    float p = 0.04165629297494888f + x2 * -0.0013597822980955243f;
    p = -0.49999895691871643f + x2 * p;
    return 1.0f + x2 * p;
}

static inline float st_sinf(float x)
{
    uint32_t u = st_word(x);
    int q = 0;
    if ((u << 1) >= 0x7e921fb6u)
        q = st_trig_reduce(&x);
    else if ((u << 1) < 0x73000000u)
        return x;
    float x2 = x * x;
    float r = (q & 1) ? st_cos_poly(x2) : st_sin_poly(x, x2);
    return (q & 2) ? -r : r;
}

/*
 * st_sinf without branches, for calls meant to overlap (an LFO's voices computed side by side): the reduction
 * always (below pi/4 it gives x itself, -0 included: r is +0 and x - 0 r is x), both polynomials, the quadrant's one
 * and its sign picked by integer operations on their bits; a tiny x (below 2^-25) returned as it is. The same bits as
 * st_sinf for every argument.
 */
static inline float st_sinf_nb(float x)
{
    const uint32_t u2 = st_word(x) << 1;
    float y = x;
    const uint32_t q = (uint32_t)st_trig_reduce(&y);
    const float y2 = y * y, s = st_sin_poly(y, y2), c = st_cos_poly(y2);
    const uint32_t odd = 0u - (q & 1u), neg = (q & 2u) << 30;
    const uint32_t r = ((st_word(c) & odd) | (st_word(s) & ~odd)) ^ neg;
    const uint32_t tiny = 0u - (uint32_t)(u2 < 0x73000000u);
    return st_bits((st_word(x) & tiny) | (r & ~tiny));
}

static inline float st_cosf(float x)
{
    uint32_t u = st_word(x);
    int q = 0;
    if ((u << 1) >= 0x7e921fb6u)
        q = st_trig_reduce(&x);
    else if ((u << 1) < 0x73000000u)
        return 1.0f;
    float x2 = x * x;
    if (q & 1) {
        float s = st_sin_poly(x, x2);
        return (q & 2) ? s : -s;
    }
    float c = st_cos_poly(x2);
    return (q & 2) ? -c : c;
}

/* armcc's asinf: one odd polynomial, products rounded; up to 0.5 on x itself, above on sqrt((1 - |x|) / 2) folded
   from pi/2 (split in two). Below 2^-13 it is x; outside [-1, 1] NaN */
static inline float st_asinf(float x)
{
    uint32_t u = st_word(x) << 1;
    if (u < 0x72000000u || u > 0x7f000000u) {
        if (u < 0x72000000u)
            return x;
        return u > 0xff000000u ? x + x : st_bits(0x7fc00000u);   /* a NaN stays one; |x| > 1: NaN */
    }
    float hi, lo, z, s;
    if (u <= 0x7e000000u) {
        hi = 0.0f;
        lo = 0.0f;
        z = x * x;
        s = x;
    } else {
        z = (1.0f - st_abs(x)) * 0.5f;
        float r = st_sqrt(z);
        if (x < 0.0f) {
            hi = -1.5703125f;
            lo = -0.0004838267923332751f;
            r = -r;
        } else {
            hi = 1.5703125f;
            lo = 0.0004838267923332751f;
        }
        s = r * -2.0f;
    }
    float p = 0.026433361694216728f + z * 0.03832830488681793f;
    p = 0.0450202152132988f + z * p;
    p = 0.07498761266469955f + z * p;
    p = 0.16666673123836517f + z * p;
    float y = lo + s;
    y = y + (s * z) * p;
    return y + hi;
}

/* ---------------------------------------------------------------- powf (armcc) */

/* log2 of 1 + i / 16 x 16, in two parts (hi rounded to 12 bits, lo), i = 0..15 */
static const float st_pow_log[32] = {
    0.0f, 0.0f, 1.3984375f, 0.0009679599897935987f, 2.71875f, 5.002307807444595e-05f, 3.96875f,
    -0.0019097848562523723f, 5.15234375f, -0.0014942317502573133f, 6.27734375f, -0.00026498554507270455f,
    7.3515625f, -0.0006566017982549965f, 8.37890625f, -0.001914953114464879f, 9.359375f, 2.5011539037222974e-05f,
    10.30078125f, 0.0009177863830700517f, 11.20703125f, 4.240257567289518e-06f, 12.078125f, 7.503461529267952e-05f,
    12.91796875f, -0.00028999708592891693f, 13.7265625f, 0.0011334220180287957f, 14.51171875f,
    -0.0014692202676087618f, 15.265625f, 0.0015159661415964365f,
};
/* 2^(j / 16): the remainder (lo), in full, rounded to 12 bits (hi) */
static const float st_pow_lo[16] = {
    0.0f, 0.000328469934174791f, 0.00017570141062606126f, 0.00011675975838443264f, 0.00024227125686593354f,
    0.00015859332052059472f, 0.0004528358986135572f, 0.0002516406821087003f, 0.00015106236969586462f,
    0.000263645953964442f, 0.00021863791334908456f, 0.00013876945013180375f, 0.0001522055099485442f,
    0.00039278538315556943f, 2.371140908508096e-05f, 0.00016749890346545726f,
};
static const float st_pow_full[16] = {
    1.0f, 1.0442737340927124f, 1.0905077457427979f, 1.1387885808944702f, 1.1892070770263672f, 1.2418577671051025f,
    1.2968395948410034f, 1.3542555570602417f, 1.4142135381698608f, 1.4768261909484863f, 1.5422108173370361f,
    1.610490322113037f, 1.6817928552627563f, 1.7562521696090698f, 1.8340080976486206f, 1.9152065515518188f,
};
static const float st_pow_hi[16] = {
    1.0f, 1.0439453125f, 1.09033203125f, 1.138671875f, 1.18896484375f, 1.24169921875f, 1.29638671875f,
    1.35400390625f, 1.4140625f, 1.4765625f, 1.5419921875f, 1.6103515625f, 1.681640625f, 1.755859375f,
    1.833984375f, 1.9150390625f,
};

/* x rounded to 12 fraction bits (to nearest, up from the half) */
static inline float st_round12(float x)
{
    return st_bits((st_word(x) + 0x800u) & ~0xfffu);
}

/* x^y as 2^(y log2 x), both in two parts (16 log2 x and its product with y split at 12 bits). Ported for x positive
   and normal and y finite (the effects' use); the original's other special cases (x <= 0, infinities, NaN, a
   result over- or underflowing) are not */
static inline float st_powf(float x, float y)
{
    uint32_t ux = st_word(x), uy = st_word(y), iy = (uy << 1) + 0x2000000u;
    if (iy < 0x40000000u) {                             /* |y| < 2^-65 */
        if (y == 0.0f)
            return 1.0f;
        y = st_bits((uy & 0x80000000u) | 0x1f800000u);
    }
    float hi, lo;
    if (ux + 0xc0801000u < 0x2001u) {                   /* x within 2^-12 of 1 */
        float p = x - 1.0f;
        float a = 0.3333333432674408f - p * 0.25f;
        float b = 0.5f - p * a;
        float q = -((p * p) * b);
        float m = p * 23.0859375f, n = p * -0.0028168456628918648f + q * 23.083120346069336f;
        hi = st_round12(m + n);
        lo = (m - hi) + n;
    } else {
        uint32_t t = (ux + 0x40000u) >> 19;
        int i = (int)(t & 0xfu), e = (int)((t >> 4) & 0xffu) - 0x7f;
        float xm = st_bits(ux - ((uint32_t)e << 23));
        float c = 1.0f + (float)i * 0.0625f;
        float sum = xm + c, dif = xm - c;
        float a = st_round12(sum), b = (c - a) + xm, r = 1.0f / sum;
        float qh = st_round12(dif * r);
        float rem = dif - qh * a;
        rem = rem - qh * b;
        float ql = rem * r, u = qh + ql, u2 = u * u;
        float poly = (0.3333333432674408f + u2 * 0.20008274912834167f) * u2;
        float s4 = qh * 1.0f, s2 = ql * 1.0f + poly * u;
        float h = st_round12(s4 + s2), l = (s4 - h) + s2;
        float m = h * 46.171875f, n = h * -0.0056336913257837296f + l * 46.16624069213867f;
        float h2 = st_round12(m + n), l2 = (m - h2) + n;
        float th = st_pow_log[2 * i], tl = st_pow_log[2 * i + 1], fe = (float)(e * 16);
        hi = st_round12((((l2 + tl) + h2) + th) + fe);
        lo = l2 - ((((hi - fe) - th) - h2) - tl);
    }
    float yh = st_round12(y), yl = y - yh, zh, zl = 0.0f, prod = hi * yh;
    uint32_t ap = st_word(prod) & 0x7fffffffu;
    if (ap > 0x45800000u && !(0xff000000u < (ap << 1))) {
        zh = st_bits((st_word(prod) & 0x80000000u) | 0x45800000u);        /* +-4096: the result saturates */
    } else {
        float t1 = hi * yl;
        t1 = t1 + lo * (yh + yl);
        zh = st_round12(prod + t1);
        zl = (prod - zh) + t1;
    }
    float z = zh + zl;
    int n = (int)((z < 0.0f ? -0.5f : 0.5f) + z);
    float k = (float)(n >> 4), r = (zh - (float)n) + zl;
    float p = 0.0009384123259223998f + r * 1.3551205483963713e-05f;
    p = 0.043321698904037476f + r * p;
    float s = p * r;
    int j = n & 15;
    float m = st_pow_lo[j] + st_pow_full[j] * s;
    m = m + st_pow_hi[j];
    if (st_abs(k) < 126.0f)
        return st_bits((uint32_t)((k + 127.0f) * 8388608.0f)) * (1.0f * m);
    int ik = (int)k, h = ik / 2;
    float m1 = st_bits(((uint32_t)h << 23) + 0x3f800000u) * (1.0f * m);
    return m1 * st_bits(((uint32_t)(ik - h) << 23) + 0x3f800000u);
}

/* ---------------------------------------------------------------- log10f (armcc) */

/* log10 of 1 + i / 8 in two parts, i = 0..7 */
static const float st_log10_hi[8] = {
    0.0f, 0.0511474609375f, 0.096893310546875f, 0.13824462890625f, 0.17608642578125f, 0.2108154296875f,
    0.24298095703125f, 0.27294921875f,
};
static const float st_log10_lo[8] = {
    0.0f, 5.061509909864981e-06f, 1.670246092544403e-05f, 5.806926128570922e-05f, 4.833274488191819e-06f,
    3.7935627915430814e-05f, 5.709165634470992e-05f, 5.20533139933832e-05f,
};

/* log10 x = n log10 2 + log10(1 + i / 8) + 2 atanh(u) / ln 10, u = (m - c) / (m + c), c = 1 + i / 8 the nearest
   eighth of the mantissa m; near 1 a polynomial. Ported for x positive and normal (the effects' use); the original's
   other cases (0, negative, denormals, infinities, NaN) are not */
static inline float st_log10f(float x)
{
    uint32_t ux = st_word(x), t = ux + 0x80000u;
    int e = ((int32_t)t >> 23) - 127, i = (int)((t >> 20) & 7u);
    float c = st_bits(0x3f800000u + ((uint32_t)i << 20)), m = st_bits(ux - ((uint32_t)e << 23));
    if (e == 0 && i == 0) {
        float p = m - 1.0f;
        float a = -0.10863007605075836f + p * 0.08267129957675934f;
        a = 0.14476843178272247f + p * a;
        a = -0.21714723110198975f + p * a;
        float y = (p * p) * a;
        y = y + p * 0.012419481761753559f;
        return y + p * 0.421875f;
    }
    float u = (m - c) / (m + c), u2 = u * u, u3 = u * u2;
    float y = u3 * (0.2895296514034271f + u2 * 0.17383906245231628f);
    float hi = st_log10_hi[i] + (float)e * 0.301025390625f, lo = st_log10_lo[i] + (float)e * 4.605039066518657e-06f;
    y = lo + y;
    y = y + u * 0.8685889840126038f;
    return hi + y;
}