/*
 * The pieces the original's Voks amps share (docs/stock_port.md): the 30's pre-filters, its gain sections, the
 * Boost and Tone Cut sections Tone Cut moves, the envelope. The generic ones are in amp.h. Each model's file strings
 * them in its order with its tables.
 */
#pragma once
#include "amp.h"

#define VOKS_PARAM(name, def, knob) { name, 0.0f, 100.0f, 1.0f, def, knob, 0x15, 0, 0, 0, 0 }

/* the 30's pre-filters */
static const struct sos VOKS_PRE[2] = {
    { 6.593889236450195f, -9.447031021118164f, 2.8567209243774414f, 1.0f, -1.4265309572219849f,
      0.4273275136947632f },
    { 9.447031021118164f, 8.167877197265625f, 0.8710530996322632f, 1.0f, 0.7421280741691589f,
      0.012618900276720524f },
};

/* the envelope: |x| through a fixed 2nd-order low pass */
static inline float voks_env_run(struct env_state *e, float x)
{
    return env_run(e, x, 2.4700448193470947e-05f, -1.9900600910186768f, 0.9900847673416138f);
}

/* the 30's gain sections from the glided Gain g: Bright off 1st order (nrm), on 2nd order (brt). p is g in Process;
   Init computes from the taper's value before x 0.8 (p) with the constants x 0.8 */
struct voks_gain {
    struct fos nrm;
    float nrm_a0;
    struct sos brt;
};

static const float VOKS_GAIN_PROCESS[4] = { 0.999758780002594f, -1.0002410411834717f, -1.0226759910583496f,
                                            -0.9773243069648743f };
static const float VOKS_GAIN_INIT[4] = { 0.7998070120811462f, -0.8001928329467773f, -0.8181408047676086f,
                                         -0.7818594574928284f };

static inline void voks_gain_set(struct voks_gain *c, float g, float p, const float *k)
{
    float g2 = g * g, n0 = p * k[0];
    c->nrm.b0 = n0;
    c->nrm.b1 = p * -k[0];
    c->nrm_a0 = 1.0f;
    c->nrm.a1 = -0.9995176792144775f;
    c->brt.b0 = st_fma(p, k[2], g2);
    c->brt.b1 = (g - g2) + (g - g2);
    c->brt.b2 = st_fma(p, k[3], g2);
    c->brt.a0 = (-0.022922439500689507f - n0) + g2;
    c->brt.a1 = c->brt.b1 + -1.0940189895336516e-05f;
    c->brt.a2 = st_fma(p, k[1], 0.022911500185728073f) + g2;
}


/* the Boost section from Tone Cut t: a bilinear shelf whose corner is 2^(c0 + c1 t) (the 30N: 2.491446 - 8.30482 t,
   the 30TB: 1.660964 - 6.643856 t) */
static inline void voks_boost_set(struct sos *c, float t, float c1, float c0)
{
    const float K = 0.2929231822490692f, P = 1.2929232120513916f, N = -0.7070767879486084f;
    float e = st_exp2f(st_fma(t, c1, c0));
    float p = 2.0f / (e + 1.0f), q = 2.0f / (1.0f / e + 1.0f);
    float a = p + K, b = K - p, cq = q + K, d = K - q;
    float den = a * P;
    c->b0 = (cq * N) / den;
    c->b1 = st_fma(cq, P, d * N) / den;
    c->b2 = (d * P) / den;
    c->a0 = 1.0f;
    c->a1 = st_fma(b, P, a * N) / den;
    c->a2 = (b * N) / den;
}

/* the Tone Cut section: e = 2^(c1 t) (the 30N: c1 -1.0796267, the 30TB: -0.99657845), (1 + 0.6997 e, -0.291,
   1 - 0.6997 e) / (1 + 0.6997 / e, ...) */
static inline void voks_tonecut_set(struct sos *c, float t, float c1)
{
    float e = st_exp2f(t * c1);
    float n = e * 0.6996855735778809f, d = 0.6996855735778809f / e;
    c->b0 = n + 1.0f;
    c->b1 = -0.29103848338127136f;
    c->b2 = 1.0f - n;
    c->a0 = d + 1.0f;
    c->a1 = -0.29103848338127136f;
    c->a2 = 1.0f - d;
}
