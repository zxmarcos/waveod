/*
 * The spectrum analyser's taps (docs/spectrum.md): two aligned rings of mono samples at 44.1 kHz, "before" and
 * "after", written by the audio interrupt and read by the main loop.
 *
 * What they hold follows scope_watch:
 *   a slot of the running chain   its input and its output, block by block (engine/chain.c run)
 *   SCOPE_PRESET                  the guitar at the chain's input and the chain's output (engine/audio_engine.c)
 *   SCOPE_EQ                      the global EQ's input and output (after the preset level, the tails, the looper)
 *   SCOPE_OFF                     nothing (no cost in the interrupt)
 * A stereo signal is taken as (L + R) / 2. Nothing arrives while the watched slot does not run (not loaded, the
 * tuner's mute) or the audio is suspended.
 */
#pragma once

#include <stdint.h>

struct chain;

#define SCOPE_RING   8192                       /* samples per ring (186 ms) */
#define SCOPE_OFF    (-1)
#define SCOPE_PRESET (-2)
#define SCOPE_EQ     (-3)

/* Main loop: what to tap from the next block on (the chain the slots belong to: the rig's own, not a spillover
   tail's copy). A change empties the rings. */
void scope_watch(const struct chain *c, int target);
int scope_target(void);

/* Main loop: the sample pairs that arrived since the last call (the newest SCOPE_RING at most); returns the count. */
int scope_read(float *before, float *after, int max);

/* Interrupt: whether slot `slot` of chain `c` is the one tapped. */
int scope_taps(const struct chain *c, int slot);

/* Interrupt: one block of both signals; `*_ch` = 1 (mono) or 2 (interleaved stereo). */
void scope_push(const float *before, int before_ch, const float *after, int after_ch, int frames);
