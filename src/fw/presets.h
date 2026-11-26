/*
 * Where each of the 240 presets lives.
 *
 * waveod keeps its own presets in its own store (fw/wv_store.c, format fw/wv_preset.h) and never writes a
 * preset into the original's record store again. A map in that store says, for each place i:
 *   - one of waveod's records (a preset saved by waveod, or moved / copied there);
 *   - the original's preset s (WMAP_STOCK | s), read only, the way the original loader finds it
 *     (FUN_00021018 in app V1.2.2): record = map[s] of the record store (default s + 2); a valid record there
 *     wins (user preset), otherwise record - 2 < 120 selects the factory bank (0x440000 + r * 0x500 +
 *     r / 3 * 0x100), otherwise it is empty;
 *   - an empty preset (WMAP_EMPTY).
 * Until waveod saves anything every place is WMAP_STOCK | i: the presets are the original's, unchanged. Going
 * back to the original firmware finds its own presets as they were.
 *
 * The record store keeps the original's global settings (record 0), which both firmwares share, and waveod's
 * settings record (fw/settings_record.h).
 */
#pragma once

#include "fw/preset_format.h"
#include "fw/record_store.h"

enum preset_origin { PRESET_USER, PRESET_FACTORY, PRESET_EMPTY };

#define WMAP_STOCK  0x8000u                 /* | the original's preset number */
#define WMAP_EMPTY  0xFFFFu

struct preset_source {
    struct record_store store;
    int has_store;
    int16_t map[PRESET_COUNT];          /* the original's map (record 1), read only */
    uint16_t wmap[PRESET_COUNT];        /* waveod's map: its record key, WMAP_STOCK | s or WMAP_EMPTY */
    int16_t wrec[PRESET_COUNT];         /* the record number the original's list would show (moves carry it) */
    const uint8_t *factory;             /* FLASH_FACTORY_BANK, or 0 */
    uint16_t last_index;                /* preset that was active when the original firmware last saved */
};

void presets_open(struct preset_source *src);
enum preset_origin presets_load(const struct preset_source *src, int index, struct preset *out);

/* Store preset `index` in waveod's store. 0 = written; -10 bad index, -12 does not encode, -21 bad arguments,
   -22 store full, -23 flash failure. Blocks while the flash is written: suspend the audio first. */
int presets_store(struct preset_source *src, int index, const struct preset *p);

/* The same from an original-format record (the PC editor's import, rename, delete). */
int presets_save(struct preset_source *src, int index, const uint8_t rec[PRESET_RECORD_SIZE]);

/* Preset `from` copied over `to`, everything kept. 0 = written. Blocks like presets_store. */
int presets_copy(struct preset_source *src, int from, int to);

/* Preset `from` goes to place `to` and the ones between shift by one (the PC editor's move). 0 = written.
   Blocks like presets_store. */
int presets_move(struct preset_source *src, int from, int to);

/* The record number the original's preset list shows for place `index` (the PC editor reads it). */
int presets_map_value(const struct preset_source *src, int index);

/* Make `index` the preset loaded at power-up (global settings, record 0, +0x0C; the record is created with
   the original's factory values when missing). Blocks like presets_store. */
int presets_set_startup(struct preset_source *src, int index);

/* The original's global settings (record 0, fw/globals_format.h): the stored record, or 0. */
const uint8_t *presets_globals(const struct preset_source *src);

/* Store record 0 (GLOBALS_RECORD_SIZE bytes). 0 = written. Blocks like presets_store. */
int presets_write_globals(struct preset_source *src, const uint8_t *rec);

/* The global control table (fw/preset_format.h): the stored one (waveod's store, WVS_MAP key 1, as a controls
   chunk), or the factory one. */
void presets_global_controls(struct preset_controls *out);

/* Store the global control table (the links that may not be global left out). 0 = written. Blocks like
   presets_store. */
int presets_store_global_controls(const struct preset_controls *c);

/* Preset `index` as an original-format record (the PC editor's export): the original's record as it reads it
   (FUN_00021018; factory and empty ones numbered `index`), or waveod's preset encoded over the template. */
void presets_read(const struct preset_source *src, int index, uint8_t out[PRESET_RECORD_SIZE]);

/* Where preset `index` comes from, without decoding it (waveod's own presets count as PRESET_USER). */
enum preset_origin presets_origin(const struct preset_source *src, int index);

/* What place `index` holds: one of waveod's records, the original's preset, or nothing. */
enum preset_place { PLACE_WAVEOD, PLACE_ORIGINAL, PLACE_EMPTY };
enum preset_place presets_place(const struct preset_source *src, int index);

/* Name only (cheap enough for lists). */
void presets_name(const struct preset_source *src, int index, char out[PRESET_NAME_LEN + 1]);
