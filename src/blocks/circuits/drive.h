/* What waveod's circuit blocks (docs/drives.md) share: the levels, the bypass, the pots' tapers, the rebuild of the
 * matrices when a knob moves.
 *
 * - Levels: a full-scale sample is DRIVE_VIN volts at the circuit's input, and the output goes back with the same
 *   scale, so the circuit's own volume pot sets the level as on the pedal. Mono: (L + R) / 2 in, the same on both
 *   sides out.
 * - Knobs: the pedal's pots, 0..100. A knob's rotation (0..1) goes to the circuit through the pot's taper.
 * - Rebuild: poll (main loop) builds every stage into the matrix set the audio is not using, then flips the set;
 *   process takes the set once per block. A pot moves on the next block after the main loop sees it, the circuit's
 *   state (its capacitors) kept, as on the real pedal.
 * - Bypass: the output fades between the circuit and the dry signal over one block; bypassed and faded out, the
 *   circuit is not run (its state stays where it was).
 */
#pragma once

#include "sdk/wvfx.h"
#include "sdk/wvdk.h"
#include "sdk/wvos.h"

#define DRIVE_VIN   2.5f                /* volts at the input for a full-scale sample */
#define DRIVE_KNOBS 6

struct drive {
    volatile uint32_t cur;              /* the matrix set process uses */
    float built[DRIVE_KNOBS];           /* the knobs the published set was built for */
    float on;                           /* the circuit's share of the output, 0..1; < 0 before the first block */
    dk_work *work;                      /* in the buffer pool */
};

/* "A" (audio) taper: 10 % of the track at half rotation */
static inline float drive_log(float k)
{
    return (wvfx_exp2(k * 6.33985f) - 1.0f) * (1.0f / 80.0f);      /* (81^k - 1) / 80 */
}

/* a knob 0..100 as a rotation 0..1 */
static inline float drive_rot(float knob)
{
    return wvfx_clamp(knob, 0.0f, 100.0f) * 0.01f;
}

static inline void drive_init(struct drive *d, void *buffer, const float *knobs, int n)
{
    d->work = (dk_work *)buffer;
    d->cur = 0;
    d->on = -1.0f;
    for (int i = 0; i < n; i++)
        d->built[i] = knobs[i];
}

/* poll: the set to build into when the knobs moved since the last build, else -1 */
static inline int drive_stale(struct drive *d, const float *knobs, int n)
{
    int moved = 0;
    for (int i = 0; i < n; i++)
        moved |= knobs[i] != d->built[i];
    if (!moved)
        return -1;
    for (int i = 0; i < n; i++)
        d->built[i] = knobs[i];
    return (int)(dk_current(&d->cur) ^ 1u);
}

/* one block through `run` (one 1x sample in volts -> out in volts, the circuit's context ctx), with the bypass */
static inline __attribute__((always_inline)) void drive_process(struct drive *d, const struct wvfx_io *io,
                                                                float (*run)(void *ctx, float x), void *ctx)
{
    float target = io->enabled ? 1.0f : 0.0f;
    if (d->on < 0.0f)
        d->on = target;
    if (d->on == 0.0f && target == 0.0f)
        return;
    float *x = io->io;
    int frames = io->frames;
    if (d->on == 1.0f && target == 1.0f) {
        for (int f = 0; f < frames; f++) {
            float u = 0.5f * (x[2 * f] + x[2 * f + 1]) * DRIVE_VIN;
            float y = run(ctx, u) * (1.0f / DRIVE_VIN);
            x[2 * f] = x[2 * f + 1] = y;
        }
        return;
    }
    float on = d->on, step = (target - on) / (float)frames;
    for (int f = 0; f < frames; f++) {
        float l = x[2 * f], r = x[2 * f + 1];
        float y = run(ctx, 0.5f * (l + r) * DRIVE_VIN) * (1.0f / DRIVE_VIN);
        on += step;
        x[2 * f] = l + on * (y - l);
        x[2 * f + 1] = r + on * (y - r);
    }
    d->on = target;
}
