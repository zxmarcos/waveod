/*
 * The original's two simple fuzzes (Solar Fuzz, Red Fuzz; docs/stock_port.md) share their clipping stage: a hard clip
 * on 2 points per sample (the previous input and the midpoint to the current one), then two 4-point averages that
 * bring it back to one sample. The rest is in each model's file.
 */
#pragma once
#include "../amps/amp.h"

struct fz2 {
    float prev;                 /* the stage's input one sample ago */
    float a[3], b[3];           /* the clipped points and the first averages, oldest first */
};

static inline float fz2_clip(float x, float L)
{
    return x > L ? L : x < -L ? -L : x;
}

/* the stage on g, clipped at +-L */
static inline float fz2_run(struct fz2 *s, float g, float L)
{
    float p0 = fz2_clip(s->prev, L);
    float p1 = fz2_clip(st_fma(g - s->prev, 0.5f, s->prev), L);
    float b1 = (((s->a[1] + s->a[0]) + s->a[2]) + p0) * 0.25f;
    float b2 = (((s->a[2] + s->a[1]) + p0) + p1) * 0.25f;
    float y = (((s->b[0] + b1) + s->b[1]) + s->b[2]) * 0.25f;
    s->a[0] = s->a[2];
    s->a[1] = p0;
    s->a[2] = p1;
    s->b[0] = s->b[2];
    s->b[1] = b1;
    s->b[2] = b2;
    s->prev = g;
    return y;
}

#define FUZZ_PARAMS { \
    { "Fuzz", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 }, \
}
