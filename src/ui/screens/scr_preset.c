/*
 * 01 Preset (home, tab PRESET): the chain as blocks (with parallel lanes: the PRE blocks, lanes A and B
 * stacked between two dividers, the POST blocks), preset number, name, amp / cab and the bank's four
 * presets as dots, and the three quick knobs.
 *
 * Controls: MENU turns = previous / next preset, MENU press = chain tab, EFX = the next footswitch mode (preset
 * -> stomp -> scenes, the stomp and scene tabs share this), knobs 1-3 = quick knobs, CTRL = the controls map
 * (footswitches, pedals, MIDI, quick knobs; the stomp and scene tabs too).
 * Touch: tap a block = edit it, hold it = on/off (a bar fills along its bottom); swipe the preset name left /
 * right = next / previous preset, up / down = bank, tap it = the presets list; drag a quick knob = turn it, hold
 * it = what it turns (the assignments).
 */
#include "ui/footswitch.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/block.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define ROW_Y      UI_TOPBAR_H
#define ROW_H      54
#define BLOCK_H    30               /* series blocks */
#define LANE_H     21               /* lane blocks, two rows */
#define LANE_GAP   4
#define GAP        5
#define DIVIDER    2
#define MAX_W      64
#define KNOBS_H    68
#define MID_Y      (ROW_Y + ROW_H)
#define MID_H      (UI_H - MID_Y - KNOBS_H)
#define DOT_W      17
#define DOT_H      3

static struct {
    lv_obj_t *row, *mid;
    struct block blocks[PRESET_SLOTS];
    lv_obj_t *dividers[2];
    lv_obj_t *number, *name, *subtitle, *dots[4], *edited, *style;
    struct param_cell knobs[QUICK_COUNT];
} s SDRAM_BSS;

/* a block of the row, tag = chain position */
static void on_block(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {
        ui_edit(g->tag);
    } else if (g->kind == GESTURE_LONG) {
        rig_toggle(ui->rig, ui->rig->preset.order[g->tag]);
        ui_changed();
    }
}

static void on_title(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {                   /* the presets list */
        ui_go(SCR_PRESETS);
        return;
    }
    int d = gesture_swipe_x(g);
    if (d) {
        ui_load_preset(ui->rig->index - d);         /* finger to the left = next */
        return;
    }
    d = gesture_swipe_y(g);
    if (d)
        ui_press(d < 0 ? BTN_BANK_UP : BTN_BANK_DOWN);
}

/* drag = turn it, hold = what it turns (the assignments, on its row) */
static void on_knob(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_LONG) {
        ui->assign_row = g->tag;
        ui_go(SCR_ASSIGN);
        return;
    }
    const struct quick_knob *q = &ui->assign.quick[g->tag];
    if (q->slot == QUICK_VOLUME)
        ui_volume_drag(ui, g);
    else if (q->slot == QUICK_BPM)
        ui_bpm_drag(ui, g);
    else
        ui_param_drag(ui, q->slot, q->k, g);
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    s.row = theme_box(root, 0, ROW_Y, UI_W, ROW_H, C_BG);
    for (int i = 0; i < 2; i++)
        s.dividers[i] = theme_box(s.row, 0, (ROW_H - 45) / 2, DIVIDER, 45, C_WIRE);
    for (int i = 0; i < PRESET_SLOTS; i++) {
        block_create(&s.blocks[i], s.row, 0, 0, 30, BLOCK_H);
        gesture_bind_hold(s.blocks[i].box, on_block, i, 0);
    }
    theme_box(root, 0, MID_Y, UI_W, 1, C_LINE);
    s.mid = theme_box(root, 0, MID_Y + 1, UI_W, MID_H - 1, C_BG);
    gesture_bind(s.mid, on_title, 0);

    s.number = theme_label(root, &font_hero_76, C_ACCENT, -1);
    lv_obj_set_pos(s.number, UI_PAD, MID_Y + (MID_H - lv_font_get_line_height(&font_hero_76)) / 2);
    s.name = theme_label(root, &font_title_40, C_TEXT, 0);
    lv_label_set_long_mode(s.name, LV_LABEL_LONG_DOT);
    s.subtitle = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_label_set_long_mode(s.subtitle, LV_LABEL_LONG_DOT);
    for (int i = 0; i < 4; i++)
        s.dots[i] = theme_box(root, 0, 0, DOT_W, DOT_H, C_BORDER_OFF);
    s.edited = theme_label(root, &font_tag_12, C_ACCENT, 1);
    lv_label_set_text(s.edited, "EDITADO");
    s.style = theme_label(root, &font_tag_12, C_OFF, 2);          /* the preset's style (Rock, Pop...) */

    theme_box(root, 0, UI_H - KNOBS_H, UI_W, 1, C_LINE);
    for (int i = 0; i < QUICK_COUNT; i++) {
        param_cell_create(&s.knobs[i], root, i * UI_W / 3, UI_H - KNOBS_H + 1, UI_W / 3, KNOBS_H - 1,
                          &font_value_26, i > 0);
        gesture_bind_hold(s.knobs[i].box, on_knob, i, 0);
    }
}

static void place_block(struct ui_ctx *ui, int pos, int x, int y, int w, int h)
{
    const struct preset *p = &ui->rig->preset;
    struct block *b = &s.blocks[pos];
    int slot = p->order[pos];
    lv_obj_clear_flag(b->box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(b->box, x, y);
    lv_obj_set_size(b->box, w, h);
    int menu = rig_is_routing(ui->rig, slot) ? -1 : p->slot[slot].menu;
    block_set_effect(b, ui_slot_tag(ui, slot), menu, p->slot[slot].on, 0);
}

static void refresh_row(struct ui_ctx *ui)
{
    const struct topology *t = &ui->topo;
    for (int i = 0; i < PRESET_SLOTS; i++)
        lv_obj_add_flag(s.blocks[i].box, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++)
        lv_obj_add_flag(s.dividers[i], LV_OBJ_FLAG_HIDDEN);
    int avail = UI_W - 2 * UI_PAD;

    if (!t->parallel) {                             /* one row, left to right */
        int n = t->count;
        int w = n ? (avail - (n - 1) * GAP) / n : 0;
        w = w > MAX_W ? MAX_W : w;
        for (int i = 0; i < n; i++)
            place_block(ui, i, UI_PAD + i * (w + GAP), (ROW_H - BLOCK_H) / 2, w, BLOCK_H);
        return;
    }

    /* PRE | lane A over lane B | POST, all blocks one width; the dividers stand for Split and Merge */
    int pre = t->cols[LANE_PRE], post = t->cols[LANE_POST];
    int lane = t->cols[LANE_A] > t->cols[LANE_B] ? t->cols[LANE_A] : t->cols[LANE_B];
    if (lane < 1)
        lane = 1;
    int cols = pre + lane + post;
    int gaps = (cols - 1) * GAP + 2 * (DIVIDER + GAP);
    int w = (avail - gaps) / cols;
    w = w > MAX_W ? MAX_W : w;
    int x_lane = UI_PAD + pre * (w + GAP) + DIVIDER + GAP;
    int x_post = x_lane + lane * (w + GAP) + DIVIDER + GAP;
    lv_obj_clear_flag(s.dividers[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s.dividers[0], x_lane - GAP - DIVIDER);
    if (!t->dual)                                   /* dual L/R: the lanes end at the outputs */
        lv_obj_clear_flag(s.dividers[1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s.dividers[1], x_post - GAP - DIVIDER);
    int y_a = (ROW_H - 2 * LANE_H - LANE_GAP) / 2;
    for (int i = 0; i < t->count; i++) {
        const struct topo_node *n = &t->node[i];
        if (n->lane == LANE_PRE)
            place_block(ui, i, UI_PAD + n->col * (w + GAP), (ROW_H - BLOCK_H) / 2, w, BLOCK_H);
        else if (n->lane == LANE_POST)
            place_block(ui, i, x_post + n->col * (w + GAP), (ROW_H - BLOCK_H) / 2, w, BLOCK_H);
        else
            place_block(ui, i, x_lane + n->col * (w + GAP), n->lane == LANE_A ? y_a : y_a + LANE_H + LANE_GAP,
                        w, LANE_H);
    }
}

static void refresh_knobs(struct ui_ctx *ui)
{
    for (int i = 0; i < QUICK_COUNT; i++) {
        const struct quick_knob *q = &ui->assign.quick[i];
        char value[24];
        if (q->slot == QUICK_VOLUME) {
            lv_snprintf(value, sizeof value, "%d", ui->rig->preset.volume);
            param_cell_set(&s.knobs[i], "PRESET VOL", "", value, ui->rig->preset.volume / 100.0f, C_LABEL,
                           C_TEXT, C_ACCENT);
            continue;
        }
        if (q->slot == QUICK_BPM) {
            int bpm = ui->rig->preset.bpm, global = ui->rig->global_bpm != 0;
            lv_snprintf(value, sizeof value, "%d", bpm);
            param_cell_set(&s.knobs[i], "BPM", global ? "GLOBAL" : "", value, (bpm - 40) / 260.0f, C_LABEL,
                           global ? C_LABEL : C_TEXT, global ? C_OFF : C_ACCENT);
            continue;
        }
        struct rig_param p;
        if (q->slot < 0 || rig_param(ui->rig, q->slot, q->k, &p) != 0) {
            param_cell_set(&s.knobs[i], "-", "", "-", -1, C_OFF, C_OFF, C_ACCENT);
            continue;
        }
        char label[24];
        ui_param_label(&p, label, sizeof label);
        param_cell_set(&s.knobs[i], label, ui_slot_tag(ui, q->slot), p.text, ui_param_frac(&p), C_LABEL,
                       C_TEXT, C_ACCENT);
    }
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    char label[8], sub[64];
    top_bar_tabs(SCR_PRESET);
    refresh_row(ui);

    preset_label(r->index, label);
    theme_set_text(s.number, label);
    theme_set_text(s.name, r->preset.name[0] ? r->preset.name : "(vazio)");
    /* subtitle: the amp and cab of the preset, like "Brit 800 · 4x12 V30" */
    sub[0] = 0;
    int amp = -1, cab = -1;
    for (int pos = 0; pos < r->preset.count; pos++) {
        int slot = r->preset.order[pos], m = r->preset.slot[slot].menu;
        if (amp < 0 && (m == MENU_AMP || m == MENU_CLONE))
            amp = slot;
        if (cab < 0 && (m == MENU_CAB || m == MENU_IR))
            cab = slot;
    }
    char a[24] = "", c[24] = "";
    if (amp >= 0)
        ui_slot_name(ui, amp, a, sizeof a);
    if (cab >= 0)
        ui_slot_name(ui, cab, c, sizeof c);
    lv_snprintf(sub, sizeof sub, "%s%s%s", a, a[0] && c[0] ? " \xC2\xB7 " : "", c);
    if (!sub[0])
        lv_snprintf(sub, sizeof sub, "%s", r->origin == PRESET_USER ? "Preset do usuário" : "Preset de fábrica");
    theme_set_text(s.subtitle, sub);

    /* name, subtitle and the dots as one column next to the number, centred in the middle area */
    lv_obj_update_layout(s.number);
    int x = UI_PAD + lv_obj_get_width(s.number) + 19;
    int name_h = lv_font_get_line_height(&font_title_40), sub_h = lv_font_get_line_height(&font_body_14);
    int top = MID_Y + (MID_H - name_h - 2 - sub_h - 6 - DOT_H) / 2;
    lv_obj_set_pos(s.name, x, top);
    lv_obj_set_width(s.name, UI_W - UI_PAD - x);
    lv_obj_set_pos(s.subtitle, x + 2, top + name_h + 2);
    lv_obj_set_width(s.subtitle, UI_W - UI_PAD - x);
    int dots_y = top + name_h + 2 + sub_h + 6;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_pos(s.dots[i], x + 2 + i * (DOT_W + 4), dots_y);
        theme_set_bg(s.dots[i], i == r->index % 4 ? C_ACCENT : C_BORDER_OFF);
    }
    if (r->dirty)
        lv_obj_clear_flag(s.edited, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s.edited, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s.edited, x + 2 + 4 * (DOT_W + 4) + 6, dots_y - 6);
    char style[12];
    unsigned n = 0;
    for (; r->preset.style[n] && n + 1 < sizeof style; n++)
        style[n] = r->preset.style[n] >= 'a' && r->preset.style[n] <= 'z' ? (char)(r->preset.style[n] - 32) : r->preset.style[n];
    style[n] = 0;
    theme_set_text(s.style, style);
    lv_obj_align(s.style, LV_ALIGN_TOP_RIGHT, -UI_PAD, dots_y - 6);
    refresh_knobs(ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        ui_load_preset(ui->rig->index + d);
        return 1;
    }
    for (int i = 0; i < QUICK_COUNT; i++) {
        if (ev->type != IN_TURN || ev->id != ENC_KNOB1 + i)
            continue;
        const struct quick_knob *q = &ui->assign.quick[i];
        if (q->slot == QUICK_VOLUME)
            rig_set_volume(ui->rig, ui->rig->preset.volume + ev->delta);
        else if (q->slot == QUICK_BPM)
            rig_set_bpm(ui->rig, ui->rig->preset.bpm + ev->fast);
        else if (q->slot >= 0)
            rig_param_nudge(ui->rig, q->slot, q->k, ev->fast);
        ui_changed();
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        ui_tab(SCR_CHAIN);
        return 1;
    }
    if (ui_pressed(ev, BTN_CTRL)) {                 /* the footswitches, pedals, MIDI and quick knobs */
        ui_go(SCR_CONTROL_MAP);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {                  /* the next mode: preset -> stomp -> scenes -> preset */
        ui_set_mode((footswitch_mode(ui) + 1) % 3);
        return 1;
    }
    return 0;
}

const struct screen screen_preset = { create, 0, refresh, input };
