/*
 * Shared state of the screens and the screen interface.
 */
#pragma once

#include "engine/audio_engine.h"
#include "input/input.h"
#include "lvgl.h"
#include "model/assign.h"
#include "model/expression.h"
#include "model/globals.h"
#include "model/rig.h"
#include "model/settings.h"
#include "model/topology.h"
#include "ui/gesture.h"

enum screen_id { SCR_PRESET, SCR_STOMP, SCR_CHAIN, SCR_EDIT, SCR_BROWSE, SCR_SAVE, SCR_NAME, SCR_TUNER, SCR_SETTINGS,
                 SCR_EQ, SCR_ASSIGN, SCR_LOOPER, SCR_DRUM, SCR_SCENE, SCR_CONTROLS,
                 SCR_LINK, SCR_CONTROL_MAP, SCR_TARGET, SCR_LATENCY, SCR_MIC, SCR_EXP_CAL, SCR_PRESETS, SCR_MENU,
                 SCR_MODS, SCR_COUNT };

/* what the footswitches do: the preset's preset group, its stomp group, or the scenes (footswitch.c) */
enum fs_mode { FS_MODE_PRESET, FS_MODE_STOMP, FS_MODE_SCENE };

#define UI_STACK 8

struct ui_ctx;

/* learning: the footswitch (hold = it was held) or the pedal (CK_TOE, CK_EXP1A..CK_EXP2) the player uses */
typedef void (*ui_learn_fn)(struct ui_ctx *ui, int key, int hold);

struct ui_ctx {
    struct rig *rig;
    struct settings *settings;
    struct globals *globals;        /* shared with the original firmware (record 0) */
    struct expression *pedals;
    struct assign assign;
    struct topology topo;
    struct engine_stats stats;
    enum screen_id current;
    enum screen_id stack[UI_STACK]; /* screens to go back to (‹ / EXIT), the last one on top */
    int depth;
    int stomp_mode;                 /* footswitches toggle blocks instead of selecting presets */
    int scene_mode;                 /* footswitches A-D recall scenes (stomp_mode is 0 then) */

    int sel;                        /* selected chain position (chain and edit screens), count = "+" */
    int edit_row;                   /* edit screen: active row of three parameters */

    /* browse target: replace the effect of browse_slot, or when browse_slot < 0 a new block in grid cell
       browse_cell (>= 0) or at chain position browse_pos */
    int browse_slot, browse_pos, browse_cell;
    int browse_menu, browse_index;

    int save_target;                /* save screen: destination preset */
    int name_scene;                 /* name screen: the scene it names, -1 = the preset */
    int name_key;                   /* name screen: the footswitch whose card name it edits, -1 = none */
    int tempo_request;              /* BPM / VOL was tapped: the chain screen toggles tempo and volume */
    int edit_knob;                  /* edit screen: the last parameter of the active row turned (0..2) */
    int edit_scope;                 /* edit screen: the spectrum analyser on top, one row of parameters below */
    int scope_mode;                 /* its enum analyzer_mode: the block's input / output, or the preset's */
    int link_slot, link_param;      /* assign screen: the parameter (rig_param order) of a block */
    int controls_key, controls_mode, controls_link;   /* links screen: open on these, -1 / 0 = as it was */
    int mods_mod, mods_link;        /* modulators screen: open on modulator mods_mod (-1 = as it was), link selected */
    int target_link, target_new;    /* destination screen: the link (table index); new = dropped if left unset */
    int ctl_global;                 /* CONTROLES, a key's links, the destination and the card name edit the global table
                                       (GLOBAL › CONTROLES GLOBAIS) instead of the preset's */
    int global_dirty;               /* the global table changed: stored when those screens are left */
    int exp_cal;                    /* calibration screen: the pedal (0 = EXP 1, 1 = EXP 2) */
    int assign_row;                 /* assignments screen: open on this row (quick knob 0-2), -1 = as it was */
    int preset_cursor;              /* presets list: the preset under the cursor (its bank: the footswitches') */
    ui_learn_fn learned;            /* learning (ui_learn): footswitches and pedals go here instead of acting */

    uint32_t rev;                   /* bumped on every model change; screens redraw when it moves */
};

struct screen {
    void (*create)(struct ui_ctx *ui, lv_obj_t *root);
    void (*enter)(struct ui_ctx *ui);                               /* optional */
    void (*refresh)(struct ui_ctx *ui);
    int (*input)(struct ui_ctx *ui, const struct input_event *ev);  /* 1 = consumed */
    void (*leave)(struct ui_ctx *ui);                               /* optional */
    void (*tick)(struct ui_ctx *ui);                                /* optional, every loop while shown */
};

extern const struct screen screen_preset, screen_stomp, screen_chain, screen_edit, screen_browse, screen_save,
    screen_name, screen_tuner, screen_settings, screen_eq, screen_assign, screen_looper, screen_drum, screen_scene,
    screen_controls, screen_link, screen_control_map, screen_target, screen_latency, screen_mic, screen_exp_cal,
    screen_presets, screen_menu, screen_mods;

/* screen manager. The design's navigation: tabs (PRESET, STOMP, CENAS, CHAIN, TUNER) are the top level; a tab
   shows its screen and forgets the way back. Other screens are opened on top (ui_go) and ‹ / EXIT goes back
   one level (ui_back); holding EXIT goes home from anywhere. Home is the tab of the footswitch mode: only
   choosing a mode (its tab, EFX on a mode tab, the mode footswitch function, MIDI) changes what the
   footswitches do, never going back or home. */
void ui_go(enum screen_id id);                  /* open id, ‹ / EXIT come back here */
void ui_back(void);                             /* one level up; the home screen at the top */
void ui_tab(enum screen_id id);                 /* show a top-level screen */
void ui_changed(void);                          /* the rig changed: rebuild derived state, redraw */
void ui_toast(const char *text);
void ui_load_preset(int index);                 /* switch preset, reset the selections */
void ui_open_tuner(void);                       /* on top of the current screen */
void ui_open_save(void);                        /* the save screen for the current preset */
void ui_edit(int pos);                          /* edit the block at chain position pos ("+" = browse) */
void ui_name_scene(int n);                      /* the name screen for scene n */
void ui_name_key(int key);                      /* the name screen for footswitch key's card name */
enum screen_id ui_mode_tab(void);               /* the tab of the footswitch mode (PRESET, STOMP or SCENE): home */
void ui_set_mode(int mode);                     /* enum fs_mode; on a mode tab its tab is shown, elsewhere the
                                                   screen stays and a toast tells the mode */
struct ui_ctx *ui_context(void);                /* for code driven from outside the screens (MIDI) */
void ui_learn(ui_learn_fn fn);                  /* the next footswitch or pedal the player uses goes to fn (once;
                                                   0 = stop learning; showing another screen stops it too) */
void ui_link_param(int slot, int k);            /* what controls parameter k of slot (the assign screen) */
void ui_controls(int key, int mode, int link);  /* key's links in mode (0 = the footswitch mode), link selected;
                                                   a modulator's key opens the modulators screen */
void ui_mods(int mod, int link);                /* the modulators screen on modulator mod (0..3), link selected (-1) */
void ui_link_text(struct ui_ctx *ui, const struct control *l, char *out, unsigned size);   /* a link, one line */
const char *ui_key_name(int key);               /* "A", "BANK +", "A + B", "EXP 1A", "MIDI CC 80" */
void ui_target(int link, int is_new);           /* where link goes (the destination screen); is_new: a link just
                                                   added, removed again when left without choosing one */
void ui_global_controls(void);                  /* CONTROLES on the global table */
struct preset_controls *ui_ctl_table(struct ui_ctx *ui);   /* the table the control screens edit */
void ui_ctl_edited(struct ui_ctx *ui);           /* that table changed: the preset is EDITADO, or the global table
                                                   is stored when the control screens are left */
extern const uint8_t ui_functions[];            /* the functions a link can run, in the screens' order */
extern const int ui_function_count;

/* helpers shared by the screens */
const char *ui_slot_tag(const struct ui_ctx *ui, int slot);          /* "DRV", "SPLIT", ... */
void ui_slot_name(const struct ui_ctx *ui, int slot, char *out, unsigned size);
int ui_menus(const struct ui_ctx *ui, int *out);    /* categories with effects (LIB_MAX_MENUS), library order */
const lv_image_dsc_t *ui_menu_icon(int menu, int size);  /* the design's line icon, 15 or 20 px (A8: recolor) */
const char *ui_menu_name(int menu);                 /* "Drive", "Modulação" */
void ui_lane_name(const struct ui_ctx *ui, int pos, char *out, unsigned size);   /* "PRÉ · 1", "PISTA A · 2" */
void ui_cell_lane(const struct ui_ctx *ui, int cell, char *out, unsigned size);  /* the same for a grid cell */
void ui_preset_title(const struct ui_ctx *ui, char *out, unsigned size);   /* "01A CLEAN CRUNCH", "01A*" edited */
int ui_turned(const struct input_event *ev, int enc);                 /* detents of ENC_x, else 0 */
int ui_pressed(const struct input_event *ev, uint32_t button);
float ui_param_frac(const struct rig_param *p);
void ui_param_label(const struct rig_param *p, char *out, unsigned size);    /* upper-case name */

/* touch: the same actions as the controls */
void ui_press(uint32_t button);                                          /* as if BTN_x was pressed */
void ui_param_drag(struct ui_ctx *ui, int slot, int k, const struct gesture *g);   /* drag = knob turn */
void ui_volume_drag(struct ui_ctx *ui, const struct gesture *g);
void ui_bpm_drag(struct ui_ctx *ui, const struct gesture *g);
