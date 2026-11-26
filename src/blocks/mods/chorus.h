/*
 * What the original's choruses share beyond mod.h: the fixed sections of the E-Chorus and the CE-Roto (the same in
 * both); their 3rd-order section without a0 (lp3) is in amp.h.
 */
#pragma once
#include "mod.h"

/* the E-Chorus' and the CE-Roto's input section (on x / 4), the low pass before their lines and the voice's
   4th-order low pass */
static const struct fos CE_IN = { 47.88689422607422f, -47.882080078125f, -0.995186984539032f };
static const struct lp3 CE_LP3 = {
    0.12317291647195816f, 0.1405276656150818f, -0.08846341073513031f, -0.10581816732883453f, -1.3655462265014648f,
    0.7855644226074219f, -0.19787733256816864f,
};

/* the voice's 4th-order low pass: (b0 x + .. + b4 x4) - (a1 y1 + .. + a4 y4) */
static const float CE_LP4_B[5] = {
    0.025730302557349205f, 0.10292121022939682f, 0.15438181161880493f, 0.10292121022939682f, 0.025730302557349205f,
};
static const float CE_LP4_A[4] = { -0.09892655909061432f, -0.23964674770832062f, -0.10688899457454681f, -0.14285285770893097f };
struct ce_lp4_state { float x[4], y[4]; };

static inline float ce_lp4_run(struct ce_lp4_state *s, float x)
{
    float n = CE_LP4_B[0] * x, d = CE_LP4_A[0] * s->y[0];
    for (int i = 1; i < 5; i++)
        n = st_fma(CE_LP4_B[i], s->x[i - 1], n);
    for (int i = 1; i < 4; i++)
        d = st_fma(CE_LP4_A[i], s->y[i], d);
    float y = n - d;
    for (int i = 3; i > 0; i--) {
        s->x[i] = s->x[i - 1];
        s->y[i] = s->y[i - 1];
    }
    s->x[0] = x;
    s->y[0] = y;
    return y;
}
