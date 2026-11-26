/*
 * Stored preset (0x48C bytes), the same record in the factory bank and in the record store:
 *   +0x000 u16 0x5AA5   +0x002 u16 1   +0x004 u16 0xFFFF   +0x006 u16 0x48C (record size)
 *   +0x008 u16 preset number   +0x00A name[17]   +0x01E u8 volume   +0x01F i8 pan
 *   +0x020 u16 BPM      +0x022 i8 order[12] (slot of each chain position, -1 = end)
 *   +0x030 12 x {u8 on, i8 menu, i16 index, u32 id, f32 knobs[15]}   (empty slot: menu -1, index -1, id 0)
 *   +0x360 i8 quick knob target[3] (slot 0..11, QK_BPM, QK_VOLUME, -1)   +0x363 i8 quick knob parameter[3]
 *   +0x366 FX loop mode      +0x367 EXP 1 group (0 = A, 1 = B)
 *   +0x368 9 x {i8 slot, i8 parameter, u8 pad[2], f32 min, f32 max}: 3 targets of EXP 1A, EXP 1B, EXP 2
 *   +0x3D4 5 x {u8 member[12] (1 = the slot belongs to the CTRL), u8 display[12]}: CTRL 1-4 and the preset
 *          CTRL (the original rewrites `display` on load)
 *   +0x44D 2 x 26 footswitch functions (group 0 = stomp mode, 1 = preset mode): {press, hold} of FS A, B, C, D,
 *          BANK-, BANK+ and four more, then the combos A+B, B+C, C+D, BANK-+BANK+ and two more
 *   +0x481 style[10]
 * Parameters are in library order (the editor protocol's "order", not the knob position). Layout from the
 * original's preset import (FUN_0002E110) and the editor's preset model.
 * The record a preset was loaded from is kept (`raw`), so saving changes only the decoded fields.
 */
#pragma once

#include <stdint.h>

#define PRESET_COUNT        240
#define PRESET_SLOTS        12
#define PRESET_KNOBS        15
#define PRESET_RECORD_SIZE  0x48C
#define PRESET_NAME_LEN     17

#define QUICK_KNOBS   3
#define QK_BPM        12                /* quick knob targets after the 12 slots */
#define QK_VOLUME     13
#define EXP_GROUPS    3                 /* EXP 1A, EXP 1B, EXP 2 */
#define EXP_TARGETS   3
#define CTRL_COUNT    5                 /* CTRL 1-4 (footswitch functions) and the preset CTRL */
#define FS_GROUPS     2
#define FS_ENTRIES    26
#define FS_KEYS       10                /* entries 2k / 2k+1 = press / hold of key k */
#define FS_COMBO      20                /* entries 20.. = combos */

enum fs_group { FS_STOMP, FS_PRESET };

/* footswitch functions (the original's FUN_0002693C) */
enum fs_function {
    FSF_NONE = 0, FSF_CTRL1 = 1, FSF_CTRL4 = 4, FSF_LOOPER = 5, FSF_DRUM = 6, FSF_TUNER = 7, FSF_TAP = 8,
    FSF_BANK_UP = 9, FSF_BANK_DOWN = 10, FSF_PRESET_UP = 11, FSF_PRESET_DOWN = 12, FSF_PRESET_A = 13,
    FSF_PRESET_D = 16, FSF_BANK_MENU = 17, FSF_DRUM_NEXT = 18, FSF_DRUM_PREV = 19, FSF_EXP_AB = 20,
    FSF_MODE = 23, FSF_SLOT1 = 24, FSF_SLOT12 = 35,
};

struct exp_target {
    int8_t slot;                /* -1 = none */
    int8_t param;
    float min, max;
};

/*
 * waveod's routing (not in the original): the blocks sit in a grid of 2 rows x 6 columns (the 12 slots); in
 * series the first row continues in the second, in parallel the first row's columns split..merge-1 are lane A,
 * the whole second row is lane B (the first row before the split is PRE, after the merge POST), in dual L/R lane
 * A runs from the split to the end of the first row and goes to the left output, the second row to the right one. Stored in the CTRL 1 / CTRL 2 `display` bytes, which the
 * original fills itself when it loads a preset: there it plays the chain in series, in `order`.
 */
#define ROUTE_COLS   6
#define ROUTE_CELLS  (2 * ROUTE_COLS)       /* cell = row * ROUTE_COLS + column */

enum route_mode { ROUTE_SERIAL, ROUTE_PARALLEL, ROUTE_DUAL, ROUTE_MODES };
enum route_joint { JOINT_BALANCE, JOINT_LEVEL_A, JOINT_LEVEL_B, JOINT_MIX, JOINT_PAN_A, JOINT_PAN_B, JOINT_PARAMS };

struct preset_route {
    uint8_t stored;                         /* read from the record (else laid out from `order`) */
    uint8_t mode;                           /* enum route_mode */
    uint8_t split;                          /* first column of lane A, 0..5 */
    uint8_t merge;                          /* column after lane A, split+1..6 (parallel) */
    int8_t cell[PRESET_SLOTS];              /* each used slot's cell, -1 = none */
    uint8_t joint[JOINT_PARAMS];            /* 0..100: split balance, level A, level B; merge mix, pan A, pan B */
};

/*
 * waveod's scenes (not in the original; stored only in waveod's own format, fw/wv_preset.c): states of the same
 * chain that a footswitch recalls without reloading it. Each scene has which blocks are on and the values of the
 * parameters marked as scene parameters; every other parameter is the same in all of them. The preset's slots
 * always hold the active scene (`on`, `knobs`). A preset without scenes has the four equal to its blocks.
 */
#define SCENE_COUNT     4
#define SCENE_NAME_LEN  10
#define SCENE_PARAMS    32

struct scene_param {
    int8_t slot;
    int8_t knob;                            /* knob position (struct preset_slot `knobs`) */
    float value[SCENE_COUNT];
};

struct preset_scenes {
    uint8_t active;                         /* the scene played: the one saved with the preset */
    uint8_t nparams;
    uint16_t on[SCENE_COUNT];               /* bit s = slot s on */
    char name[SCENE_COUNT][SCENE_NAME_LEN + 1];     /* "" = "CENA n" */
    struct scene_param param[SCENE_PARAMS];
};

/*
 * waveod's footswitch controls (fw/preset_controls.c; chunk 11 of waveod's format): a table of links "key ->
 * action" for each footswitch mode. They replace the original's footswitch functions and CTRL groups (`fs`,
 * `ctrl`, kept only as what a preset without the table is converted from).
 *
 * A key's links that change state (blocks, parameters) share the key's state: a press flips it and every link
 * follows (block on / off, or inverted; parameter at its `on` / `off` value); momentary links hold it on while the
 * key is down. The others (functions, scenes) just run. The state is read back from the first stateful link (the
 * block's switch, the parameter's value), so nothing about it is stored.
 *
 * The same table also holds the global controls (fw/presets.c keeps it; only functions, scenes and MIDI out
 * there). A key's group (key, mode, the press or the long press links) runs from the preset's table when that has
 * a link there, otherwise from the global one (model/control.c); CA_NOTHING keeps a key empty in a preset.
 */
#define CONTROL_MAX     48
#define CONTROL_LABEL   8
#define CK_KEYS         6                   /* A, B, C, D, BANK -, BANK + */

enum control_key {
    CK_A, CK_B, CK_C, CK_D, CK_BANK_DOWN, CK_BANK_UP,
    CK_COMBO,                               /* .. +3: A+B, B+C, C+D, BANK- + BANK+ */
    CK_AGAIN = CK_COMBO + 4,                /* preset mode: the active preset's key again (the preset CTRL) */
    CK_TOE,                                 /* EXP 1's toe switch (a key in every mode) */
    CK_EXP1A,                               /* the pedals: EXP 1 in group A / B, EXP 2 (every mode) */
    CK_EXP1B,
    CK_EXP2,
    CK_MOD1,                                /* .. +3: the preset's modulators (struct preset_mods), every mode */
    CK_COUNT = CK_MOD1 + 4
};
#define CK_IS_PEDAL(k) ((k) >= CK_EXP1A && (k) <= CK_EXP2)
/* A modulator moves its parameter links like a pedal, from `off` (its 0) to `on` (its 1) along the curve, in the
   audio interrupt (engine/modulators.h); the preset's table only (a global link has no modulator to follow). */
#define CK_IS_MOD(k) ((k) >= CK_MOD1 && (k) < CK_MOD1 + 4)
/* A MIDI control change received, CK_MIDI + its number (every mode, like the pedals): a parameter or a MIDI out link
   follows its value (0..127 as heel..toe), a block is on from 64 up (arg 1: off), the rest run from 64 up. A CC with
   links in the preset does that instead of its fixed meaning (ui/midi_control.c). Not bank select (0) and the tempo
   pair (68 / 69). */
#define CK_MIDI 0x80
#define CK_IS_MIDI(k) ((k) >= CK_MIDI)
#define CK_MIDI_CC_OK(cc) ((cc) > 0 && (cc) < 128 && (cc) != 68 && (cc) != 69)
#define CK_VALID(k) ((k) < CK_COUNT || (CK_IS_MIDI(k) && CK_MIDI_CC_OK((k) - CK_MIDI)))
#define CK_SWEEPS(k) (CK_IS_PEDAL(k) || CK_IS_MIDI(k) || CK_IS_MOD(k))    /* a position, not a press: values heel..toe, a curve */
enum control_mode { CM_PRESET = 1, CM_STOMP = 2, CM_SCENE = 4, CM_ALL = 7 };   /* bits */
enum control_when { CW_PRESS, CW_HOLD, CW_MOMENT };    /* released (not held) / held / while down */
enum control_action {
    CA_NONE,
    CA_FN,                                  /* arg = enum fs_function (tuner, tap, presets, looper...) */
    CA_BLOCK,                               /* slot on with the key; arg 1 = inverted (off with the key); on a pedal:
                                               arg 0 = on with its EXP 1 group, 1 = on when it moves (auto) */
    CA_PARAM,                               /* slot, arg = knob position: `on` / `off` value with the key; on a
                                               pedal `off` at the heel, `on` at the toe, along `curve` */
    CA_SCENE,                               /* arg = scene, SCENE_NEXT, SCENE_PREV */
    CA_MIDI,                                /* MIDI out on both ports: slot = channel 0..15, arg = CC number, `off` /
                                               `on` its values (state like a block's: a press flips it, momentary
                                               holds it; a pedal sweeps it); CF_PC: program change arg, sent on the
                                               press */
    CA_NOTHING,                             /* the key does nothing here: not even the global links (a preset's
                                               table only) */
    CA_COUNT
};
#define SCENE_NEXT 0xfe
#define SCENE_PREV 0xfd

struct control {
    uint8_t key;                            /* enum control_key */
    uint8_t modes;                          /* enum control_mode bits */
    uint8_t when;                           /* enum control_when */
    uint8_t action;                         /* enum control_action */
    int8_t slot;
    uint8_t arg;
    uint8_t curve;                          /* pedals and MIDI keys: enum control_curve */
    uint8_t flags;                          /* CF_ORDER, CF_PC */
    float off, on;                          /* CA_PARAM, CA_MIDI */
};

enum control_curve { CC_LINEAR, CC_SLOW, CC_FAST, CC_COUNT };   /* slow: fine near the heel; fast: near the toe */
#define CF_ORDER 1                          /* CA_PARAM: arg is the parameter in library order (converted EXP targets) */
#define CF_PC    2                          /* CA_MIDI: a program change, not a control change */
#define AUTO_OFF_MS 500                     /* an auto block goes off after this long at the heel */

struct preset_controls {
    uint8_t count;
    uint8_t colour[CK_KEYS];                /* LED / card colour in stomp mode, 0 = from the block */
    char label[CK_KEYS][CONTROL_LABEL + 1]; /* name on the stomp card, "" = from the links */
    struct control c[CONTROL_MAX];
};

/*
 * waveod's modulators (chunk 12 of waveod's format; engine/modulators.h runs them): four per preset, each the
 * key CK_MOD1 + n of the control table, whose parameter links it moves between their `off` and `on` values.
 *   LFO   a shape repeating at `rate` Hz or, synced, once per note division of the tempo; restarts at `phase`
 *         when the preset loads
 *   ENV   the guitar's level at the chain's input: `sens` dB more, then -60 dB .. 0 dB as 0..1, rising in
 *         `attack` ms, falling in `release` ms
 *   SEQ   `steps` values (0..100) one after the other, a step per 1 / rate s or per division; `glide` (% of a step)
 *         slides into each
 */
#define MOD_COUNT 4
#define MOD_STEPS 16
enum mod_type { MOD_OFF, MOD_LFO, MOD_ENV, MOD_SEQ, MOD_TYPES };
enum mod_shape { MS_SINE, MS_TRIANGLE, MS_SQUARE, MS_SAW_UP, MS_SAW_DOWN, MS_RANDOM, MS_SMOOTH, MS_SHAPES };

struct preset_mod {
    uint8_t type;                       /* enum mod_type */
    uint8_t shape;                      /* LFO: enum mod_shape */
    uint8_t sync;                       /* 0 = rate in Hz, 1..11 = note division (param_sync_divisions) + 1 */
    uint8_t steps;                      /* SEQ: 1..MOD_STEPS */
    float rate;                         /* Hz, 0.05..20 (an LFO cycle, a SEQ step) */
    uint8_t phase;                      /* LFO: where it starts, % of a cycle */
    uint8_t glide;                      /* SEQ: % of a step */
    int8_t sens;                        /* ENV: dB, -20..40 */
    uint8_t pad;
    uint16_t attack, release;           /* ENV: ms */
    uint8_t step[MOD_STEPS];            /* SEQ: 0..100 */
};

struct preset_mods {
    struct preset_mod m[MOD_COUNT];
};

/* A modulator as a new one of `type` is: 1 Hz, a sine; 10 / 200 ms; 8 steps rising from 0 to 100. */
void preset_mod_default(struct preset_mod *m, int type);
int preset_mods_used(const struct preset_mods *mods);

struct preset_slot {
    uint8_t on;
    int8_t menu;                /* -1 = empty slot */
    int16_t index;              /* index in the menu when saved (re-resolved by id on load) */
    uint32_t id;
    float knobs[PRESET_KNOBS];
};

struct preset {
    char name[PRESET_NAME_LEN + 1];
    char style[11];
    uint8_t volume;
    int8_t pan;
    uint16_t bpm;
    uint8_t count;                      /* chain length */
    int8_t order[PRESET_SLOTS];         /* slot of each chain position */
    struct preset_slot slot[PRESET_SLOTS];
    int8_t qk_target[QUICK_KNOBS];      /* slot, QK_BPM, QK_VOLUME, -1 */
    int8_t qk_param[QUICK_KNOBS];
    uint8_t fx_loop;
    uint8_t exp_group;                  /* EXP 1: 0 = A, 1 = B */
    struct exp_target exp[EXP_GROUPS][EXP_TARGETS];
    uint8_t ctrl[CTRL_COUNT][PRESET_SLOTS];
    int8_t fs[FS_GROUPS][FS_ENTRIES];
    struct preset_route route;
    struct preset_scenes scenes;
    struct preset_controls controls;
    struct preset_mods mods;            /* waveod's modulators (chunk 12) */
    uint8_t raw[PRESET_RECORD_SIZE];    /* the stored record, for the fields not decoded */
};

/* The controls of a preset that has no table: from the original's footswitch functions and CTRL groups (`fs`,
   `ctrl`), plus scene mode (A-D scenes 1-4, BANK -/+ previous / next preset, the pairs as in preset mode). The
   groups that are what the factory global table does are left to it (dropped), those empty here while it has
   links there get a CA_NOTHING link: with the factory global table a preset plays as the original did. */
void preset_controls_legacy(const struct preset *p, struct preset_controls *out);

/* Everything the original's fields give, the factory functions included (what preset_controls_legacy leaves
   to the global table). */
void preset_controls_original(const struct preset *p, struct preset_controls *out);

/* The factory global table: what the factory presets' footswitch functions do in every preset (preset mode A-D =
   preset A-D, BANK -/+ = bank, the pairs tuner / looper / next mode; scene mode A-D = scenes 1-4, BANK -/+ =
   preset; the toe switch = EXP A/B). */
void preset_controls_factory(struct preset_controls *out);

/* Whether a link may be global: functions, scenes, MIDI out (nothing tied to a preset's blocks). */
int preset_control_global_ok(const struct control *link);

/* Forget the links to `slot`: all of them (removed), or only the parameter ones (another effect there). */
void preset_controls_forget(struct preset *p, int slot, int params_only);

/* Add a link; -1 when the table is full. */
int preset_control_add(struct preset_controls *c, const struct control *link);
void preset_control_remove(struct preset_controls *c, int i);

/* Scenes of a preset that has none: all four the blocks as they are, no scene parameters, no names. */
void preset_scenes_reset(struct preset *p);

/* Scene parameter entry of (slot, knob), or -1. */
int preset_scene_param(const struct preset *p, int slot, int knob);

/* Forget the scene parameters of `slot` (its effect changed or it was removed). */
void preset_scene_forget(struct preset *p, int slot);

/* Whether the scenes are more than the reset state (wv_preset.c stores them only then). */
int preset_scenes_used(const struct preset *p);

/* The original's preset template (fw/preset_default.c, generated by tools/gen_defaults.py). */
extern const uint8_t preset_template[PRESET_RECORD_SIZE];

/* Forget every assignment (quick knobs, expression targets, CTRL membership, scene parameters, footswitch links)
   that points at `slot`, as the original does when a block is removed (FUN_00021138). */
void preset_unassign_slot(struct preset *p, int slot);

/* Decode a stored record; returns -1 when the magic is wrong. */
int preset_decode(const uint8_t *rec, struct preset *out);

/* Record for preset number `index`: `raw` with the decoded fields written over it. */
void preset_encode(const struct preset *p, int index, uint8_t out[PRESET_RECORD_SIZE]);

/* The routing of a preset without one: the chain in series, `order` filling the grid row by row. */
void preset_route_default(const struct preset *p, struct preset_route *r);

/* Empty preset (no effects). */
void preset_clear(struct preset *p, const char *name);

/* "01A" style label of a preset number (4 per bank, banks 01..60). */
void preset_label(int index, char out[8]);
