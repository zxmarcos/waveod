/*
 * 2x / 4x oversampling for waveod's blocks (docs/drives.md): two halfband stages, 1x <-> 2x a 47-tap halfband
 * (odd taps, linear phase), 2x <-> 4x a 19-tap minimax halfband (-92 dB from 68 kHz at 4x). The filters of
 * research/pulse/common/dsp.h (the Dark Muff there), same arithmetic. The state (os4_t) lives in the block's state;
 * the stages run as callbacks with a context, which the compiler inlines when they are constants.
 *
 *   y = os4_run2(&o, x, g, f, h, ctx)   one 1x sample: g at 2x, f at 4x, h at 2x (g, h may be 0)
 *   y = os4_run(&o, x, f, ctx)          one 1x sample: f at 4x
 *   y = os2_run(&o, x, g, f, ctx)       one 1x sample: g then f at 2x
 *
 * Latency: OS_DELAY samples at 1x for os4_run / os4_run2, OS2_DELAY for os2_run.
 */
#pragma once

#include <stdint.h>

#define OS_FN static inline __attribute__((unused))

#define HB_TAPS   12                    /* stage 1, 1x <-> 2x: 47-tap halfband, odd taps per side */
#define HB_N      (2 * HB_TAPS)
#define HB_CENTER 0.499995397f
#define HB2_TAPS  5                     /* stage 2, 2x <-> 4x: 19-tap minimax */
#define HB2_N     (2 * HB2_TAPS)
#define OS2_DELAY 23.0f                 /* 47 taps at 2x, up and down: 2 x 23 / 2 */
#define OS_DELAY  (OS2_DELAY + 4.5f)    /* + 19 taps at 4x, up and down: 2 x 9 / 4 */

static const float HB[HB_TAPS] = {
    3.160629362e-01f, -9.953458363e-02f, 5.323959922e-02f, -3.190621204e-02f, 1.951168259e-02f,
    -1.168538411e-02f, 6.670847582e-03f, -3.539467847e-03f, 1.690651032e-03f, -6.900036008e-04f,
    2.146042569e-04f, -3.236808785e-05f };
static const float HB2[HB2_TAPS] = { 3.069135016e-01f, -7.602794622e-02f, 2.439581712e-02f, -6.145266956e-03f, 8.638944452e-04f };

/* histories are stored twice so a window is always contiguous */
typedef struct {
    float up_h[2 * HB_N], dn_a[2 * HB_N], dn_b[2 * HB_N];
    float up2_h[2 * HB2_N], dn2_a[2 * HB2_N], dn2_b[2 * HB2_N];
    uint32_t up_p, dn_p, up2_p, dn2_p;
} os4_t;
typedef float (*os4_fn)(void *ctx, float x);

OS_FN void os4_init(os4_t *o)
{
    for (int i = 0; i < 2 * HB_N; i++) o->up_h[i] = o->dn_a[i] = o->dn_b[i] = 0.0f;
    for (int i = 0; i < 2 * HB2_N; i++) o->up2_h[i] = o->dn2_a[i] = o->dn2_b[i] = 0.0f;
    o->up_p = o->dn_p = o->up2_p = o->dn2_p = 0;
}

OS_FN float hb_sym(const float *w)     /* w[0] newest .. w[HB_N-1] oldest, symmetric odd taps */
{
    float acc = 0.0f;
    for (int j = 0; j < HB_TAPS; j++) acc += HB[HB_TAPS - 1 - j] * (w[j] + w[HB_N - 1 - j]);
    return acc;
}

OS_FN float hb2_sym(const float *w)
{
    float acc = 0.0f;
    for (int j = 0; j < HB2_TAPS; j++) acc += HB2[HB2_TAPS - 1 - j] * (w[j] + w[HB2_N - 1 - j]);
    return acc;
}

OS_FN float os4_stage2(os4_t *o, float s, os4_fn f, void *ctx)   /* one 2x sample: up to 4x, f on both, down */
{
    o->up2_h[o->up2_p] = o->up2_h[o->up2_p + HB2_N] = s;
    const float *w = &o->up2_h[o->up2_p];
    float t0 = w[HB2_TAPS - 1];         /* the centre tap is 0.5: x 2 for the zero stuffing */
    float t1 = 2.0f * hb2_sym(w);
    o->up2_p = o->up2_p == 0 ? HB2_N - 1 : o->up2_p - 1;
    float a = f(ctx, t1);               /* the older phase */
    float b = f(ctx, t0);
    o->dn2_a[o->dn2_p] = o->dn2_a[o->dn2_p + HB2_N] = a;
    o->dn2_b[o->dn2_p] = o->dn2_b[o->dn2_p + HB2_N] = b;
    float y = 0.5f * o->dn2_b[o->dn2_p + HB2_TAPS] + hb2_sym(&o->dn2_a[o->dn2_p]);
    o->dn2_p = o->dn2_p == 0 ? HB2_N - 1 : o->dn2_p - 1;
    return y;
}

/* as os4_stage2, with g run on the 2x sample before and h after (stages that are fine at 2x; either may be 0) */
OS_FN float os4_stage2g(os4_t *o, float s, os4_fn g, os4_fn f, os4_fn h, void *ctx)
{
    float y = os4_stage2(o, g ? g(ctx, s) : s, f, ctx);
    return h ? h(ctx, y) : y;
}

OS_FN float os4_run2(os4_t *o, float x, os4_fn g, os4_fn f, os4_fn h, void *ctx)
{
    o->up_h[o->up_p] = o->up_h[o->up_p + HB_N] = x;
    const float *w = &o->up_h[o->up_p];
    float u0 = 2.0f * HB_CENTER * w[HB_TAPS - 1];
    float u1 = 2.0f * hb_sym(w);
    o->up_p = o->up_p == 0 ? HB_N - 1 : o->up_p - 1;
    float va = os4_stage2g(o, u1, g, f, h, ctx);
    float vb = os4_stage2g(o, u0, g, f, h, ctx);
    o->dn_a[o->dn_p] = o->dn_a[o->dn_p + HB_N] = va;
    o->dn_b[o->dn_p] = o->dn_b[o->dn_p + HB_N] = vb;
    float y = HB_CENTER * o->dn_b[o->dn_p + HB_TAPS] + hb_sym(&o->dn_a[o->dn_p]);
    o->dn_p = o->dn_p == 0 ? HB_N - 1 : o->dn_p - 1;
    return y;
}

OS_FN float os2_run(os4_t *o, float x, os4_fn g, os4_fn f, void *ctx)
{
    o->up_h[o->up_p] = o->up_h[o->up_p + HB_N] = x;
    const float *w = &o->up_h[o->up_p];
    float u0 = 2.0f * HB_CENTER * w[HB_TAPS - 1];
    float u1 = 2.0f * hb_sym(w);
    o->up_p = o->up_p == 0 ? HB_N - 1 : o->up_p - 1;
    if (g) { u1 = g(ctx, u1); u0 = g(ctx, u0); }
    float va = f(ctx, u1), vb = f(ctx, u0);
    o->dn_a[o->dn_p] = o->dn_a[o->dn_p + HB_N] = va;
    o->dn_b[o->dn_p] = o->dn_b[o->dn_p + HB_N] = vb;
    float y = HB_CENTER * o->dn_b[o->dn_p + HB_TAPS] + hb_sym(&o->dn_a[o->dn_p]);
    o->dn_p = o->dn_p == 0 ? HB_N - 1 : o->dn_p - 1;
    return y;
}

OS_FN float os4_run(os4_t *o, float x, os4_fn f, void *ctx)
{
    o->up_h[o->up_p] = o->up_h[o->up_p + HB_N] = x;
    const float *w = &o->up_h[o->up_p];
    float u0 = 2.0f * HB_CENTER * w[HB_TAPS - 1];
    float u1 = 2.0f * hb_sym(w);
    o->up_p = o->up_p == 0 ? HB_N - 1 : o->up_p - 1;
    float va = os4_stage2(o, u1, f, ctx);
    float vb = os4_stage2(o, u0, f, ctx);
    o->dn_a[o->dn_p] = o->dn_a[o->dn_p + HB_N] = va;
    o->dn_b[o->dn_p] = o->dn_b[o->dn_p + HB_N] = vb;
    float y = HB_CENTER * o->dn_b[o->dn_p + HB_TAPS] + hb_sym(&o->dn_a[o->dn_p]);
    o->dn_p = o->dn_p == 0 ? HB_N - 1 : o->dn_p - 1;
    return y;
}
