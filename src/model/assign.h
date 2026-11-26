/*
 * What the performance controls act on for the current preset, from the preset's own assignments
 * (fw/preset_format.h), as the original uses them:
 *   - quick knobs 1..3: a parameter of a block, the preset BPM or the preset volume;
 *   - footswitches A..D, BANK -, BANK +: the function of each in the stomp group (CTRL 1-4 switch their member
 *     blocks, tap tempo...); `slot` is the block a card shows (the first member of a CTRL, in chain order).
 */
#pragma once

#include "model/rig.h"

#define STOMP_COUNT 6           /* the original's key order: A, B, C, D, BANK -, BANK + */
#define QUICK_COUNT QUICK_KNOBS

#define QUICK_VOLUME (-1)       /* the preset volume */
#define QUICK_NONE   (-2)
#define QUICK_BPM    (-3)       /* the preset tempo */

struct quick_knob {
    int8_t slot;            /* or QUICK_VOLUME / QUICK_BPM / QUICK_NONE */
    int8_t k;               /* parameter (library order) */
};

struct stomp_key {
    int8_t fn;              /* first function link (enum fs_function), FSF_NONE */
    int8_t scene;           /* first scene link (scene, SCENE_NEXT / PREV as int8), -1 */
    int8_t slot;            /* block shown, -1 = none */
    int8_t param;           /* the first parameter link's parameter (library order) when no block link, -1 */
    uint8_t members;        /* block and parameter links */
    int16_t midi;           /* first MIDI out link: its number, + 0x100 for a program change; -1 = none */
    int8_t state;           /* the key's state: 1 / 0, -1 = no block or parameter link */
};

struct assign {
    struct stomp_key stomp[STOMP_COUNT];
    struct quick_knob quick[QUICK_COUNT];
};

void assign_build(const struct rig *r, struct assign *a);

/* The footswitch function d steps after cur in the list offered (the editor's order, then the others). */
int assign_fn_step(int cur, int d);

/* Short name of a footswitch function, for the cards ("CTRL 1", "TAP", "AFINADOR"...). */
const char *assign_fn_name(int fn);
