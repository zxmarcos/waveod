/*
 * A key's links (a card of CONTROLES, scr_control_map.c; EXP): what a footswitch does in a footswitch mode, or
 * what the toe switch and the expression pedals move, as the preset's control table (fw/preset_format.h,
 * model/control.c); where each goes is chosen on the destination screen (scr_target.c). Not in the design; a
 * list like the settings.
 *
 * Rows (knob 1 / MENU press):
 *   TECLA              key (A..D, BANK -/+, the pairs, in preset mode the active preset's key again, the toe
 *                      switch, EXP 1A / 1B / 2, MIDI CC 1..127; the modulators have their own screen,
 *                      scr_mods.c) / learn it: the next footswitch pressed, pedal
 *                      moved or MIDI CC received
 *   NOME NO CARD       - / the name keyboard (A..D, BANK -/+)
 *   COR                automatic or a LED colour
 *   one row per link   - / where it goes (the destination screen)
 *   + NOVA LIGAÇÃO     - / a new one, on the destination screen
 * MENU hold removes the selected link; EFX = the mode edited (preset, stomp, scenes); EXIT stops learning, else
 * goes back.
 * Touch: tap a row to select it (tap again = MENU press), drag it sideways = knob 1, hold a link = remove it.
 * Changes are part of the preset (EDITADO) and saved with it; on the global table (GLOBAL › CONTROLES GLOBAIS,
 * ui->ctl_global) they are stored when the control screens are left. On a preset, the global links of the groups it
 * has no links in follow its own as GLOBAL rows: MENU press copies the group into the preset (then its destination),
 * MENU hold gives the group a CA_NOTHING link.
 */
#include "model/control.h"
#include "model/param_format.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "ui/footswitch.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/row_list.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define ROW_H    32
#define DRAG_PX  24

enum { R_KEY, R_NAME, R_COLOUR, R_LINKS };

static struct {
    struct row_list list;
    char titles[ROW_LIST_MAX][24];
    int key;                                /* enum control_key */
    int mode;                               /* enum control_mode bit edited */
    int links[CONTROL_MAX], nlinks;         /* table entries of the key, in the rows' order */
    uint8_t from_global[CONTROL_MAX];       /* that row is a global link (a group the preset has no links in) */
    int global;                             /* editing the global table (GLOBAL › CONTROLES GLOBAIS) */
    int first;                              /* the first link row (R_LINKS) */
    int drag_start;
} s SDRAM_BSS;

static const char *key_name(int key)
{
    return key == CK_AGAIN ? T(MSG_CTL_AGAIN) : ui_key_name(key);
}

static const uint16_t curve_names[CC_COUNT] = { MSG_SHARED_CURVE_LINEAR, MSG_SHARED_CURVE_SLOW, MSG_SHARED_CURVE_FAST };

static const uint16_t colour_names[8] = {
    MSG_SHARED_COLOUR_AUTO, MSG_SHARED_COLOUR_RED, MSG_SHARED_COLOUR_GREEN, MSG_SHARED_COLOUR_YELLOW,
    MSG_SHARED_COLOUR_BLUE, MSG_SHARED_COLOUR_MAGENTA, MSG_SHARED_COLOUR_CYAN, MSG_SHARED_COLOUR_WHITE,
};

static int pedal(void)
{
    return CK_IS_PEDAL(s.key);
}

static struct preset_controls *table(struct ui_ctx *ui)
{
    return ui_ctl_table(ui);
}

/* the keys offered: footswitches and pairs, the active preset's key (preset mode only, not global), the toe
   switch, the pedals, the MIDI CCs (these three in every mode); not the modulators (scr_mods.c) */
static int key_ok(int mode, int key)
{
    return key >= 0 && CK_VALID(key) && (key != CK_AGAIN || (mode == CM_PRESET && !s.global)) &&
           !CK_IS_MOD(key);
}

/* the next key offered after `key` (dir 1) or before it (-1), round */
static int next_key(int mode, int key, int dir)
{
    for (int n = 0; n < 2 * 256; n++) {
        key = dir > 0 ? (key + 1 == CK_COUNT ? CK_MIDI : key + 1 > 255 ? 0 : key + 1)
                      : (key - 1 < 0 ? 255 : key == CK_MIDI ? CK_COUNT - 1 : key - 1);
        if (key_ok(mode, key))
            return key;
    }
    return 0;
}

static int step_key(int mode, int key, int d)
{
    for (; d > 0; d--)
        key = next_key(mode, key, 1);
    for (; d < 0; d++)
        key = next_key(mode, key, -1);
    return key;
}

static int in_group(const struct control *l, int hold)
{
    return l->key == s.key && (l->modes & s.mode) && (hold ? l->when == CW_HOLD : l->when != CW_HOLD);
}

/* the key's links in the edited mode; on a preset, the global ones of the groups (press, long press) it has none
   in after them */
static void collect(struct ui_ctx *ui)
{
    const struct preset_controls *c = table(ui), *g = &ui->rig->global;
    s.first = R_LINKS;
    int max = ROW_LIST_MAX - s.first - 1, own[2] = { 0, 0 };
    s.nlinks = 0;
    for (int i = 0; i < c->count && s.nlinks < max; i++)
        if (c->c[i].key == s.key && (c->c[i].modes & s.mode)) {
            own[c->c[i].when == CW_HOLD]++;
            s.from_global[s.nlinks] = 0;
            s.links[s.nlinks++] = i;
        }
    for (int i = 0; !s.global && i < g->count && s.nlinks < max; i++)
        if ((in_group(&g->c[i], 0) && !own[0]) || (in_group(&g->c[i], 1) && !own[1])) {
            s.from_global[s.nlinks] = 1;
            s.links[s.nlinks++] = i;
        }
}

static int row_global(int row)
{
    return row >= s.first && row < s.first + s.nlinks && s.from_global[row - s.first];
}

/* a row's link in the edited table, -1 = none (or a global link shown on a preset) */
static int row_link(int row)
{
    return row >= s.first && row < s.first + s.nlinks && !row_global(row) ? s.links[row - s.first] : -1;
}

static const struct control *row_control(struct ui_ctx *ui, int row)
{
    if (row < s.first || row >= s.first + s.nlinks)
        return 0;
    return row_global(row) ? &ui->rig->global.c[s.links[row - s.first]] : &table(ui)->c[s.links[row - s.first]];
}

static int key_modes(void)
{
    return s.key >= CK_TOE ? CM_ALL : s.mode;   /* the toe switch, the pedals, MIDI: the same in every mode */
}

/* the global links of a group (hold: the long press one) copied into the preset, which then has that group; the
   table index of the copy of global link `which` (-1: none / the table is full) */
static int copy_group(struct ui_ctx *ui, int hold, int which)
{
    struct preset_controls *c = &ui->rig->preset.controls;
    const struct preset_controls *g = &ui->rig->global;
    int at = -1;
    for (int i = 0; i < g->count; i++) {
        if (!in_group(&g->c[i], hold))
            continue;
        struct control l = g->c[i];
        l.modes = (uint8_t)key_modes();
        if (preset_control_add(c, &l) != 0) {
            ui_toast(T(MSG_COMMON_TABLE_FULL));
            return -1;
        }
        if (i == which)
            at = c->count - 1;
    }
    return at;
}

static int row_add(void)
{
    return s.first + s.nlinks;
}

static void edited(struct ui_ctx *ui)
{
    ui_ctl_edited(ui);
}

/* the link of a row made the edited mode's own (a link shared with other modes splits), its table index */
static int own(struct ui_ctx *ui, int row)
{
    struct preset_controls *c = table(ui);
    int i = row_link(row);
    if (i < 0)
        return -1;
    if (c->c[i].modes != s.mode && s.key < CK_TOE) {   /* the toe switch and the pedals are the same in every mode */
        if (c->count >= CONTROL_MAX) {
            ui_toast(T(MSG_COMMON_TABLE_FULL));
            return -1;
        }
        /* the copy right after it: the rows keep their order */
        memmove(&c->c[i + 2], &c->c[i + 1], (size_t)(c->count - i - 1) * sizeof c->c[0]);
        c->count++;
        c->c[i + 1] = c->c[i];
        c->c[i + 1].modes = (uint8_t)s.mode;
        c->c[i].modes &= (uint8_t)~s.mode;
        collect(ui);
        i++;
    }
    return i;
}

/* ---------------------------------------------------------------- changes */

static void knob(struct ui_ctx *ui, int row, int d)
{
    struct preset_controls *c = table(ui);
    if (row == R_KEY) {
        s.key = step_key(s.mode, s.key, d);
        collect(ui);
        ui->rev++;
    } else if (row == R_COLOUR && s.key < CK_KEYS) {
        c->colour[s.key] = (uint8_t)(((c->colour[s.key] + d) % 8 + 8) % 8);
        edited(ui);
    }
}

/* a new link that does nothing yet: the destination screen gives it one, or drops it. On a preset whose press
   group is the global one, that group is copied in first (the new link joins it). */
static void add_link(struct ui_ctx *ui)
{
    if (!s.global && control_from_global(ui->rig, s.key, s.key >= CK_TOE ? CM_ALL : s.mode, 0)) {
        copy_group(ui, 0, -1);
        edited(ui);
    }
    struct control l = { .key = (uint8_t)s.key, .modes = (uint8_t)key_modes(),
                         .when = CW_PRESS, .action = CA_FN, .slot = -1, .arg = FSF_NONE };
    if (preset_control_add(table(ui), &l) != 0) {
        ui_toast(T(MSG_COMMON_TABLE_FULL));
        return;
    }
    collect(ui);
    s.list.sel = s.first + s.nlinks - 1;
    ui_target(table(ui)->count - 1, 1);
}

static void remove_link(struct ui_ctx *ui, int row)
{
    struct preset_controls *c = table(ui);
    if (row_global(row)) {                      /* not in this preset: its group gets a NOTHING link */
        const struct control *g = row_control(ui, row);
        struct control none = { .key = (uint8_t)s.key, .modes = (uint8_t)key_modes(),
                                .when = (uint8_t)(g->when == CW_HOLD ? CW_HOLD : CW_PRESS), .action = CA_NOTHING,
                                .slot = -1 };
        if (preset_control_add(c, &none) != 0) {
            ui_toast(T(MSG_COMMON_TABLE_FULL));
            return;
        }
        collect(ui);
        ui_toast(T(MSG_CTL_NO_GLOBALS));
        edited(ui);
        return;
    }
    int i = row_link(row);
    if (i < 0)
        return;
    if (s.global) {                             /* the global table: out of every mode (fw/preset_format.h) */
        int modes = preset_control_remove_alike(c, i);
        collect(ui);
        ui_toast(T(s.key < CK_TOE && (modes & (modes - 1)) ? MSG_CTL_REMOVED_ALL_MODES : MSG_COMMON_LINK_REMOVED));
        edited(ui);
        return;
    }
    if (c->c[i].modes != s.mode && s.key < CK_TOE)
        c->c[i].modes &= (uint8_t)~s.mode;      /* still in the other modes */
    else
        preset_control_remove(c, i);
    collect(ui);
    ui_toast(T(MSG_COMMON_LINK_REMOVED));
    edited(ui);
}

/* learning the key: the footswitch or pedal the player uses next */
static void learned(struct ui_ctx *ui, int key, int hold)
{
    (void)hold;
    if (!key_ok(s.mode, key))
        return;
    s.key = key;
    s.list.sel = R_KEY;
    collect(ui);
    ui->rev++;
}

static void activate(struct ui_ctx *ui, int row)
{
    if (row == R_KEY) {
        ui_learn(ui->learned == learned ? 0 : learned);
    } else if (row == R_NAME) {
        if (s.key < CK_KEYS)
            ui_name_key(s.key);
    } else if (row == row_add()) {
        add_link(ui);
    } else if (row_global(row)) {               /* made the preset's own (with the rest of its group) */
        int i = copy_group(ui, row_control(ui, row)->when == CW_HOLD, s.links[row - s.first]);
        if (i >= 0) {
            ui_toast(T(MSG_CTL_COPIED));
            edited(ui);
            ui_target(i, 0);
        }
    } else if (row_link(row) >= 0) {
        int i = own(ui, row);
        if (i >= 0)
            ui_target(i, 0);
    } else {
        knob(ui, row, 1);
    }
}

/* ---------------------------------------------------------------- text */

static const char *when_text(int when)
{
    return T(when == CW_HOLD ? MSG_COMMON_WHEN_HOLD : when == CW_MOMENT ? MSG_COMMON_WHEN_MOMENT : MSG_COMMON_WHEN_PRESS);
}

void ui_link_text(struct ui_ctx *ui, const struct control *l, char *out, unsigned size)
{
    char name[24] = "?";
    if (l->action == CA_NOTHING) {
        lv_snprintf(out, size, "%s", T(MSG_CTL_NOTHING));
    } else if (l->action == CA_MIDI) {
        if (l->flags & CF_PC)
            lv_snprintf(out, size, T(MSG_CTL_MIDI_PC), l->arg, (l->slot & 15) + 1);
        else
            lv_snprintf(out, size, T(MSG_CTL_MIDI_CC), l->arg, (int)l->off, (int)l->on,
                        (l->slot & 15) + 1);
    } else if (CK_IS_PEDAL(l->key) && l->action == CA_BLOCK) {
        ui_slot_name(ui, l->slot, name, sizeof name);
        lv_snprintf(out, size, "%s %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name,
                    T(l->arg ? MSG_CTL_ON_MOVE : MSG_CTL_WITH_GROUP));
    } else if (l->action == CA_BLOCK) {
        ui_slot_name(ui, l->slot, name, sizeof name);
        lv_snprintf(out, size, "%s %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name, T(l->arg ? MSG_CTL_BLOCK_OFF : MSG_CTL_BLOCK_ON));
    } else if (l->action == CA_PARAM) {
        struct rig_param p;
        char lo[16], hi[16];
        if (control_param_index(ui->rig, l, &p) < 0) {
            lv_snprintf(out, size, "%s ?", ui_slot_tag(ui, l->slot));
            return;
        }
        ui_param_label(&p, name, sizeof name);
        param_format(&p.def, l->off, 0, lo, sizeof lo);
        param_format(&p.def, l->on, 0, hi, sizeof hi);
        if (CK_SWEEPS(l->key) && l->curve != CC_LINEAR)
            lv_snprintf(out, size, "%s %s %s \xC2\xBB %s \xC2\xB7 %s", ui_slot_tag(ui, l->slot), name, lo, hi,
                        T(curve_names[l->curve % CC_COUNT]));
        else
            lv_snprintf(out, size, "%s %s %s \xC2\xBB %s", ui_slot_tag(ui, l->slot), name, lo, hi);
    } else if (l->action == CA_SCENE) {
        if (l->arg < SCENE_COUNT) {
            rig_scene_name(ui->rig, l->arg, name);
            lv_snprintf(out, size, T(MSG_CTL_SCENE), l->arg + 1, name);
        } else {
            lv_snprintf(out, size, "%s", T(l->arg == SCENE_NEXT ? MSG_SHARED_SCENE_NEXT : MSG_SHARED_SCENE_PREV));
        }
    } else {
        lv_snprintf(out, size, "%s", assign_fn_name(l->arg));
    }
}

static void value_text(void *ctx, int row, char *out, unsigned size)
{
    struct ui_ctx *ui = ctx;
    const struct preset_controls *c = table(ui);
    if (row == R_KEY && ui->learned == learned) {
        lv_snprintf(out, size, "%s", T(MSG_CTL_LEARNING));
    } else if (row == R_KEY) {
        lv_snprintf(out, size, "%s \xC2\xB7 %d", key_name(s.key), s.nlinks);
    } else if (row == R_NAME) {
        lv_snprintf(out, size, "%s", s.key >= CK_KEYS ? "-" :
                                     c->label[s.key][0] ? c->label[s.key] : T(MSG_CTL_AUTOMATIC));
    } else if (row == R_COLOUR) {
        lv_snprintf(out, size, "%s", s.key >= CK_KEYS ? "-" : T(colour_names[c->colour[s.key] & 7]));
    } else if (row == row_add()) {
        lv_snprintf(out, size, "%s", T(MSG_COMMON_MENU_ARROW));
    } else if (row_control(ui, row)) {
        ui_link_text(ui, row_control(ui, row), out, size);
    } else {
        out[0] = 0;
    }
}

/* ---------------------------------------------------------------- touch */

static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    struct row_list *l = &s.list;
    if (g->tag >= l->count)
        return;
    if (row_list_scroll(l, g)) {
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        if (l->sel == g->tag)
            activate(ui, g->tag);
        l->sel = g->tag;
        ui->rev++;
    } else if (g->kind == GESTURE_LONG && row_link(g->tag) >= 0) {
        l->sel = g->tag;
        remove_link(ui, g->tag);
    } else if (g->kind == GESTURE_DRAG) {
        if (g->first)
            s.drag_start = 0;
        l->sel = g->tag;
        int steps = g->dx / DRAG_PX;
        if (steps != s.drag_start) {
            knob(ui, g->tag, steps - s.drag_start);
            s.drag_start = steps;
        }
    }
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    row_list_create(&s.list, root, ROW_LIST_MAX, UI_TOPBAR_H, ROW_H, on_row);
}

/* opened on ui_controls' key, mode and link when given; back from the destination screen as it was */
static void enter(struct ui_ctx *ui)
{
    s.global = ui->ctl_global;
    if (ui->controls_key >= 0 || ui->controls_mode) {
        s.mode = ui->controls_mode ? ui->controls_mode : footswitch_mode_bit(ui);
        if (ui->controls_key >= 0)
            s.key = ui->controls_key;
        s.list.sel = 0;
        s.list.top = 0;
    } else if (!s.mode) {
        s.mode = footswitch_mode_bit(ui);
    }
    if (!key_ok(s.mode, s.key))
        s.key = 0;
    collect(ui);
    for (int k = 0; k < s.nlinks; k++)
        if (s.links[k] == ui->controls_link)
            s.list.sel = s.first + k;
    ui->controls_key = -1;
    ui->controls_mode = 0;
    ui->controls_link = -1;
}

static void refresh(struct ui_ctx *ui)
{
    static const uint16_t modes[] = { 0, MSG_COMMON_MODE_PRESET, MSG_COMMON_MODE_STOMP, 0, MSG_COMMON_MODE_SCENES };
    char title[48];
    const char *pre = s.global ? T(MSG_CTL_PREFIX_GLOBAL) : "";
    if (CK_IS_MIDI(s.key))
        lv_snprintf(title, sizeof title, T(MSG_CTL_TITLE_MIDI), pre);
    else if (s.key >= CK_TOE)
        lv_snprintf(title, sizeof title, T(MSG_CTL_TITLE_PEDALS), pre);
    else
        lv_snprintf(title, sizeof title, "%s%s \xC2\xB7 %s", pre, T(s.global ? MSG_CTL_KEYS : MSG_CTL_FOOTSWITCHES),
                    T(modes[s.mode]));
    top_bar_sub(title);
    collect(ui);
    struct row_list *l = &s.list;
    int n = row_add() + 1;
    for (int r = n; r < ROW_LIST_MAX; r++)
        lv_obj_add_flag(l->rows[r], LV_OBJ_FLAG_HIDDEN);
    l->count = n;
    l->last_stop = n - 1;
    if (l->sel >= n)
        l->sel = n - 1;
    lv_snprintf(s.titles[R_KEY], sizeof s.titles[0], "%s", T(MSG_CTL_ROW_KEY));
    lv_snprintf(s.titles[R_NAME], sizeof s.titles[0], "%s", T(MSG_CTL_ROW_NAME));
    lv_snprintf(s.titles[R_COLOUR], sizeof s.titles[0], "%s", T(MSG_CTL_ROW_COLOUR));
    for (int k = 0; k < s.nlinks; k++)
        lv_snprintf(s.titles[s.first + k], sizeof s.titles[0], "%s%s", s.from_global[k] ? T(MSG_CTL_ROW_GLOBAL) : "",
                    pedal() ? "PEDAL" : CK_IS_MIDI(s.key) ? "MIDI" :
                    s.key == CK_TOE ? T(MSG_COMMON_WHEN_PRESS) : when_text(row_control(ui, s.first + k)->when));
    lv_snprintf(s.titles[row_add()], sizeof s.titles[0], "%s", T(MSG_COMMON_NEW_LINK));
    for (int r = 0; r < n; r++)
        row_list_title(l, r, s.titles[r]);
    row_list_refresh(l, value_text, ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        row_list_step(&s.list, d);
        ui->rev++;
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB1);
    if (d) {
        knob(ui, s.list.sel, d);
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU) {
        remove_link(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        activate(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {                  /* the next mode */
        s.mode = s.mode == CM_PRESET ? CM_STOMP : s.mode == CM_STOMP ? CM_SCENE : CM_PRESET;
        if (!key_ok(s.mode, s.key))
            s.key = 0;
        collect(ui);
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) && ui->learned) {     /* stop learning */
        ui_learn(0);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_CTRL)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_controls = { create, enter, refresh, input, 0, 0 };
