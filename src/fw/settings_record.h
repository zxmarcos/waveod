/*
 * waveod's own settings, kept in the record store next to the original's records.
 *
 * The original reads only record 0 (its settings), record 1 (the preset map) and the records the map points
 * at (2..299). waveod uses the highest record the map does not point at (299 on a pedal whose presets were
 * never moved), tagged "WVOD", so the original never sees it. If the original later stores a preset there,
 * the tag no longer matches and waveod falls back to its defaults; nothing else is affected.
 */
#pragma once

#include "fw/presets.h"

#define SETTINGS_RECORD_MAX 64              /* payload bytes */

/* Copy the payload (up to `len` bytes, the rest zeroed); returns its stored length, or -1 when absent. */
int settings_record_read(const struct preset_source *src, void *out, unsigned len);

/* Store the payload. 0 = written. Blocks while the flash is written: suspend the audio first. */
int settings_record_write(struct preset_source *src, const void *data, unsigned len);
