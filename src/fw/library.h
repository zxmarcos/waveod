/*
 * Effect library (firmware section 1, flash 0x480000).
 *
 *   +0x00 u32 0          +0x04 u32 0x18 + 4 * ncat   +0x08 u32 index size   +0x0C u32 total size
 *   +0x10 u32 0          +0x14 u32 ncat (<= 16)      +0x18 u32 list offset[ncat]
 *   list:  u32 count, u32 entry offset[count]
 *   entry: u32 total, u32 header size, u32 id, i32 link id, u8 nparams, u8 subgroup, u8 flags[2],
 *          u16 name, u16 category (offsets into the string table that follows the parameters),
 *          nparams x {i16 -1, u16 name, f32 min, max, step, default, u8 idx, u8 f1, u8 f2, u8 f3},
 *          strings, 0xFF padding, then the module ELF (<= 32 KB) at `header size`.
 *
 * Menus ("categories" in the file) are the 16 effect families of the pedal; their order is fixed by the app.
 */
#pragma once

#include <stdint.h>

#define LIB_MAX_MENUS   16
#define LIB_MAX_PARAMS  15

enum lib_menu {
    MENU_DYN, MENU_FX, MENU_WAH, MENU_DRV, MENU_AMP, MENU_CAB, MENU_IR, MENU_EQ,
    MENU_MOD, MENU_DLY, MENU_RVB, MENU_CLONE, MENU_LOOP, MENU_SND, MENU_RTN, MENU_VOL
};

/* Ids of the parallel routing modules (examples/parallel in the research repository). */
#define ID_SPLIT  0x01000070u
#define ID_PATH_B 0x01000071u
#define ID_MERGE  0x01000072u

struct library {
    const uint8_t *base;
    uint32_t size;
    int menus;
    uint8_t wv;                 /* waveod's library (fw/wv_library.h): parameter records name options / units */
};

/* waveod's library: f1 of a parameter record says what its first field (an offset into the strings) names */
#define LIB_P_OPTIONS 0x40
#define LIB_P_UNIT    0x80

struct lib_entry {
    uint8_t wv;                 /* from waveod's library */
    uint32_t id;
    int32_t link_id;            /* default cab of an amp, -1 otherwise */
    const char *name;
    const char *category;       /* tab label of the entry's subgroup */
    uint8_t subgroup;
    uint8_t nparams;
    const uint8_t *params;      /* nparams records of 24 bytes */
    const uint8_t *strings;
    const uint8_t *elf;
    uint32_t elf_size;
};

/* Parameter flags f3 = display format, f2 = tempo sync link (0x1N value / 0x2N switch of param N). waveod's
   blocks (sdk/wvfx.h) may also name their values or give a unit; the original's entries never do. */
struct lib_param {
    const char *name;
    float min, max, step, def;
    uint8_t idx;                /* position in the slot's knob array */
    uint8_t f1, f2, f3;
    const char *options;        /* named values "Off\0On\0" (value 0, 1, ...), or 0 */
    const char *unit;           /* a template with one %.Nf, or 0 */
};

int lib_open(struct library *lib, const uint8_t *data, uint32_t max_size);
int lib_count(const struct library *lib, int menu);
int lib_entry(const struct library *lib, int menu, int index, struct lib_entry *out);
int lib_param(const struct lib_entry *e, int k, struct lib_param *out);

/* Index of `id` in `menu`, or -1. */
int lib_find(const struct library *lib, int menu, uint32_t id);

/* Menu and index of `id` in any menu; returns 0 when found. */
int lib_locate(const struct library *lib, uint32_t id, int *menu, int *index);

/* Short label of a menu ("DRV", "AMP", ...). */
const char *lib_menu_tag(int menu);
