/*
 * The PC editor's protocol, served on the USB MIDI port like the original firmware does: the Matribox II PRO
 * editor and the Matribox Studio see the same pedal. Requests read and change the edited preset, the stored
 * presets, the global settings, the global EQ, the drum machine, the user IRs and Clone captures; the pedal
 * tells the computer what changes on it (knobs, switches, presets).
 *
 * Main loop only.
 */
#pragma once

#include "fw/globals_format.h"
#include "model/rig.h"
#include <stdint.h>

struct settings;
void editor_init(struct rig *rig, struct globals *globals, struct settings *settings);

/* what the USB MIDI port received (the editor's SysEx among it) */
void editor_feed(const uint8_t *buf, int n);

/* requests, pending notifications */
void editor_poll(void);

/* the master knob as the editor reports it (0..1) */
void editor_set_master(float level);
