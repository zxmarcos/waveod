/*
 * Setting the tempo from tap tempo, MIDI or a quick knob, as the original's FUN_000397EC does: the clock source
 * (global +0x79) decides who may set it - internal (0): taps and knobs, not the MIDI clock; TRS / USB /
 * external (1-3): the MIDI clock only; mixed (4): all. The global tempo takes it while it is on, otherwise the
 * preset's BPM (a quick knob always sets the preset's). The drums follow through the UI (ui.c) when synced.
 */
#pragma once

#include "fw/globals_format.h"
#include "model/rig.h"

enum tempo_from { TEMPO_TAP, TEMPO_MIDI_CLOCK, TEMPO_KNOB };

/* bpm 40..300; 1 = taken */
int tempo_set(struct rig *r, struct globals *g, int bpm, enum tempo_from from);
