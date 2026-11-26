/*
 * The pieces the original's modulation effects share (docs/stock_port.md): the tempo divisions of their Sync and the
 * LFO rate they derive from it.
 */
#pragma once
#include "../amps/amp.h"

/* the beats per LFO period of each Rate value while Sync is on (0-10: 1/1 .. 1/32 with the dotted and triplet ones
   between); any other value counts as one beat */
static inline float mod_sync_beats(float rate)
{
    static const float beats[11] = {
        4.0f, 2.0f, 3.0f, 1.3333332538604736f, 1.0f, 1.5f, 0.6666666865348816f, 0.5f, 0.75f, 0.3333333134651184f, 0.25f,
    };
    for (int i = 0; i < 11; i++)
        if (rate == (float)i)
            return beats[i];
    return 1.0f;
}

/* a 1st-order allpass section as the phasers run it: y = (b0 x + b1 x1 - a1 y1) / a0 */
struct ap1 { float b0, b1, a1, a0; };
struct ap1_state { float x1, y1; };

static inline float ap1_run(const struct ap1 *c, struct ap1_state *s, float x)
{
    float y = st_fms(s->y1, c->a1, st_fma(s->x1, c->b1, c->b0 * x)) / c->a0;
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* the modulated delay lines of the vibrato, the flangers and the choruses: MOD_LINE(n) declares one of n samples;
   mod_line_run writes x, then reads d samples back (1 <= d < n) between the two samples around it */
#define MOD_LINE(n) struct { float buf[n]; int w; }

static inline float line_run(float *buf, int *w, int len, float x, float d)
{
    buf[*w] = x;
    *w = *w >= len - 1 ? 0 : *w + 1;
    int i = *w - (int)d;
    if (i < 0)
        i += len;
    int older = i < 1 ? i + len - 1 : i - 1;
    float a = buf[i], frac = d - __builtin_truncf(d);                    /* VRINTZ */
    return st_fma(buf[older] - a, frac, a);
}

#define mod_line_run(ln, x, d) line_run((ln)->buf, &(ln)->w, (int)(sizeof (ln)->buf / sizeof (ln)->buf[0]), x, d)

/* the LFO rate in cycles per sample: Rate in Hz (x per_hz: 1/44100 as the modules round it), or with Sync on (the
   knob's whole value 1) from the tempo */
static inline float mod_rate_k(float rate, float sync, uint32_t beat_ms, float per_hz)
{
    if ((int)sync == 1)
        return 1.0f / (((float)(beat_ms & 0xffffu) * 44.099998474121094f) * mod_sync_beats(rate));
    return rate * per_hz;
}

static inline float mod_rate(float rate, float sync, uint32_t beat_ms)
{
    return mod_rate_k(rate, sync, beat_ms, 2.2675736545352265e-05f);
}
