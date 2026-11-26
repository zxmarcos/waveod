/*
 * The rig: the active preset as the user edits it, kept in sync with the running chain.
 *
 * Knob and on/off changes go straight to the chain slot the audio interrupt reads; structural changes
 * (replace / insert / remove an effect) rebuild the chain like the original app does. Edits stay in RAM until
 * rig_save writes the preset to the flash, in the original's format.
 */
#pragma once

#include "engine/chain.h"
#include "fw/catalog.h"
#include "fw/presets.h"

#define RIG_NOTICE_LEN 48

/* What changed in the rig, for whoever mirrors it (editor/server.c tells the PC editor, like the original's
   notifications). a / b as noted. */
enum rig_event {
    RIG_EV_LOAD,            /* a = preset index */
    RIG_EV_PARAM,           /* a = slot, b = parameter (library order) */
    RIG_EV_ON,              /* a = slot */
    RIG_EV_ALL_ON,          /* several slots switched at once (CTRL) */
    RIG_EV_CHAIN,           /* a = slot that changed, -1 = order only */
    RIG_EV_CHAIN_FULL,      /* insert refused: the chain is full */
    RIG_EV_ASSIGN,          /* a block's quick knob / EXP / CTRL assignments were forgotten or checked */
    RIG_EV_DIRTY,           /* a = new state of the modified flag */
    RIG_EV_VOLUME,
    RIG_EV_EXP,             /* a = expression group moved (0, 1 = EXP 1, 2 = EXP 2) */
    RIG_EV_SAVED,           /* a = preset index */
    RIG_EV_SCENE,           /* a = active scene (switched, or its scene parameters changed) */
};

struct rig;
typedef void (*rig_listener)(struct rig *r, enum rig_event ev, int a, int b);

struct rig {
    struct catalog cat;
    struct preset_source src;
    struct chain chain;
    struct chain tail[2];           /* spillover: the chain that played, by its side of the pools */
    struct preset preset;           /* resolved copy of the active preset */
    int index;                      /* 0..239 */
    enum preset_origin origin;
    uint8_t dirty;
    int failed;                     /* slots whose module did not load (e.g. no memory) */
    char notice[RIG_NOTICE_LEN];    /* last message for the UI, cleared by the reader */
    uint16_t global_bpm;            /* the global tempo when it is on, else 0 (rig_set_global_tempo) */
    rig_listener listener;
    uint8_t quiet;                  /* events held back (expression pedal moves) */
    uint8_t performing;             /* parameter moves that are not edits (expression pedal): scenes keep theirs */
    uint32_t heel_at[3];            /* pedal links (CK_EXP1A..CK_EXP2): when it came to the heel, 0 = not there */
    uint8_t midi_sent[CONTROL_MAX]; /* MIDI out links (by table index): the CC value sent last + 1, 0 = none yet */
    struct preset_controls global;  /* the global control table (fw/presets.c; model/control.c runs it) */
    uint8_t global_sent[CONTROL_MAX];   /* as midi_sent, for the global table (kept across preset loads) */
    uint8_t scene_mode;             /* footswitches in scene mode: block switches go to the active scene even while the
                                       scenes are all alike (otherwise those keep the four alike) */
    uint8_t scene_auto;             /* a parameter edited while the scenes are in play (rig_scene_editing) becomes a
                                       scene parameter (the setting KNOBS NA CENA: AUTO) */
    uint8_t preset_wide;            /* parameter moves that stay the preset's (a pasted block, footswitch links) */
    uint8_t scene_chosen;           /* a scene was chosen to edit (the scene menu): the scenes are in play on this
                                       preset even while they are all alike (cleared by a preset load) */
    uint32_t gen;                   /* counts every change told (the UI redraws when it moves) */
    uint32_t loads;                 /* counts preset loads, wherever they came from (pedal, editor, MIDI) */
};

void rig_listen(struct rig *r, rig_listener fn);

/* the modified flag (r->dirty) as the original keeps it: setting it tells the listener when it changes */
void rig_set_dirty(struct rig *r, int dirty);

/* Open the library and load the power-up preset, unless it is `avoid` (RIG_AVOID_ANY: whichever it is): then
   the rig starts at its place on an empty chain named "MODO SEGURO" (after a crash in it, or on request).
   Returns 0, 1 = started that way, -1 = no effect library. */
#define RIG_AVOID_NONE (-1)
#define RIG_AVOID_ANY  (-2)
int rig_init(struct rig *r, int avoid);
void rig_load(struct rig *r, int index);

/* Store the global control table (r->global; fw/presets.c), then read it back. 0 = written. Blocks while the
   flash is written. */
int rig_store_global(struct rig *r);

/* Effect name of `slot` in stored preset `index` (not the active one), and its chain position (*pos, -1 = not
   in the chain). 0 = found. */
int rig_slot_name_in(const struct rig *r, int index, int slot, int *pos, char *out, unsigned size);

/* A stored preset as the rig would play it: each slot's effect found again by its id (the menu / index it was
   saved with may have moved, an unknown one falls back like the original's), chain positions of empty slots
   dropped. rig_load does this to the preset it loads. */
void rig_resolve(const struct rig *r, struct preset *p);

/* Slot queries (slot = 0..11, the storage index, not the chain position) */
int rig_slot_used(const struct rig *r, int slot);
int rig_slot_item(const struct rig *r, int slot, struct catalog_item *out);
int rig_is_routing(const struct rig *r, int slot);      /* Split, Path B or Merge */

/* Parameters of a slot, in library order (k = 0..count-1) */
struct rig_param {
    struct lib_param def;           /* min/max/step replaced by the note divisions when tempo-synced */
    float value;
    int synced;
    char text[24];                  /* formatted like the pedal shows it */
};
/* Index of the first parameter of the virtual mic (engine/cabmic.h: CABMIC_PARAMS of them, after the library
   entry's) of a Cab / User IR slot on waveod's engine, or -1. */
int rig_mic_first(const struct rig *r, int slot);

int rig_param_count(const struct rig *r, int slot);
int rig_param(const struct rig *r, int slot, int k, struct rig_param *out);
void rig_param_nudge(struct rig *r, int slot, int k, int steps);

/* Set parameter k to `value`, quantized to its step and limited to its range like the original's setter
   (FUN_000213CC); a tempo-sync switch moves its value parameter like a nudge does. */
void rig_param_set(struct rig *r, int slot, int k, float value);

/* Tempo the effects follow: the global tempo when it is on, otherwise the preset's BPM. */
void rig_set_global_tempo(struct rig *r, uint16_t bpm);    /* 40..300, 0 = off */
uint16_t rig_tempo(const struct rig *r);
void rig_set_bpm(struct rig *r, int bpm);                  /* the preset's, 40..300 */

/* A block on / off (no change: nothing told). */
void rig_set_on(struct rig *r, int slot, int on);

/* The parameter at knob position `knob` of `slot` to `value`, as rig_param_set. */
void rig_knob_set(struct rig *r, int slot, int knob, float value);

/* Knob position of parameter k (library order), or -1. */
int rig_param_knob(const struct rig *r, int slot, int k);

/* A pedal move: the parameter at knob position `knob` to `value` (in its range and steps), gliding over a few
   audio blocks when it has many steps; not an edit, scenes untouched. */
void rig_knob_glide(struct rig *r, int slot, int knob, float value);

/* Expression pedal: the links of `group` (0 = EXP 1A, 1 = EXP 1B, 2 = EXP 2) at position 0..1 (model/control.c;
   the original's FUN_000201D8). */
void rig_exp(struct rig *r, int group, float pos);

/* EXP 1 A / B (FUN_0001FFBC): the blocks that follow the group (and the wah / volume blocks of its parameter
   links) of the group left are switched off, those of the new group on. */
void rig_exp_group(struct rig *r, int group);

void rig_toggle(struct rig *r, int slot);

/*
 * Scenes (fw/preset_format.h): switching one puts its blocks on / off and its scene parameters at their values,
 * without reloading the chain and without marking the preset as edited. While a scene is active, switching a
 * block (touch, footswitch, CTRL, MIDI) and turning a scene parameter change that scene only; the other
 * parameters belong to the preset and are the same in every scene.
 */
int rig_scene(struct rig *r, int n);                    /* 0..SCENE_COUNT-1; 0 = switched */
int rig_scene_active(const struct rig *r);
/* The scenes are in play (scene mode, the preset's scenes differ, or r->scene_chosen): edits go to the active
   scene, block switches included (otherwise those keep the four scenes alike). With
   r->scene_auto, turning a parameter then makes it a scene parameter (up to SCENE_PARAMS). */
int rig_scene_editing(const struct rig *r);
/* Scene n back to how the stored preset has it (blocks, scene parameter values, name); blocks and scene
   parameters that are not in the stored preset keep theirs. 0 = done, -1 = nothing stored. */
int rig_scene_revert(struct rig *r, int n);
/* Whether parameter k of `slot` is a scene parameter. */
int rig_scene_param(const struct rig *r, int slot, int k);
/* Make it one (every scene starts at its current value) or the preset's again (at the active scene's value).
   Returns the new state, -1 = not possible (the notice says why). */
int rig_scene_param_toggle(struct rig *r, int slot, int k);
void rig_scene_set_name(struct rig *r, int n, const char *name);    /* up to SCENE_NAME_LEN characters */
/* "CENA 2" or the scene's own name */
void rig_scene_name(const struct rig *r, int n, char out[SCENE_NAME_LEN + 1]);
/* Copy scene `from` (blocks on / off, scene parameter values) over scene `to`; marks the preset as edited. */
void rig_scene_copy(struct rig *r, int from, int to);

/* After a CPU overload (engine_overload_take): the engine switched every slot off, the preset follows and
   counts as edited, like the original (FUN_00022514). */
void rig_overload_bypass(struct rig *r);
void rig_set_name(struct rig *r, const char *name);     /* up to PRESET_NAME_LEN characters, saved by rig_save */
void rig_set_volume(struct rig *r, int volume);

/* Structural edits: chain position `pos`, catalog item (menu, index). Return 0 or -1 (notice says why). */
int rig_replace(struct rig *r, int slot, int menu, int index);
int rig_insert(struct rig *r, int pos, int menu, int index);
int rig_remove(struct rig *r, int pos);
int rig_move(struct rig *r, int from, int to);          /* `to` counts without the moved block */

/* The routing grid (model/route.h). A new block in an empty cell: returns its slot, or -1. */
int rig_place(struct rig *r, int cell, int menu, int index);
/* A block to another cell; with a block there the two swap places. */
int rig_move_cell(struct rig *r, int slot, int cell);
/* Series / parallel / dual and where the split and merge are (kept in range). 1 = changed. */
int rig_route(struct rig *r, int mode, int split, int merge);
/* A split / merge parameter (enum route_joint), 0..100. */
void rig_joint(struct rig *r, int k, int value);

/* Load the chain's modules again (an IR or capture they read was rewritten: FUN_00020424(2)). */
void rig_rebuild(struct rig *r);

/* waveod's effect library (fw/wv_library.h) is about to be rewritten: the catalog stops reading it. Then, once
   written (or failed), it is read again, the edited preset's effects found again by id and the chain rebuilt
   (the edits are kept). */
void rig_library_writing(struct rig *r);
void rig_library_changed(struct rig *r);

/* Save the edited preset as preset `index` (0..239, a user preset from then on) and make it the active one.
   The audio is muted while the flash is written (well under a second). 0 = saved and verified. */
int rig_save(struct rig *r, int index);

/* Swap places: the edited preset is saved as preset index (and stays active there), what was stored at index`n   goes where the edited preset came from (an empty place leaves an empty preset). 0 = both written. */
int rig_swap(struct rig *r, int index);
