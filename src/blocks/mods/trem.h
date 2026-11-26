/*
 * What the original's Sine, Triangle and Bias Trem share: Depth, Volume and the LFO rate gliding 0.00022676 of the way
 * to the knobs each sample (from where they were at the block's start; the rate from 0), an LFO sample count that
 * starts over once it would reach one period, and a fixed 2nd-order tone section, x 2.
 */
#pragma once
#include "mod.h"

static const struct sos TREM_TONE = {
    2.0582547187805176f, -3.705545425415039f, 1.6647188663482666f, -2.0582549571990967f, 3.705545425415039f,
    -1.6647188663482666f,
};

struct trem {
    float depth, volume, rate, n;       /* glided; n counts samples (in float) */
    float dd, dv, dr;                   /* this block's steps */
};

static inline void trem_init(struct trem *t, float depth, float volume)
{
    *t = (struct trem){ depth * 0.01f, volume * 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
}

static inline void trem_block(struct trem *t, float depth, float volume, float rate)
{
    t->dv = (volume * 0.01f - t->volume) * 0.0002267600066261366f;
    t->dr = (rate - t->rate) * 0.0002267600066261366f;
    t->dd = (depth * 0.01f - t->depth) * 0.0002267600066261366f;
}

/* one sample's glides; the LFO's phase 2 pi r n */
static inline float trem_step(struct trem *t)
{
    t->rate += t->dr;
    t->depth += t->dd;
    t->volume += t->dv;
    return (t->rate * 6.2831854820251465f) * t->n;
}

/* the sample count after this sample: n + 1 while that is short of the period, else 0 */
static inline void trem_count(struct trem *t, float period)
{
    float next = t->n + 1.0f;
    t->n = period > next ? next : 0.0f;
}
