/*
 * waveod's own preset format (stored by fw/wv_store.c), not the original's 0x48C-byte record:
 *
 *   "WP" u8 version u8 flags, then chunks {u8 tag, u16 length (little endian), bytes}
 *
 *   1 NAME   the name (up to 17 bytes)           2 STYLE  the style (up to 10 bytes)
 *   3 MAIN   u8 volume, i8 pan, u16 BPM, u8 FX loop, u8 EXP 1 group, u8 count, i8 order[12]
 *   4 SLOT   one per used slot: u8 slot, u8 on, i8 menu, i16 index, u32 id, u8 n, f32 knobs[n]
 *   5 QK     i8 target[3], i8 parameter[3]       6 EXP    9 x {i8 slot, i8 parameter, f32 min, f32 max}
 *   7 CTRL   u8 member[5][12]                    8 FS     i8 function[2][26]
 *   9 ROUTE  u8 stored, mode, split, merge, i8 cell[12], u8 joint[6]
 *
 * A reader skips the tags it does not know, so later versions add chunks (scenes, ...) without breaking older
 * firmware; what a chunk does not set keeps preset_clear's value. Everything of struct preset is kept except
 * `raw` (the original's record), which a decoded preset gets from the template.
 */
#pragma once

#include "fw/preset_format.h"

#define WVP_MAX 3072                /* largest encoding: 12 slots with 15 knobs each and every chunk full (48 links,
                                       32 scene parameters, 4 modulators; tests/test_scenes.c) */

/* Encode into out (room for `max` bytes); the length, or -1 when it does not fit. */
int wvp_encode(const struct preset *p, uint8_t *out, unsigned max);

/* The control table alone as its chunk (tag 11, length, body), always (waveod Studio's describe); the length, or
   -1 when it does not fit. */
int wvp_encode_controls(const struct preset_controls *c, uint8_t *out, unsigned max);

/* A control table from such a chunk (the global table's record, the Studio's); -1 when malformed. */
int wvp_decode_controls(const uint8_t *in, unsigned len, struct preset_controls *out);

/* Decode; -1 when the data is not this format (out is then an empty preset). */
int wvp_decode(const uint8_t *in, unsigned len, struct preset *out);

/* The name only; -1 when the data is not this format. */
int wvp_name(const uint8_t *in, unsigned len, char out[PRESET_NAME_LEN + 1]);
