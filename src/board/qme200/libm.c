/*
 * The math library functions the firmware calls (dsp/fmath.h): biquad and EQ design, the Cab/IR engine's filters, the
 * virtual mic, the spectrum, the latency test. sinf, cosf, expf and logf are musl's (MIT, the FreeBSD-derived single-
 * precision versions: __sindf / __cosdf kernels, __rem_pio2f, the old expf and logf), the same code as the compiler
 * runtime the firmware was first built with, so the results are the same bit for bit; sqrt and fmaf are the FPU's
 * VSQRT.F64 and VFMA.F32 (both correctly rounded). Built without link-time optimisation (CMakeLists.txt), like a
 * library: the compiler may call these from code it generates.
 *
 * Not ported: __rem_pio2_large (sinf / cosf of |x| >= 2^28 pi / 2, ~4.2e8); there the argument is reduced in double
 * precision with the medium-range formula, less accurate. Nothing in waveod gets near it.
 */
#include <stdint.h>

union fbits {
    float f;
    uint32_t u;
};

static inline uint32_t f2u(float f)
{
    union fbits b = { .f = f };
    return b.u;
}

static inline float u2f(uint32_t u)
{
    union fbits b = { .u = u };
    return b.f;
}

double sqrt(double x)
{
    return __builtin_sqrt(x);           /* VSQRT.F64 (this file has -fno-math-errno) */
}

float fmaf(float x, float y, float z)
{
    return __builtin_fmaf(x, y, z);     /* VFMA.F32 */
}

/* ---------------------------------------------------------------- sinf, cosf */

/* |sin(x)/x - s(x)| < 2**-37.5 on [-pi/4, pi/4] */
static float sindf(double x)
{
    static const double S1 = -0x15555554cbac77.0p-55, S2 = 0x111110896efbb2.0p-59,
                        S3 = -0x1a00f9e2cae774.0p-65, S4 = 0x16cd878c3b46a7.0p-71;
    double z = x * x, w = z * z, r = S3 + z * S4, s = z * x;
    return (float)((x + s * (S1 + z * S2)) + s * w * r);
}

/* |cos(x) - c(x)| < 2**-34.1 on [-pi/4, pi/4] */
static float cosdf(double x)
{
    static const double C0 = -0x1ffffffd0c5e81.0p-54, C1 = 0x155553e1053a42.0p-57,
                        C2 = -0x16c087e80f1e27.0p-62, C3 = 0x199342e0ee5069.0p-68;
    double z = x * x, w = z * z, r = C2 + z * C3;
    return (float)(((1.0 + z * C0) + w * C1) + (w * z) * r);
}

/* x rem pi/2 in *y (double), the quadrant returned */
static int rem_pio2f(float x, double *y)
{
    static const double toint = 0x1.8p52, pio4 = 0x1.921fb6p-1,         /* 1.5 / DBL_EPSILON */
                        invpio2 = 6.36619772367581382433e-01,   /* 53 bits of 2/pi */
                        pio2_1 = 1.57079631090164184570e+00,    /* the first 25 bits of pi/2 */
                        pio2_1t = 1.58932547735281966916e-08;   /* pi/2 - pio2_1 */
    uint32_t ix = f2u(x) & 0x7fffffff;
    if (ix >= 0x7f800000) {             /* inf or NaN */
        *y = (double)(x - x);
        return 0;
    }
    /* |x| ~< 2^28 (pi/2): exact enough with 25 + 53 bits of pi; beyond it the same formula (see the top) */
    double fn = (double)x * invpio2 + toint - toint;
    int n = (int)fn;
    *y = (double)x - fn * pio2_1 - fn * pio2_1t;
    if (*y < -pio4) {
        n--;
        fn--;
        *y = (double)x - fn * pio2_1 - fn * pio2_1t;
    } else if (*y > pio4) {
        n++;
        fn++;
        *y = (double)x - fn * pio2_1 - fn * pio2_1t;
    }
    return n;
}

/* small multiples of pi/2 in double precision */
#define PIO2_1 0x1.921fb54442d18p0          /* 0x3FF921FB, 0x54442D18 */
#define PIO2_2 0x1.921fb54442d18p1          /* 0x400921FB, 0x54442D18 */
#define PIO2_3 0x1.2d97c7f3321d2p2          /* 0x4012D97C, 0x7F3321D2 */
#define PIO2_4 0x1.921fb54442d18p2          /* 0x401921FB, 0x54442D18 */

float sinf(float x)
{
    uint32_t ix = f2u(x);
    int sign = ix >> 31;
    ix &= 0x7fffffff;
    if (ix <= 0x3f490fda) {             /* |x| ~<= pi/4 */
        if (ix < 0x39800000)            /* |x| < 2^-12 */
            return x;
        return sindf(x);
    }
    if (ix <= 0x407b53d1) {             /* |x| ~<= 5 pi/4 */
        if (ix <= 0x4016cbe3)           /* |x| ~<= 3 pi/4 */
            return sign ? -cosdf((double)x + PIO2_1) : cosdf((double)x - PIO2_1);
        return sindf(sign ? -((double)x + PIO2_2) : -((double)x - PIO2_2));
    }
    if (ix <= 0x40e231d5) {             /* |x| ~<= 9 pi/4 */
        if (ix <= 0x40afeddf)           /* |x| ~<= 7 pi/4 */
            return sign ? cosdf((double)x + PIO2_3) : -cosdf((double)x - PIO2_3);
        return sindf(sign ? (double)x + PIO2_4 : (double)x - PIO2_4);
    }
    if (ix >= 0x7f800000)               /* inf or NaN */
        return x - x;
    double y;
    switch (rem_pio2f(x, &y) & 3) {
    case 0: return sindf(y);
    case 1: return cosdf(y);
    case 2: return sindf(-y);
    default: return -cosdf(y);
    }
}

float cosf(float x)
{
    uint32_t ix = f2u(x);
    int sign = ix >> 31;
    ix &= 0x7fffffff;
    if (ix <= 0x3f490fda) {             /* |x| ~<= pi/4 */
        if (ix < 0x39800000)            /* |x| < 2^-12 */
            return 1.0f;
        return cosdf(x);
    }
    if (ix <= 0x407b53d1) {             /* |x| ~<= 5 pi/4 */
        if (ix > 0x4016cbe3)            /* |x| ~> 3 pi/4 */
            return -cosdf(sign ? (double)x + PIO2_2 : (double)x - PIO2_2);
        return sign ? sindf((double)x + PIO2_1) : sindf(PIO2_1 - (double)x);
    }
    if (ix <= 0x40e231d5) {             /* |x| ~<= 9 pi/4 */
        if (ix > 0x40afeddf)            /* |x| ~> 7 pi/4 */
            return cosdf(sign ? (double)x + PIO2_4 : (double)x - PIO2_4);
        return sign ? sindf(-(double)x - PIO2_3) : sindf((double)x - PIO2_3);
    }
    if (ix >= 0x7f800000)               /* inf or NaN */
        return x - x;
    double y;
    switch (rem_pio2f(x, &y) & 3) {
    case 0: return cosdf(y);
    case 1: return sindf(-y);
    case 2: return -cosdf(y);
    default: return sindf(y);
    }
}

/* ---------------------------------------------------------------- expf, logf */

/* y 2^k, rounded once (y normal) */
static float scale2(float y, int k)
{
    uint32_t u = f2u(y);
    int e = (int)((u >> 23) & 0xff) + k;
    if (e >= 0xff)
        return y * 0x1p127f * 0x1p127f;                     /* overflow: inf */
    if (e > 0)
        return u2f((u & 0x807fffffu) | (uint32_t)e << 23);
    if (e <= -64)
        return y * 0.0f;
    return u2f((u & 0x807fffffu) | (uint32_t)(e + 64) << 23) * 0x1p-64f; /* subnormal: one rounding */
}

float expf(float x)
{
    static const float half[2] = { 0.5f, -0.5f };
    static const float ln2hi = 6.9314575195e-1f, ln2lo = 1.4286067653e-6f, invln2 = 1.4426950216e+0f,
                       P1 = 1.6666625440e-1f, P2 = -2.7667332906e-3f;
    uint32_t hx = f2u(x);
    int sign = hx >> 31, k;
    float hi, lo;
    hx &= 0x7fffffff;
    if (x != x)
        return x;
    if (hx >= 0x42aeac50) {             /* |x| >= 87.33655 */
        if (hx > 0x7f800000)
            return x;
        if (hx >= 0x42b17218 && !sign)  /* x >= 88.722839: overflow */
            return x * 0x1p127f;
        if (sign && hx >= 0x42cff1b5)   /* x <= -103.972084: underflow */
            return 0.0f;
    }
    if (hx > 0x3eb17218) {              /* |x| > 0.5 ln2 */
        if (hx > 0x3f851592)            /* |x| > 1.5 ln2 */
            k = (int)(invln2 * x + half[sign]);
        else
            k = 1 - sign - sign;
        float fk = (float)k;
        hi = x - fk * ln2hi;
        lo = fk * ln2lo;
        x = hi - lo;
    } else if (hx > 0x39000000) {       /* |x| > 2^-14 */
        k = 0;
        hi = x;
        lo = 0.0f;
    } else {
        return 1.0f + x;
    }
    float xx = x * x, c = x - xx * (P1 + xx * P2), y = 1.0f + (x * c / (2.0f - c) - lo + hi);
    return k == 0 ? y : scale2(y, k);
}

float logf(float x)
{
    static const float ln2_hi = 6.9313812256e-01f, ln2_lo = 9.0580006145e-06f, Lg1 = 0xaaaaaa.0p-24f,
                       Lg2 = 0xccce13.0p-25f, Lg3 = 0x91e9ee.0p-25f, Lg4 = 0xf89e26.0p-26f;
    uint32_t ix = f2u(x);
    int k = 0;
    if (ix < 0x00800000 || ix >> 31) {  /* x < 2^-126 */
        if (ix << 1 == 0)
            return -__builtin_inff();   /* log(+-0) */
        if (ix >> 31)
            return __builtin_nanf("");  /* log(-x) */
        k -= 25;                        /* subnormal: scale x */
        x *= 0x1p25f;
        ix = f2u(x);
    } else if (ix >= 0x7f800000) {
        return x;
    } else if (ix == 0x3f800000) {
        return 0.0f;
    }
    /* x into [sqrt(2) / 2, sqrt(2)] */
    ix += 0x3f800000 - 0x3f3504f3;
    k += (int)(ix >> 23) - 0x7f;
    ix = (ix & 0x007fffff) + 0x3f3504f3;
    x = u2f(ix);
    float f = x - 1.0f, s = f / (2.0f + f), z = s * s, w = z * z;
    float t1 = w * (Lg2 + w * Lg4), t2 = z * (Lg1 + w * Lg3), R = t2 + t1, hfsq = 0.5f * f * f, dk = (float)k;
    return s * (hfsq + R) + dk * ln2_lo - hfsq + f + dk * ln2_hi;
}
