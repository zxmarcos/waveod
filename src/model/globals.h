/*
 * The pedal's global settings shared with the original firmware (record 0, fw/globals_format.h): input level
 * and mode, AUX IN level, outputs without cabinet, global EQ. Loaded at start-up, applied to the audio engine
 * at once when changed, written to the flash on globals_save.
 */
#pragma once

#include "fw/globals_format.h"
#include "fw/presets.h"

void globals_load(struct globals *g, const struct preset_source *src);
void globals_apply(const struct globals *g);

/* counts globals_apply calls (the UI redraws when it moves) */
uint32_t globals_generation(void);

/* Write record 0 when the decoded fields differ from what is stored (the rest of the record is kept).
   0 = stored or nothing to do. Mutes the audio briefly. */
int globals_save(struct globals *g, struct preset_source *src);

/* A setting changed outside the settings screens (editor, MIDI): written once nothing changed for 2 s, like
   the original's +0xBFC flag. globals_poll writes it when due (main loop). */
void globals_touch(void);
void globals_poll(struct globals *g, struct preset_source *src);

/* Step parameter k of the global EQ by `steps` of its step size, within its range. */
void globals_eq_nudge(struct globals *g, int k, int steps);

/* The drum pattern of the settings (0..99), and choosing one (genre and pattern follow). */
int globals_drum_pattern(const struct globals *g);
void globals_set_drum_pattern(struct globals *g, int pattern);
