/*
 * Preset assignments (KNOBS on the controls map, or hold a quick knob on the PRESET tab): the quick knobs and
 * EXP 1's starting group. Not in the design; a list like the settings.
 *
 * Rows and controls (knob 1 / knob 2 / knob 3 / MENU press):
 *   KNOB 1-3           target (block, BPM, volume) / parameter
 *   EXP 1              group the preset starts in (A / B)
 * MENU turns pick a row, EXIT goes back. Touch: tap a row to select it (tap again = MENU press), drag it
 * sideways = knob 1. Changes are part of the preset (EDITADO) and saved with it.
 */
#include "platform/sdram.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/row_list.h"
#include "ui/widgets/top_bar.h"
#include <stddef.h>
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define ROW_H    32
#define DRAG_PX  24

enum {
    R_QK = 0,                               /* 3 */
    R_EXP_GROUP = R_QK + QUICK_KNOBS,
    R_COUNT,
};

static struct {
    struct row_list list;
    char titles[R_COUNT][24];
    int drag_start;
} s SDRAM_BSS;

static struct preset *preset(struct ui_ctx *ui)
{
    return &ui->rig->preset;
}

/* the assignment fields of the preset, from qk_target up to raw */
#define ASSIGN_FIRST offsetof(struct preset, qk_target)
#define ASSIGN_SIZE  (offsetof(struct preset, raw) - ASSIGN_FIRST)

static uint8_t before[ASSIGN_SIZE] SDRAM_BSS;

static void snapshot(struct ui_ctx *ui)
{
    memcpy(before, (const uint8_t *)preset(ui) + ASSIGN_FIRST, ASSIGN_SIZE);
}

/* the preset counts as edited only when an assignment really changed */
static void edited(struct ui_ctx *ui)
{
    if (memcmp(before, (const uint8_t *)preset(ui) + ASSIGN_FIRST, ASSIGN_SIZE) != 0)
        ui->rig->dirty = 1;
    ui_changed();
}

/* ---------------------------------------------------------------- choices */

/* next target in [-1, effects in chain order..., extra...] after `cur`, d steps */
static int step_target(struct ui_ctx *ui, int cur, int d, int with_extra)
{
    int list[PRESET_SLOTS + 3], n = 0;
    const struct preset *p = preset(ui);
    list[n++] = -1;
    for (int pos = 0; pos < p->count; pos++)
        if (!rig_is_routing(ui->rig, p->order[pos]))
            list[n++] = p->order[pos];
    if (with_extra) {
        list[n++] = QK_BPM;
        list[n++] = QK_VOLUME;
    }
    int i = 0;
    while (i < n && list[i] != cur)
        i++;
    if (i == n)
        i = 0;
    return list[((i + d) % n + n) % n];
}

static int step_param(struct ui_ctx *ui, int slot, int cur, int d)
{
    int n = rig_param_count(ui->rig, slot);
    return n ? ((cur + d) % n + n) % n : 0;
}

/* ---------------------------------------------------------------- changes */

static void knob(struct ui_ctx *ui, int row, int knob_no, int d)
{
    struct preset *p = preset(ui);
    snapshot(ui);
    if (row < R_EXP_GROUP) {
        int i = row - R_QK;
        if (knob_no == 0)
            p->qk_target[i] = (int8_t)step_target(ui, p->qk_target[i], d, 1);
        else if (knob_no == 1 && p->qk_target[i] >= 0 && p->qk_target[i] < PRESET_SLOTS)
            p->qk_param[i] = (int8_t)step_param(ui, p->qk_target[i], p->qk_param[i], d);
        if (knob_no == 0)
            p->qk_param[i] = 0;
    } else if (row == R_EXP_GROUP) {
        if (knob_no == 0)
            p->exp_group = !p->exp_group;
    }
    edited(ui);
}

static void activate(struct ui_ctx *ui, int row)
{
    knob(ui, row, 0, 1);
}

/* ---------------------------------------------------------------- text */

static void target_text(struct ui_ctx *ui, int slot, int k, char *out, unsigned size)
{
    if (slot == QK_BPM) {
        lv_snprintf(out, size, "%s", T(MSG_ASSIGN_PRESET_BPM));
    } else if (slot == QK_VOLUME) {
        lv_snprintf(out, size, "%s", T(MSG_ASSIGN_PRESET_VOLUME));
    } else if (slot >= 0 && slot < PRESET_SLOTS && rig_slot_used(ui->rig, slot)) {
        struct rig_param p;
        char name[24] = "?";
        if (rig_param(ui->rig, slot, k, &p) == 0)
            ui_param_label(&p, name, sizeof name);
        lv_snprintf(out, size, "%s \xC2\xB7 %s", ui_slot_tag(ui, slot), name);
    } else {
        lv_snprintf(out, size, "-");
    }
}

static void value_text(void *ctx, int row, char *out, unsigned size)
{
    struct ui_ctx *ui = ctx;
    const struct preset *p = preset(ui);
    if (row < R_EXP_GROUP) {
        target_text(ui, p->qk_target[row - R_QK], p->qk_param[row - R_QK], out, size);
    } else if (row == R_EXP_GROUP) {
        lv_snprintf(out, size, p->exp_group ? "B" : "A");
    }
}

/* ---------------------------------------------------------------- touch */

static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    struct row_list *l = &s.list;
    if (row_list_scroll(l, g)) {
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        if (l->sel == g->tag)
            activate(ui, g->tag);
        l->sel = g->tag;
        ui->rev++;
    } else if (g->kind == GESTURE_DRAG) {
        if (g->first)
            s.drag_start = 0;
        l->sel = g->tag;
        int steps = g->dx / DRAG_PX;
        if (steps != s.drag_start) {
            knob(ui, g->tag, 0, steps - s.drag_start);
            s.drag_start = steps;
        }
    }
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    row_list_create(&s.list, root, R_COUNT, UI_TOPBAR_H, ROW_H, on_row);
    for (int i = 0; i < QUICK_KNOBS; i++)
        lv_snprintf(s.titles[R_QK + i], sizeof s.titles[0], "KNOB %d", i + 1);
    lv_snprintf(s.titles[R_EXP_GROUP], sizeof s.titles[0], "%s", T(MSG_ASSIGN_EXP_START));
    for (int r = 0; r < R_COUNT; r++)
        row_list_title(&s.list, r, s.titles[r]);
}

static void enter(struct ui_ctx *ui)
{
    if (ui->assign_row >= 0 && ui->assign_row < R_COUNT)   /* a quick knob held on the PRESET tab */
        s.list.sel = ui->assign_row;
    ui->assign_row = -1;
}

static void refresh(struct ui_ctx *ui)
{
    top_bar_sub(T(MSG_ASSIGN_TITLE));
    row_list_refresh(&s.list, value_text, ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        row_list_step(&s.list, d);
        ui->rev++;
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        d = ui_turned(ev, k);
        if (d) {
            knob(ui, s.list.sel, k - ENC_KNOB1, d);
            return 1;
        }
    }
    if (ui_pressed(ev, BTN_MENU)) {
        activate(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_CTRL)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_assign = { create, enter, refresh, input, 0, 0 };
