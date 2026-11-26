/*
 * 03 Effect edit (opened from a block): a strip with every block of the chain, the effect's category and
 * model with an ON / OFF switch, and its parameters in rows of three; the three knobs act on the active row.
 *
 * Controls: MENU turns = active row, knobs 1-3 = the row's parameters, EFX = on/off, CTRL = models (browse),
 * EXP = what controls the row's parameter turned last (scr_link.c: scenes, pedals, footswitches), MENU press =
 * the next row (on a Cab / IR block's mic rows: the MIC screen, scr_mic.c), EXIT = back. A parameter's cell names what moves it: CENA, a pedal or a footswitch.
 * Touch: strip: tap a block = edit it instead, hold = switch it on/off; tap the model name = models, tap
 * ON / OFF = on/off; tap a parameter row = make it active, drag a parameter sideways = turn its knob, swipe
 * the parameters up / down = next / previous page; hold a parameter = what controls it (as EXP); ‹ = back.
 * FFT (tap) = the spectrum analyser (ui/widgets/analyzer.h, docs/spectrum.md) in the upper half, without the
 * strip, and the active row alone below (swipe = next / previous row); its own touches: the legend = block /
 * preset, tap = cursor, hold = freeze.
 */
#include "engine/cabmic.h"
#include "model/control.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/analyzer.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define STRIP_Y   UI_TOPBAR_H
#define STRIP_H   36
#define STRIP_GAP 4
#define BAR_H     5
#define HEAD_Y    (STRIP_Y + STRIP_H)
#define HEAD_H    50
#define PILL_W    44
#define PILL_H    26
#define ROWS_Y    (HEAD_Y + HEAD_H)
#define ROW_H     ((UI_H - ROWS_Y) / 2)
#define EDGE_W    3
/* with the spectrum analyser: a thinner head without the strip, the analyser, the active row at the bottom */
#define SC_HEAD_Y UI_TOPBAR_H
#define SC_HEAD_H 32
#define SC_PLOT_Y (SC_HEAD_Y + SC_HEAD_H)
#define SC_ROW_Y  (UI_H - ROW_H)
#define SC_PLOT_H (SC_ROW_Y - SC_PLOT_Y)

static struct {
    lv_obj_t *cols[PRESET_SLOTS], *col_text[PRESET_SLOTS], *col_bar[PRESET_SLOTS];
    int col_pos[PRESET_SLOTS];                  /* chain position shown in each column */
    lv_obj_t *head_line, *scope, *fft, *fft_text;
    lv_obj_t *head, *tag_box, *tag_text, *name, *caret, *pill, *pill_text, *mic, *mic_text;
    lv_obj_t *rows[2], *edge[2];
    struct param_cell cells[2][3];
} s SDRAM_BSS;

static int selected_slot(const struct ui_ctx *ui)
{
    return ui->sel < ui->topo.count ? ui->topo.node[ui->sel].slot : -1;
}

/* the first of the virtual mic's parameters of a Cab / IR block on waveod's engine, or -1 */
static int mic_first(const struct ui_ctx *ui, int slot)
{
    return slot < 0 ? -1 : rig_mic_first(ui->rig, slot);
}

static int row_count(const struct ui_ctx *ui)
{
    int n = rig_param_count(ui->rig, selected_slot(ui));
    return n ? (n + 2) / 3 : 1;
}

/* ---------------------------------------------------------------- touch */

static void on_col(struct ui_ctx *ui, const struct gesture *g)
{
    int pos = s.col_pos[g->tag];
    if (g->kind == GESTURE_TAP && pos != ui->sel) {
        ui_edit(pos);
    } else if (g->kind == GESTURE_LONG) {
        rig_toggle(ui->rig, ui->rig->preset.order[pos]);
        ui_changed();
    }
}

static void on_name(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        ui_press(BTN_CTRL);
}

static void on_pill(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        ui_press(BTN_EFX);
}

static void on_mic(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        ui_go(SCR_MIC);
}

static void on_fft(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    ui->edit_scope = !ui->edit_scope;
    if (!ui->edit_scope)
        analyzer_stop();
    ui->rev++;
}

static void on_scope(struct ui_ctx *ui, const struct gesture *g)
{
    if (analyzer_touch(g)) {
        ui->scope_mode = ui->scope_mode == ANALYZER_BLOCK ? ANALYZER_PRESET : ANALYZER_BLOCK;
        ui->rev++;
    }
}

/* parameter row on the page (tag 0 / 1) or one of its cells (tag 2 + row * 3 + column) */
static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    int on_page = g->tag < 2 ? g->tag : (g->tag - 2) / 3;
    int row = ui->edit_scope ? ui->edit_row : ui->edit_row / 2 * 2 + on_page;   /* one row with the analyser */
    if (g->kind == GESTURE_DRAG || g->kind == GESTURE_DRAG_END) {
        if (g->vertical) {
            int d = gesture_swipe_y(g), rows = row_count(ui);
            int to = ui->edit_scope ? ui->edit_row + (d < 0 ? 1 : -1) : ui->edit_row / 2 * 2 + (d < 0 ? 2 : -2);
            if (d && to >= 0 && to < rows) {
                ui->edit_row = to;
                ui->rev++;
            }
            return;
        }
        if (g->tag < 2 || row >= row_count(ui))
            return;
        int k = row * 3 + (g->tag - 2) % 3;
        if (ui->edit_row != row) {
            ui->edit_row = row;
            ui->rev++;
        }
        if (g->kind == GESTURE_DRAG && k < rig_param_count(ui->rig, selected_slot(ui))) {
            ui->edit_knob = k % 3;
            ui_param_drag(ui, selected_slot(ui), k, g);
        }
        return;
    }
    /* hold a parameter: what controls it */
    if (g->kind == GESTURE_LONG && g->tag >= 2 && row < row_count(ui)) {
        int k = row * 3 + (g->tag - 2) % 3;
        ui->edit_knob = k % 3;
        ui_link_param(selected_slot(ui), k);
        return;
    }
    if (g->kind == GESTURE_TAP && row < row_count(ui) && ui->edit_row != row) {
        ui->edit_row = row;
        ui->rev++;
    }
}

/* ---------------------------------------------------------------- drawing */

/* the "▾" after the model name (not in the font): five rows of pixels */
static lv_obj_t *caret_create(lv_obj_t *parent)
{
    lv_obj_t *c = theme_box(parent, 0, 0, 9, 5, C_BG);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    for (int i = 0; i < 5; i++)
        theme_box(c, i, i, 9 - 2 * i, 1, C_OFF);
    return c;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        s.cols[i] = theme_box(root, 0, STRIP_Y, 10, STRIP_H, C_BG);
        lv_obj_set_style_bg_opa(s.cols[i], LV_OPA_TRANSP, 0);
        s.col_text[i] = theme_label(s.cols[i], &font_tag_12, C_OFF, 1);
        lv_obj_align(s.col_text[i], LV_ALIGN_TOP_MID, 0, 4);
        s.col_bar[i] = theme_box(s.cols[i], 0, STRIP_H - 9 - BAR_H, 10, BAR_H, C_BORDER_OFF);
        lv_obj_set_style_outline_color(s.col_bar[i], rgb(C_TEXT), 0);
        lv_obj_set_style_outline_pad(s.col_bar[i], 1, 0);
        gesture_bind_hold(s.cols[i], on_col, i, s.col_bar[i]);
    }

    s.head_line = theme_box(root, 0, HEAD_Y, UI_W, 1, C_LINE);
    s.head = theme_box(root, 0, HEAD_Y + 1, UI_W - UI_PAD - PILL_W - 8, HEAD_H - 1, C_BG);
    gesture_bind(s.head, on_name, 0);
    s.tag_box = theme_box(s.head, UI_PAD, (HEAD_H - 1 - 23) / 2, 40, 23, C_TEXT);
    lv_obj_set_size(s.tag_box, LV_SIZE_CONTENT, 23);
    lv_obj_set_style_pad_hor(s.tag_box, 7, 0);
    lv_obj_set_style_radius(s.tag_box, 2, 0);
    s.tag_text = theme_label(s.tag_box, &font_tag_12, C_BG, 1);
    lv_obj_align(s.tag_text, LV_ALIGN_LEFT_MID, 0, 0);
    s.name = theme_label(s.head, &font_value_26, C_TEXT, 0);
    lv_label_set_long_mode(s.name, LV_LABEL_LONG_DOT);
    s.caret = caret_create(s.head);

    s.pill = theme_box(root, UI_W - UI_PAD - PILL_W, HEAD_Y + 1 + (HEAD_H - 1 - PILL_H) / 2, PILL_W, PILL_H, C_BG);
    lv_obj_set_style_radius(s.pill, 2, 0);
    lv_obj_set_style_border_width(s.pill, 2, 0);
    s.pill_text = theme_label(s.pill, &font_tag_12, C_OFF, 2);
    lv_obj_center(s.pill_text);
    gesture_bind(s.pill, on_pill, 0);
    s.mic = theme_box(root, UI_W - UI_PAD - 2 * PILL_W - 8, HEAD_Y + 1 + (HEAD_H - 1 - PILL_H) / 2, PILL_W, PILL_H, C_BG);
    lv_obj_set_style_radius(s.mic, 2, 0);
    lv_obj_set_style_border_width(s.mic, 2, 0);
    s.mic_text = theme_label(s.mic, &font_tag_12, C_LABEL, 2);
    lv_label_set_text(s.mic_text, "MIC");
    lv_obj_center(s.mic_text);
    gesture_bind(s.mic, on_mic, 0);
    s.fft = theme_box(root, 0, 0, PILL_W, PILL_H, C_BG);
    lv_obj_set_style_radius(s.fft, 2, 0);
    lv_obj_set_style_border_width(s.fft, 2, 0);
    s.fft_text = theme_label(s.fft, &font_tag_12, C_LABEL, 2);
    lv_label_set_text(s.fft_text, "FFT");
    lv_obj_center(s.fft_text);
    gesture_bind(s.fft, on_fft, 0);

    s.scope = analyzer_create(root, 0, SC_PLOT_Y, UI_W, SC_PLOT_H);
    gesture_bind(s.scope, on_scope, 0);

    for (int r = 0; r < 2; r++) {
        s.rows[r] = theme_box(root, 0, ROWS_Y + r * ROW_H, UI_W, ROW_H, C_BG);
        gesture_bind(s.rows[r], on_row, r);
        theme_box(s.rows[r], 0, 0, UI_W, 1, C_LINE);
        for (int i = 0; i < 3; i++) {
            param_cell_create(&s.cells[r][i], s.rows[r], i * UI_W / 3, 1, UI_W / 3, ROW_H - 1, &font_value_26,
                              i > 0);
            gesture_bind(s.cells[r][i].box, on_row, 2 + r * 3 + i);
        }
        s.edge[r] = theme_box(s.rows[r], 0, 1, EDGE_W, ROW_H - 1, C_ACCENT);
    }
}

static void refresh_strip(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    const struct preset *p = &r->preset;
    int n = 0;
    for (int i = 0; i < ui->topo.count; i++)    /* the effects; Split / Path B / Merge are on the chain tab */
        if (ui->topo.node[i].kind == NODE_EFFECT)
            s.col_pos[n++] = i;
    int w = n ? (UI_W - 2 * UI_PAD - (n - 1) * STRIP_GAP) / n : 0;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        if (i >= n) {
            lv_obj_add_flag(s.cols[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int pos = s.col_pos[i], sl = p->order[pos], cur = pos == ui->sel, on = p->slot[sl].on;
        uint32_t c = rig_is_routing(r, sl) ? C_TEXT : theme_menu_color(p->slot[sl].menu);
        lv_obj_clear_flag(s.cols[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s.cols[i], UI_PAD + i * (w + STRIP_GAP), STRIP_Y);
        lv_obj_set_width(s.cols[i], w);
        lv_obj_set_width(s.col_bar[i], w);
        theme_set_text(s.col_text[i], ui_slot_tag(ui, sl));
        theme_set_color(s.col_text[i], cur ? C_TEXT : on ? C_LABEL : C_DIM);
        lv_obj_align(s.col_text[i], LV_ALIGN_TOP_MID, 0, 4);
        theme_set_bg(s.col_bar[i], on ? c : C_BORDER_OFF);
        lv_obj_set_style_outline_width(s.col_bar[i], cur ? 2 : 0, 0);
    }
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    int slot = selected_slot(ui);
    if (slot < 0)
        return;                         /* only reachable with a block selected */
    int rows = row_count(ui);
    if (ui->edit_row >= rows)
        ui->edit_row = rows - 1;
    int scope = ui->edit_scope, page = ui->edit_row / 2, pages = (rows + 1) / 2;
    char title[48], lane[24], name[32];
    ui_lane_name(ui, ui->sel, lane, sizeof lane);
    if (scope && rows > 1)
        lv_snprintf(title, sizeof title, "EDIT \xC2\xB7 %s \xC2\xB7 LINHA %d/%d", lane, ui->edit_row + 1, rows);
    else if (!scope && pages > 1)
        lv_snprintf(title, sizeof title, "EDIT \xC2\xB7 %s \xC2\xB7 P\xC3\x81G %d/%d", lane, page + 1, pages);
    else
        lv_snprintf(title, sizeof title, "EDIT \xC2\xB7 %s", lane);
    top_bar_sub(title);
    top_bar_scene();
    if (scope)
        for (int i = 0; i < PRESET_SLOTS; i++)
            lv_obj_add_flag(s.cols[i], LV_OBJ_FLAG_HIDDEN);
    else
        refresh_strip(ui);

    /* the head: tag, name, then FFT, MIC (a Cab / IR on waveod's engine), ON from the right */
    const struct preset *p = &r->preset;
    int on = p->slot[slot].on, has_mic = mic_first(ui, slot) >= 0;
    int head_y = scope ? SC_HEAD_Y : HEAD_Y, head_h = scope ? SC_HEAD_H : HEAD_H;
    int pill_y = head_y + 1 + (head_h - 1 - PILL_H) / 2, pills = 2 + has_mic;
    uint32_t col = rig_is_routing(r, slot) ? C_TEXT : theme_menu_color(p->slot[slot].menu);
    lv_obj_set_y(s.head_line, head_y);
    lv_obj_set_pos(s.head, 0, head_y + 1);
    lv_obj_set_size(s.head, UI_W - UI_PAD - pills * (PILL_W + 8), head_h - 1);
    lv_obj_set_y(s.tag_box, (head_h - 1 - 23) / 2);
    theme_set_bg(s.tag_box, col);
    theme_set_text(s.tag_text, ui_slot_tag(ui, slot));
    ui_slot_name(ui, slot, name, sizeof name);
    lv_obj_set_style_text_font(s.name, scope ? &font_value_20 : &font_value_26, 0);
    theme_set_text(s.name, name);
    lv_obj_update_layout(s.tag_box);
    int x = UI_PAD + lv_obj_get_width(s.tag_box) + 10;
    int max_w = lv_obj_get_width(s.head) - x - 24;
    lv_obj_set_pos(s.pill, UI_W - UI_PAD - PILL_W, pill_y);
    if (has_mic) {
        lv_obj_clear_flag(s.mic, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s.mic, UI_W - UI_PAD - 2 * PILL_W - 8, pill_y);
        theme_set_border(s.mic, C_BORDER_OFF);
    } else {
        lv_obj_add_flag(s.mic, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(s.fft, UI_W - UI_PAD - pills * PILL_W - (pills - 1) * 8, pill_y);
    theme_set_bg(s.fft, scope ? C_TEXT : C_BG);
    theme_set_border(s.fft, scope ? C_TEXT : C_BORDER_OFF);
    theme_set_color(s.fft_text, scope ? C_BG : C_LABEL);
    lv_obj_set_width(s.name, LV_SIZE_CONTENT);
    lv_obj_update_layout(s.name);
    if (lv_obj_get_width(s.name) > max_w)
        lv_obj_set_width(s.name, max_w);
    lv_obj_set_pos(s.name, x, (head_h - 1 - (scope ? 24 : 30)) / 2);
    lv_obj_update_layout(s.name);
    lv_obj_set_pos(s.caret, x + lv_obj_get_width(s.name) + 10, (head_h - 1) / 2 - 1);

    theme_set_text(s.pill_text, on ? "ON" : "OFF");
    theme_set_color(s.pill_text, on ? C_BG : C_OFF);
    theme_set_bg(s.pill, on ? col : C_BG);
    theme_set_border(s.pill, on ? col : C_BORDER_OFF);
    lv_obj_center(s.pill_text);

    /* the analyser between the head and the active row, or two rows of parameters */
    if (scope) {
        analyzer_show(1);
        analyzer_watch(&ui->rig->chain, slot, (enum analyzer_mode)ui->scope_mode, col);
        lv_obj_set_y(s.rows[0], SC_ROW_Y);
        lv_obj_add_flag(s.rows[1], LV_OBJ_FLAG_HIDDEN);
    } else {
        analyzer_show(0);
        lv_obj_set_y(s.rows[0], ROWS_Y);
        lv_obj_clear_flag(s.rows[1], LV_OBJ_FLAG_HIDDEN);
    }

    int np = rig_param_count(r, slot);
    for (int rr = 0; rr < 2; rr++) {
        int row = scope ? (rr == 0 ? ui->edit_row : rows) : page * 2 + rr, active = row == ui->edit_row;
        theme_set_bg(s.edge[rr], col);
        if (active)
            lv_obj_clear_flag(s.edge[rr], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s.edge[rr], LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 3; i++) {
            int k = row * 3 + i;
            struct rig_param prm;
            if (row < rows && k < np && rig_param(r, slot, k, &prm) == 0) {
                char label[24];
                ui_param_label(&prm, label, sizeof label);
                int scene = rig_scene_param(r, slot, k);
                int link = control_find_param(r, slot, prm.def.idx, 0);
                char source[32] = "";
                if (link >= 0) {
                    int more = control_find_param(r, slot, prm.def.idx, link + 1) >= 0;
                    lv_snprintf(source, sizeof source, "%s%s%s", scene ? "CENA \xC2\xB7 " : "",
                                ui_key_name(r->preset.controls.c[link].key), more ? " +" : "");
                } else if (scene) {
                    lv_snprintf(source, sizeof source, "CENA");
                }
                param_cell_set(&s.cells[rr][i], label, source, prm.text, ui_param_frac(&prm),
                               active ? C_LABEL : C_DIM, active ? C_TEXT : C_OFF, active ? C_ACCENT : C_WIRE);
                if (scene || link >= 0)
                    theme_set_color(s.cells[rr][i].source, active ? C_ACCENT : C_LABEL);
            } else {
                param_cell_set(&s.cells[rr][i], "", "", "", -1, C_DIM, C_OFF, C_WIRE);
            }
        }
    }
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int slot = selected_slot(ui);
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        int rows = row_count(ui);
        ui->edit_row = ((ui->edit_row + d) % rows + rows) % rows;
        ui->rev++;
        return 1;
    }
    for (int i = 0; i < 3; i++)
        if (ev->type == IN_TURN && ev->id == ENC_KNOB1 + i && slot >= 0) {
            int k = ui->edit_row * 3 + i;
            if (k < rig_param_count(ui->rig, slot)) {
                ui->edit_knob = i;
                rig_param_nudge(ui->rig, slot, k, ev->fast);
                ui_changed();
            }
            return 1;
        }
    if (ui_pressed(ev, BTN_EFX) && slot >= 0) {
        rig_toggle(ui->rig, slot);
        ui_changed();
        return 1;
    }
    if (ui_pressed(ev, BTN_EXP) && slot >= 0) {
        int k = ui->edit_row * 3 + ui->edit_knob, n = rig_param_count(ui->rig, slot);
        ui_link_param(slot, k < n ? k : ui->edit_row * 3);
        return 1;
    }
    if (ui_pressed(ev, BTN_CTRL) && slot >= 0) {
        ui->browse_slot = slot;
        ui->browse_cell = -1;
        ui->browse_pos = ui->sel;
        ui_go(SCR_BROWSE);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        int mic = mic_first(ui, slot);
        if (mic >= 0 && ui->edit_row >= mic / 3 && ui->edit_row <= (mic + CABMIC_PARAMS - 1) / 3) {
            ui_go(SCR_MIC);
        } else {                            /* the next row, round (EXIT goes back) */
            ui->edit_row = (ui->edit_row + 1) % row_count(ui);
            ui->rev++;
        }
        return 1;
    }
    return 0;
}

static void leave(struct ui_ctx *ui)
{
    (void)ui;
    analyzer_stop();                            /* no tap while another screen shows */
}

static void tick(struct ui_ctx *ui)
{
    if (ui->edit_scope)
        analyzer_tick();
}

const struct screen screen_edit = { create, 0, refresh, input, leave, tick };
