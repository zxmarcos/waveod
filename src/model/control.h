/*
 * Footswitch controls at work (the table is fw/preset_format.h's struct preset_controls, the preset's or the
 * global one): what a key does in a footswitch mode. A key's links of one kind of press (CW_PRESS with CW_MOMENT, or CW_HOLD) form a group with one
 * state; the stateful links (blocks, parameters) follow it, the others (functions, scenes) are handed to the
 * caller, which owns the screens.
 */
#pragma once

#include "model/rig.h"

/* The table key's group (hold = the long press group; pedals and MIDI keys: CM_ALL, 0) runs from: the preset's when
   it has a link there (CA_NOTHING too), else the global one (r->global). Every function below goes by it. */
const struct preset_controls *control_table(const struct rig *r, int key, int mode, int hold);

/* Whether that group has links and they are the global table's. */
int control_from_global(const struct rig *r, int key, int mode, int hold);

/* functions and scenes of a pressed key, for the UI (footswitch.c) */
typedef void (*control_run_fn)(void *ctx, const struct control *link);

/* The state of key's group (mode = enum control_mode bit, hold = the long press group): 1 / 0, -1 = no
   stateful link. */
int control_state(const struct rig *r, int key, int mode, int hold);

/* A press (hold = the long press group): the group's state flips, then the functions and scenes run. Returns how
   many links acted. */
int control_press(struct rig *r, int key, int mode, int hold, control_run_fn run, void *ctx);

/* A key going down / up: its momentary links on / off (the functions and scenes among them run on the way
   down). Returns how many acted. */
int control_moment(struct rig *r, int key, int mode, int down, control_run_fn run, void *ctx);

/* The first link of key's press group (hold: its long press group) in `mode` with this action (CA_NONE = any),
   or 0. */
const struct control *control_first(const struct rig *r, int key, int mode, int hold, int action);

/* How many links of that group have this action (CA_NONE = any). */
int control_count(const struct rig *r, int key, int mode, int hold, int action);

/* ---------------------------------------------------------------- pedals */

/* A pedal (CK_EXP1A..CK_EXP2) at position 0..1 (heel..toe), `now` in ms: its parameter links glide to their
   values along their curves; its auto blocks go on when it leaves the heel. */
void control_exp(struct rig *r, int key, float pos, uint32_t now);

/* MIDI control change cc (CK_MIDI_CC_OK) at value 0..127 received: its links act (CK_MIDI in preset_format.h); the
   functions and scenes among them go to run. Returns how many acted (0: the CC keeps its fixed meaning). */
int control_midi(struct rig *r, int cc, int value, control_run_fn run, void *ctx);

/* Auto blocks of pedals left at the heel for AUTO_OFF_MS go off. Returns 1 when one did. */
int control_exp_tick(struct rig *r, uint32_t now);

/* EXP 1 changes group: the blocks of the group left that follow it (block links with arg 0, and the wah /
   volume blocks of its parameter links, like the original) go off, those of the new group on. */
void control_exp_group(struct rig *r, int from_key, int to_key);

/* A curve at x in 0..1. */
float control_curve(int curve, float x);

/* The knob position a parameter link moves (CF_ORDER: converted from library order), or -1. */
int control_knob(const struct rig *r, const struct control *link);

/* ---------------------------------------------------------------- editing */

/* Make link (its key and slot set) a parameter link to parameter k (rig_param order) of its block, with the
   default values: a pedal sweeps the whole range heel to toe, a key goes from the value now (OFF) to the other end
   of the range (ON). -1 when the block has no such parameter. */
int control_set_param(const struct rig *r, struct control *link, int k);

/* A new parameter link at the end of the table: key (pedals and the toe switch: every mode, pressed; otherwise in
   `modes`, on `when`) moves parameter k of slot, with the default values. Its index, or -1 (full table, no such
   parameter). */
int control_add_param(struct rig *r, int key, int modes, int when, int slot, int k);

/* Make link a MIDI out link with the default values: a control change (the key's own number for a MIDI key, else
   CC 80) 0 / 127, or program change 0; on channel 1 unless it was a MIDI out link already. */
void control_set_midi(struct control *link, int program);

/* The parameter (rig_param order) a parameter link moves, its definition in *out; -1 = none. */
int control_param_index(const struct rig *r, const struct control *link, struct rig_param *out);

/* The first parameter link from table index `from` on that moves knob position `knob` of slot, or -1. */
int control_find_param(const struct rig *r, int slot, int knob, int from);
