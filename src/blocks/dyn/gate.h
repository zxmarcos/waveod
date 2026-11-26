/*
 * The original's Gate 1 and Gate 2 (docs/stock_port.md): a noise gate keyed by the chain's input (the right channel
 * of the side-chain buffer). The gain's target is 1 while |key| reaches the threshold (2^(0.1661 Threshold - 16.61):
 * -100 dB at 0 .. 0 dB at 100), else 0; the gain moves toward it by 1 - attack of the way per sample while rising
 * and, after one sample held, by 1 - release while falling. Both outputs are the left input x the gain. Bypassed it
 * passes the signal. Single precision.
 */
#pragma once
#include "../amps/amp.h"

struct gate {
    float thr_knob, att_knob, rel_knob;  /* the knobs (x 0.01) the values below were set from */
    float threshold, attack, release, gain, hold;
};

static inline float gate_threshold(float knob)
{
    return st_exp2f(st_fma(knob, 0.16609640419483185f, -16.60964012145996f));
}

/* a time knob (x 0.01) to its coefficient: e^(-1 / (44100 (scale k + offset))), the denominator in double */
static inline float gate_coeff(float k, double scale, double offset)
{
    return st_expf(-4.982313112122938e-05f / (float)st_fmad((double)k, scale, offset));
}

/* thr, att, rel: the knobs x 0.01; threshold, attack, release: their values for this block */
static inline void gate_run(struct gate *s, const struct wvfx_io *io, float thr, float att, float rel,
                            float threshold, float attack, float release)
{
    float *p = io->io;
    const float *key = io->aux;
    const int n = io->frames;
    if (n <= 0 || !io->enabled)
        return;
    /* the knobs are the same for the whole block: the original's per-sample checks only act on the first sample */
    if (s->thr_knob != thr) {
        s->thr_knob = thr;
        s->threshold = threshold;
    }
    if (s->att_knob != att) {
        s->att_knob = att;
        s->attack = attack;
    }
    if (s->rel_knob != rel) {
        s->rel_knob = rel;
        s->release = release;
    }
    const float thres = s->threshold, a = s->attack, r = s->release;
    float gain = s->gain, hold = s->hold;
    for (int i = 0; i < n; i++, p += 2, key += 2) {
        float target = thres > st_abs(key[1]) ? 0.0f : 1.0f, g;
        if (target > gain) {
            g = st_fma(a, gain - target, target);
            hold = 0.0f;
        } else if (!(hold > 0.0f)) {
            hold += 1.0f;
            g = gain;
        } else {
            g = st_fma(r, gain - target, target);
        }
        gain = g;
        p[0] = p[1] = g * p[0];
    }
    s->gain = gain;
    s->hold = hold;
}
