/*
 * waveod's modulators at work (fw/preset_format.h struct preset_mods; docs/modulators.md): in the audio interrupt,
 * once per block before the chain, each modulator's value 0..1 is worked out and the parameter links that follow
 * it (routes, compiled from the control table in the main loop: model/modulation.c) write their knob words, so
 * the movement is exact to the block whatever the main loop is doing.
 *
 * A route writes off + (on - off) x curve(value), held to its parameter's range, stepped when the parameter has
 * few steps (switches, types). The main loop owns everything else: it hands over a new set (the modulators and
 * their routes) with mods_set, which takes effect at the next block.
 */
#pragma once

#include "engine/chain.h"
#include "fw/preset_format.h"

#define MOD_ROUTES 32

struct mod_route {
    uint8_t mod;                        /* 0..MOD_COUNT-1 */
    uint8_t slot, knob;
    uint8_t curve;                      /* enum control_curve */
    float off, on;                      /* the values at 0 and at 1 */
    float min, max;                     /* the parameter's range */
    float step;                         /* > 0: the values are rounded to it (a parameter with few steps) */
};

/* The modulators and routes from the next block on (copied). n = 0: nothing moves. */
void mods_set(const struct preset_mods *mods, const struct mod_route *routes, int n);

/* Start every modulator again from its start (a preset was loaded). */
void mods_restart(void);

/* Interrupt: one block. `in` = the guitar at the chain's input (interleaved stereo, for ENV), beat_ms the tempo. */
void mods_run(struct chain *c, const float *in, int frames, uint32_t beat_ms);

/* The value of modulator i now, 0..1 (for the screens). */
float mods_value(int i);

/* Where modulator i is (for the screens' drawings): 0..1 of its LFO cycle or SEQ step, and the cycles / steps
   done since it started. */
float mods_phase(int i);
uint32_t mods_cycle(int i);

/* One modulator's next value, for the tests: the state of one modulator advanced by `frames`. */
struct mod_state {
    float phase;                        /* 0..1 of an LFO cycle or a SEQ step */
    uint32_t cycle;                     /* cycles / steps done */
    float from, to;                     /* random: the last and the next value */
    float env;                          /* ENV: the level followed */
    float value;
    uint32_t seed;
};
void mod_reset(struct mod_state *s, const struct preset_mod *m);
float mod_step(struct mod_state *s, const struct preset_mod *m, const float *in, int frames, uint32_t beat_ms);
