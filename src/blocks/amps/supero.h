/*
 * The pieces the original's Supero 2 amps share (docs/stock_port.md): the pre-filters, the oversampled stage, the
 * tone stack. Each model's file strings them in its order.
 *
 * One channel, per sample:
 *   gain          x 5 x Gain (A25)
 *   pre-filters   two fixed 1st-order sections
 *   stage         8x oversampled: 8 points interpolated from the last 5 inputs (the anti-aliasing FIR as a
 *                 polyphase filter, x 8), each through a soft clipper 100500 |v| / (v^2 + 129200 |v| + 92490), the
 *                 FIR over the last 40 clipped points back to one sample
 *   tone stack    3rd order from Tone (1 - A25 or B25 of the knob)
 * Every knob glides 0.0001 per sample; the tone coefficients stop one glide step short of Tone.
 */
#pragma once
#include "amp.h"

#define SUPERO_KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }
#define SUPERO_KNOBD(name, k, d) { name, 0.0f, 100.0f, 1.0f, d, k, 0x15, 0, 0, 0, 0 }

static const struct fos SUPERO_PF[2] = {
    { 0.9923461675643921f, -0.9923461675643921f, -0.984692394733429f },
    { -5.900777816772461f, 5.86108922958374f, -0.9603111743927002f },
};

/* ---------------------------------------------------------------- the oversampled stage (amp.h, os8_block) */

/* the clipper on 8 v: 100500 |8v| / ((8v)^2 + 92490 + 129200 |8v|), odd */
static inline float supero_clip(float v)
{
    v = v * 8.0f;
    float a = st_abs(v);
    float r = (a * 100500.0f) / st_fma(a, 129200.0f, st_fma(v, v, 92490.0f));
    return clip_sign(r, v);                     /* v < 0 ? -r : r */
}

static inline void supero_nl_block(struct os8 *s, float *y, int n)
{
    os8_block(s, y, n, supero_clip);
}

/* ---------------------------------------------------------------- the tone stack */

/*
 * From Tone t: ni = ci0 + ci1 t + ci2 t^2, d0 = t + 0.043, di = ei0 + ei1 t (i > 0), each fused from the constant
 * (t^2 rounded first); in single precision (the OD) or in double (the CL, t^2 still single).
 */
#define SUPERO_TONE(T, FMA, C) do { \
    T tt = t, t2 = (float)(t * t); \
    o->b[0] = (float)FMA(t2, C(-0.9054494), FMA(tt, C(0.960216), C(0.04297302))); \
    o->b[1] = (float)FMA(t2, C(2.716348), FMA(tt, C(-2.871068), C(-0.1222946))); \
    o->b[2] = (float)FMA(t2, C(-2.716348), FMA(tt, C(2.861505), C(0.1156907))); \
    o->b[3] = (float)FMA(t2, C(0.9054494), FMA(tt, C(-0.9506529), C(-0.03636912))); \
    o->a[0] = (float)(tt + C(0.04300836)); \
    o->a[1] = (float)FMA(tt, C(-2.983279), C(-0.1223249)); \
    o->a[2] = (float)FMA(tt, C(2.966593), C(0.1156554)); \
    o->a[3] = (float)FMA(tt, C(-0.9833139), C(-0.03633875)); \
} while (0)

static inline void supero_tone_set(struct tone3 *o, float t)
{
    SUPERO_TONE(float, st_fma, ST_F);
}

static inline void supero_tone_set_d(struct tone3 *o, float t)
{
    SUPERO_TONE(double, st_fmad, ST_D);
}
