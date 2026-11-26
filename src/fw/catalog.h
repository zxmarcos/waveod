/*
 * Everything a chain slot can hold, per menu: library entries for most menus; for Cab (5), User IR (6) and
 * Clone (11) the items are IRs / captures, all run by the menu's single engine entry.
 *
 * A slot stores (menu, id). Resolution follows the original preset loader (FUN_00021660): the id is looked
 * up in the menu, then in the next menu (presets saved before a menu was inserted); if it is still unknown
 * the slot falls back to item 0 of its menu with default knobs.
 *
 * After the library's entries a menu lists waveod's built-in blocks (engine/blocks.h, docs/blocks.md), then the
 * modules of waveod's library in the flash (fw/wv_library.h); the Cab and User IR items run on waveod's engine
 * block while it is in use, its parameters after the entry's; a Clone item holding a NAM capture runs on the NAM
 * block. User IR and Clone items are named as they were stored (fw/ir_store.h ir_fs_name), else as the directory.
 *
 * A slot with an id of waveod's (sdk/wvfx.h WVFX_IS_ID) that is not installed does not fall back: it resolves to
 * CATALOG_MISSING, keeps its id and knobs, and passes the signal until the module is installed again.
 */
#pragma once

#include "fw/ir_store.h"
#include "fw/library.h"

struct wvfx_desc;

struct catalog {
    struct library lib;
    struct ir_dir irs;
    struct library wvl;             /* waveod's library (empty when there is none) */
};

#define CATALOG_MISSING (-2)        /* the index of an effect of waveod's that is not installed */
#define CATALOG_MISSING_NAME "AUSENTE"

struct catalog_item {
    uint32_t id;
    char name[24];
    struct lib_entry engine;        /* the library entry whose module runs the item and defines its knobs (for a
                                       built-in only id, name and category are set: elf 0, no parameters) */
    const struct wvfx_desc *block;  /* the block of waveod's that runs it instead, or 0 */
    int nparams;                    /* the entry's parameters, then the block's (catalog_param) */
};

/* Parameter k (0..nparams-1) of an item: the library entry's, then its block's. 0 = there is one. */
int catalog_param(const struct catalog_item *item, int k, struct lib_param *out);

enum resolve_result { RESOLVE_EXACT = 0, RESOLVE_FALLBACK = 1, RESOLVE_MISSING = 2, RESOLVE_INVALID = -1 };

int catalog_open(struct catalog *c);

/* Read waveod's library again (after it was written). 0 = there is one. */
int catalog_reopen_wvlib(struct catalog *c);

int catalog_is_special(int menu);
int catalog_count(const struct catalog *c, int menu);
int catalog_item(const struct catalog *c, int menu, int index, struct catalog_item *out);

/* Find (menu, id); *menu_io may change to the next menu, *index gets the item. */
enum resolve_result catalog_resolve(const struct catalog *c, int *menu_io, uint32_t id, int *index);

/* Flash offset of the data an IR / capture engine reads, 0 when there is none. */
uint32_t catalog_data(const struct catalog *c, int menu, int index);

/* What Clone item `index` holds: a capture of the original's engine (or nothing), a NAM capture at 44.1 kHz, or a
   NAM capture that runs at 48 kHz through the resampler (docs/nam.md). */
enum clone_kind { CLONE_ORIGINAL, CLONE_NAM, CLONE_NAM_48K };
enum clone_kind catalog_clone_kind(const struct catalog *c, int index);
