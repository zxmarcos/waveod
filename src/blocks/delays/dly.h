/*
 * The pieces the original's delays share (docs/stock_port.md): the delay time from Time or the tempo, the two
 * 176400-sample lines (4 s each) in the block's buffer, the read between two samples.
 */
#pragma once
#include "../amps/amp.h"

#define DLY_LINE 176400                 /* samples per line: 4 s */
#define DLY_BUFFER (2 * DLY_LINE * (int)sizeof(float))

/* the beats per Time value while Sync is on (0-10: 1/1 .. 1/32 with the dotted and triplet ones between); any other
   value counts as one beat */
static const float DLY_SYNC_BEATS[11] = {
    4.0f, 2.0f, 3.0f, 1.3333332538604736f, 1.0f, 1.5f, 0.6666666865348816f, 0.5f, 0.75f, 0.3333333134651184f, 0.25f,
};

/* the delay time in samples: Time in ms, or with Sync on (the knob's whole value 1) the tempo division picked by
   Time's whole value; clipped to 882 .. 176400 (20 ms .. 4 s) */
static inline float dly_time(float time, float sync, uint32_t beat_ms)
{
    float t;
    if ((int)sync == 1) {
        unsigned i = (unsigned)(int)time;
        t = ((float)(beat_ms & 0xffffu) * 44.099998474121094f) * (i <= 10 ? DLY_SYNC_BEATS[i] : 1.0f);
    } else {
        t = (float)(int)time * 44.099998474121094f;
    }
    return __builtin_fmaxf(__builtin_fminf(t, 176400.0f), 882.0f);                  /* VMINNM, VMAXNM */
}

/* the same computed in double (the Warm and others) */
static inline float dly_time_d(float time, float sync, uint32_t beat_ms)
{
    float t;
    if ((int)sync == 1) {
        unsigned i = (unsigned)(int)time;
        t = (float)(((double)(beat_ms & 0xffffu) * 44.1) * (double)(i <= 10 ? DLY_SYNC_BEATS[i] : 1.0f));
    } else {
        t = (float)((double)(int)time * 44.1);
    }
    return __builtin_fmaxf(__builtin_fminf(t, 176400.0f), 882.0f);
}

/* the BBD's and Ice's: Time in ms as it is (not clipped, not whole), or with Sync on the tempo division picked by
   Time's whole value (the tempo word whole), clipped to 882 .. 176400 */
static inline float dly_time_free(float time, float sync, uint32_t beat_ms)
{
    if ((int)sync != 1)
        return time * 44.10000228881836f;
    int i = (int)time;
    i = i > 9 ? 10 : i < 0 ? 0 : i;
    float t = ((float)(int32_t)beat_ms * 44.10000228881836f) * DLY_SYNC_BEATS[i];
    return __builtin_fmaxf(__builtin_fminf(t, 176400.0f), 882.0f);                  /* VMINNM, VMAXNM */
}

/* a line of the buffer: write x at w, step w, then read d samples back between the two samples around it */
struct dly_line {
    float *buf;
    int w;
};

static inline void dly_write(struct dly_line *l, float x)
{
    l->buf[l->w] = x;
    l->w = l->w > DLY_LINE - 2 ? 0 : l->w + 1;
}

static inline float dly_read(const struct dly_line *l, int whole, float frac)
{
    int i = l->w - whole;
    if (i < 0)
        i += DLY_LINE;
    int older = i < 1 ? i + DLY_LINE - 1 : i - 1;
    float a = l->buf[i];
    return st_fma(l->buf[older] - a, frac, a);
}

/* x held at +-L as VMAXNM / VMINNM (no flags: a float compare reaches its select through the FPSCR flags, which
   serialize the code around it): the same value as the delays' compares, x >= L ? L : x > -L ? x : -L and the
   like, for every x but NaN */
static inline float dly_hold(float x, float L)
{
    return __builtin_fminf(__builtin_fmaxf(x, -L), L);
}

static inline float dly_clip2(float x)
{
    return dly_hold(x, 2.0f);
}
