/*
 * The spectrum analyser panel (docs/spectrum.md): the spectra of the two signals engine/scope.h taps, drawn on a
 * log frequency axis (20 Hz .. 20 kHz) and in dB of full scale (-90 .. 0): "before" as a grey line, "after" as a
 * filled area in the block's colour.
 *
 * Every main-loop pass (analyzer_tick) it takes the new samples; at most every ANALYZER_FRAME_MS, when at least
 * ANALYZER_HOP new samples came, it analyses the last SPEC_N of each (dsp/spectrum.h) and draws them into its own
 * pixel buffer (an LVGL canvas: no objects per bar). The curves rise at once and fall 30 dB per second; with no
 * samples (a slot that does not run, the audio suspended) they fall to the floor.
 *
 * Touch (analyzer_touch): tap the mode tag (top right) = the other mode; tap the plot = a cursor with the frequency
 * and both levels there (tap it again = no cursor); hold = freeze / unfreeze.
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>

struct chain;
struct gesture;

#define ANALYZER_FRAME_MS 40                    /* 25 frames per second at most */
#define ANALYZER_HOP      1024

enum analyzer_mode { ANALYZER_BLOCK, ANALYZER_PRESET };

/* One panel at a time (its buffers are static); returns its object (for gesture_bind). */
lv_obj_t *analyzer_create(lv_obj_t *parent, int x, int y, int w, int h);
void analyzer_show(int on);

/* What it watches: slot `slot` of chain `c` (its input and output), or with ANALYZER_PRESET the guitar and the
   chain's output; `color` for the "after" curve. A change clears the curves. */
void analyzer_watch(const struct chain *c, int slot, enum analyzer_mode mode, uint32_t color);

/* Stop tapping (the screen is left or the panel hidden). */
void analyzer_stop(void);

void analyzer_tick(void);

/* A gesture on the panel; returns 1 when it asks for the other mode. */
int analyzer_touch(const struct gesture *g);
