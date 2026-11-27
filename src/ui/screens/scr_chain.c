/*
 * 05 Chain (tab CHAIN), after the design's "Fluxo de uso 4,3" v2": the 12 places of the chain always on
 * screen, 2 rows x 6 slots (model/route.h), each block with its category's icon, the routing drawn as wires:
 *
 *   SÉRIE      row 1 continues in row 2, one output
 *   PARALELO   a split and a merge (diamonds) on row 1: between them lane A, and lane B is the whole row 2 (the
 *              wire leaves the split, runs down the corridor between the rows to the start of row 2, across it
 *              and back up the corridor to the merge)
 *   DUAL L/R   a split and no merge: lane A (row 1 to the end) goes to the left output, row 2 to the right one
 *
 * Below the grid a bar with the selected block (ON / OFF, model ›, its place, EDIT) or, for the split, the
 * merge and the IN / OUT pills, the three modes, or (BPM / VOL in the top bar) tempo and volume with TAP TEMPO;
 * at the bottom three parameters of the selection.
 *
 *   - Tap a block = select it, tap it again = edit; tap an empty slot (+) = choose the effect for it.
 *   - Hold or drag a block = move it: onto an empty slot it goes there, onto a block the two swap; the bar
 *     below the grid becomes a bin (dropping there deletes the block), anywhere else cancels.
 *   - Drag a diamond sideways: it snaps between the slots; the merge dragged onto OUT splits the outputs (dual).
 *   - Tap IN / OUT or a diamond: the bar shows SÉRIE / PARALELO / DUAL L/R.
 *
 * Controls: MENU turns = select (IN, the slots in signal order, the split and the merge), knobs 1-3 = the three
 * parameters shown, MENU press = edit (an empty slot: choose its effect; IN / split / merge: next mode; tempo
 * and volume: tap tempo),
 * MENU hold = remove the block, EFX = on/off, CTRL = models.
 */
#include "model/route.h"
#include "ui/footswitch.h"
#include "ui/i18n.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

/* areas */
#define GRAPH_Y      UI_TOPBAR_H
#define GRAPH_H      116
#define BAR_Y        (GRAPH_Y + GRAPH_H)
#define BAR_H        44             /* a finger's height: ON / OFF, the model, EDIT */
#define PARAMS_Y     (BAR_Y + BAR_H)
#define PARAMS_H     (UI_H - PARAMS_Y)
#define ARROW_W      30
#define POWER_W      100            /* ON / OFF and the scene it goes to */
#define EDIT_W       76

/* the grid, in the graph */
#define X0           48
#define PITCH        68
#define BW           44
#define R0           6
#define R1           66
#define Y_A          28             /* wire rows and the corridor between them */
#define Y_M          58
#define Y_B          88
#define X_L          22
#define X_R          458
#define X_IN_END     31
#define X_OUT        446
#define X_OUT_LR     440            /* the wider "OUT L" / "OUT R" pills */
#define PILL_H       18
#define JOINT_W      24
#define JOINT_H      36
#define DIAMOND      11
#define CORNER       9              /* wire corner radius */
#define LIFT         24             /* the carried block floats above the finger */
#define MOVE_HOLD_MS 280
#define DASHES       28
#define DASH         4
#define DASH_GAP     3
#define WIRE_PTS     64
#define PARAM_DRAG_PX 200           /* a drag this long sweeps a split / merge parameter (ui.c's range) */
#define C_JOINT      0xC9C6BF
#define C_EMPTY      0x232220
#define TAP_W        96
#define CHIP_H       32             /* the bar's buttons (modes, TAP TEMPO) */

enum { SEL_CELL, SEL_SPLIT, SEL_MERGE, SEL_ROUTE, SEL_TEMPO };
enum { T_NONE, T_TRASH, T_PLACE, T_SWAP };

struct target {
    int kind;
    int cell;
};

struct dash {
    lv_obj_t *box, *icon, *text, *d[DASHES];
};

struct node {
    lv_obj_t *box, *icon, *text;
};

struct wire {
    lv_obj_t *line;
    lv_point_precise_t pts[WIRE_PTS];
};

static struct {
    lv_obj_t *graph;
    struct wire wa, wb;
    lv_obj_t *pills[3], *pill_text[3];
    struct node cells[ROUTE_CELLS];
    lv_obj_t *joint_hit[2], *joints[2];
    struct dash ghost, source;
    lv_obj_t *fly, *fly_icon, *fly_text;
    /* bar */
    lv_obj_t *bar_line, *fx, *power, *power_btn, *power_icon, *power_text, *power_scene, *model, *model_tag, *model_name, *model_caret;
    lv_obj_t *model_lane, *edit, *jt, *jt_name, *modes[ROUTE_MODES], *mode_text[ROUTE_MODES];
    lv_obj_t *message, *drag_bar, *drag_icon, *drag_text;
    lv_obj_t *tempo;
    /* parameters */
    struct param_cell params[3];
    lv_obj_t *prev, *next, *prev_icon, *next_icon, *page_text;
} s SDRAM_BSS;

static struct {
    int kind;                       /* SEL_x */
    int cell;                       /* SEL_CELL */
    int page;                       /* of three parameters */
    uint32_t loads;                 /* the preset the selection belongs to */
} sel = { SEL_ROUTE, 0, 0, 0 };

/* the block carried by the finger, or a diamond being dragged */
static struct {
    int active;
    int menu;                       /* its category */
    int from;                       /* its cell */
    struct target over;
    int joint;                      /* 0 = split, 1 = merge being dragged, -1 = none */
    int out;                        /* the merge is over OUT: dual on release */
} drag = { .joint = -1 };

/* ---------------------------------------------------------------- the grid */

static const struct preset_route *route(const struct ui_ctx *ui)
{
    return &ui->rig->preset.route;
}

static int slot_at(const struct ui_ctx *ui, int cell)
{
    return route_slot_at(&ui->rig->preset, cell);
}

static int pos_of(const struct ui_ctx *ui, int slot)
{
    for (int pos = 0; pos < ui->rig->preset.count; pos++)
        if (ui->rig->preset.order[pos] == slot)
            return pos;
    return -1;
}

static int cell_x(int cell)
{
    return X0 + cell % ROUTE_COLS * PITCH;
}

static int cell_y(int cell)
{
    return cell >= ROUTE_COLS ? R1 : R0;
}

static int gap_x(int g)                 /* between columns g-1 and g */
{
    return X0 + g * PITCH - 12;
}

static uint32_t slot_color(const struct ui_ctx *ui, int slot)
{
    return theme_menu_color(rig_is_routing(ui->rig, slot) ? -1 : ui->rig->preset.slot[slot].menu);
}

static int selected_slot(const struct ui_ctx *ui)
{
    return sel.kind == SEL_CELL ? slot_at(ui, sel.cell) : -1;
}

/* select; a block also becomes the chain screens' selection (edit screen) */
static void choose(struct ui_ctx *ui, int kind, int cell)
{
    if (sel.kind != kind || sel.cell != cell)
        sel.page = 0;
    sel.kind = kind;
    sel.cell = cell;
    int slot = selected_slot(ui);
    if (slot >= 0)
        ui->sel = pos_of(ui, slot);
    ui->rev++;
}

/* the stops of MENU: IN, then the slots along the signal with the split and the merge where they are */
static int stops(const struct ui_ctx *ui, int *kind, int *cell)
{
    const struct preset_route *r = route(ui);
    int8_t path[ROUTE_CELLS];
    int n = route_path(r, path), k = 0, end = route_lanes_end(r);
    kind[k] = SEL_ROUTE, cell[k++] = 0;
    for (int i = 0; i < n; i++) {
        int c = path[i];
        if (r->mode != ROUTE_SERIAL && c == r->split)
            kind[k] = SEL_SPLIT, cell[k++] = 0;
        if (r->mode == ROUTE_PARALLEL && c == end)
            kind[k] = SEL_MERGE, cell[k++] = 0;
        kind[k] = SEL_CELL, cell[k++] = c;
    }
    if (r->mode == ROUTE_PARALLEL && end == ROUTE_COLS)
        kind[k] = SEL_MERGE, cell[k++] = 0;
    return k;
}

static void step(struct ui_ctx *ui, int d)
{
    int kind[ROUTE_CELLS + 4], cell[ROUTE_CELLS + 4], n = stops(ui, kind, cell), cur = 0;
    for (int i = 0; i < n; i++)
        if (kind[i] == sel.kind && (kind[i] != SEL_CELL || cell[i] == sel.cell))
            cur = i;
    cur += d;
    cur = cur < 0 ? 0 : cur >= n ? n - 1 : cur;
    choose(ui, kind[cur], cell[cur]);
}

/* ---------------------------------------------------------------- dragging */

static void find_target(const struct ui_ctx *ui, int x, int y, struct target *t)
{
    memset(t, 0, sizeof *t);
    if (x < 0 || x >= UI_W)
        return;
    if (y >= BAR_Y) {
        t->kind = T_TRASH;
        return;
    }
    int gy = y - GRAPH_Y;
    if (gy < 0)
        return;
    for (int c = 0; c < ROUTE_CELLS; c++) {
        int sx = cell_x(c), sy = cell_y(c);
        if (x < sx - 7 || x > sx + BW + 7 || gy < sy - 8 || gy > sy + BW + 8)
            continue;
        if (c == drag.from)
            return;
        t->cell = c;
        t->kind = slot_at(ui, c) >= 0 ? T_SWAP : T_PLACE;
        return;
    }
}

static void place_fly(int x, int y)
{
    lv_obj_set_pos(s.fly, x - BW / 2, y - BW / 2 - LIFT);
}

static void drag_move(struct ui_ctx *ui, int x, int y)
{
    place_fly(x, y);
    struct target t;
    find_target(ui, x, y, &t);
    if (memcmp(&t, &drag.over, sizeof t) != 0) {
        drag.over = t;
        ui->rev++;
    }
}

static void drag_begin(struct ui_ctx *ui, int from, int x, int y)
{
    int slot = slot_at(ui, from);
    drag.active = 1;
    drag.menu = rig_is_routing(ui->rig, slot) ? -1 : ui->rig->preset.slot[slot].menu;
    drag.from = from;
    memset(&drag.over, 0, sizeof drag.over);
    uint32_t c = slot_color(ui, slot);
    theme_set_bg(s.fly, theme_menu_dim(drag.menu));
    theme_set_border(s.fly, c);
    theme_set_icon(s.fly_icon, ui_menu_icon(drag.menu, 20), c);
    theme_set_text(s.fly_text, ui_slot_tag(ui, slot));
    theme_set_color(s.fly_text, c);
    lv_obj_align(s.fly_text, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_clear_flag(s.fly, LV_OBJ_FLAG_HIDDEN);
    drag_move(ui, x, y);
    ui->rev++;
}

static void drag_cancel(void)
{
    drag.active = 0;
    lv_obj_add_flag(s.fly, LV_OBJ_FLAG_HIDDEN);
}

static void drop(struct ui_ctx *ui)
{
    struct target t = drag.over;
    int moved = slot_at(ui, drag.from);
    drag_cancel();
    struct rig *r = ui->rig;
    if (t.kind == T_TRASH && moved >= 0) {
        if (rig_remove(r, pos_of(ui, moved)) == 0)
            ui_toast(T(MSG_CHAIN_BLOCK_REMOVED));
    } else if ((t.kind == T_PLACE || t.kind == T_SWAP) && rig_move_cell(r, moved, t.cell) == 0) {
        choose(ui, SEL_CELL, t.cell);
    }
    ui_changed();
}

/* a diamond moved to gap g (or the merge past OUT) */
static void joint_move(struct ui_ctx *ui, int j, int x)
{
    const struct preset *p = &ui->rig->preset;
    int g = (x - (X0 - 12) + PITCH / 2) / PITCH, lo, hi;
    if (x < X0 - 12)
        g = 0;
    if (j == 0) {
        route_split_range(p, &lo, &hi);
        g = g < lo ? lo : g > hi ? hi : g;
        rig_route(ui->rig, p->route.mode, g, p->route.merge);
    } else {
        route_merge_range(p, &lo, &hi);
        g = g < lo ? lo : g > hi ? hi : g;
        drag.out = x > X_OUT + 4;
        rig_route(ui->rig, p->route.mode, p->route.split, g);
    }
    ui_changed();
}

/* ---------------------------------------------------------------- touch */

static void browse(struct ui_ctx *ui)
{
    int slot = selected_slot(ui);
    if (sel.kind != SEL_CELL)
        return;
    ui->browse_slot = slot;
    ui->browse_cell = slot < 0 ? sel.cell : -1;
    ui->browse_pos = slot < 0 ? ui->rig->preset.count : pos_of(ui, slot);
    ui_go(SCR_BROWSE);
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    int cell = g->tag, slot = slot_at(ui, cell);
    switch (g->kind) {
    case GESTURE_TAP:
        if (slot < 0) {
            choose(ui, SEL_CELL, cell);
            browse(ui);
        } else if (sel.kind == SEL_CELL && sel.cell == cell) {
            ui_edit(pos_of(ui, slot));
        } else {
            choose(ui, SEL_CELL, cell);
        }
        break;
    case GESTURE_LONG:
        if (slot >= 0)
            drag_begin(ui, cell, g->x, g->y);
        break;
    case GESTURE_DRAG:
        if (drag.active)
            drag_move(ui, g->x, g->y);
        else if (slot >= 0)
            drag_begin(ui, cell, g->x, g->y);
        break;
    case GESTURE_DRAG_END:
        if (drag.active) {
            drag_move(ui, g->x, g->y);              /* where the finger left */
            drop(ui);
        }
        break;
    }
}

static void on_joint(struct ui_ctx *ui, const struct gesture *g)
{
    int j = g->tag;
    switch (g->kind) {
    case GESTURE_TAP:
        choose(ui, j ? SEL_MERGE : SEL_SPLIT, 0);
        break;
    case GESTURE_DRAG:
        if (g->first) {
            choose(ui, j ? SEL_MERGE : SEL_SPLIT, 0);
            drag.joint = j;
            drag.out = 0;
        }
        joint_move(ui, j, g->x);
        break;
    case GESTURE_DRAG_END:
        if (drag.joint == 1 && drag.out) {
            const struct preset_route *r = route(ui);
            rig_route(ui->rig, ROUTE_DUAL, r->split, r->merge);
            choose(ui, SEL_SPLIT, 0);
            ui_changed();
        }
        drag.joint = -1;
        drag.out = 0;
        ui->rev++;
        break;
    default:
        break;
    }
}

static void on_pill(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        choose(ui, SEL_ROUTE, 0);
}

static void on_power(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        ui_press(BTN_EFX);
}

static void on_model(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        browse(ui);
}

static void on_edit(struct ui_ctx *ui, const struct gesture *g)
{
    int slot = selected_slot(ui);
    if (g->kind == GESTURE_TAP && slot >= 0)
        ui_edit(pos_of(ui, slot));
}

static void on_mode(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    const struct preset_route *r = route(ui);
    rig_route(ui->rig, g->tag, r->split, r->merge);
    if (g->tag == ROUTE_SERIAL && sel.kind != SEL_ROUTE)
        choose(ui, SEL_ROUTE, 0);
    else if (g->tag == ROUTE_DUAL && sel.kind == SEL_MERGE)
        choose(ui, SEL_SPLIT, 0);
    ui_changed();
}

static void on_tap(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        footswitch_tap(ui);
}

/* from tempo and volume back to the block of the chain screens (or the routing) */
static void tempo_close(struct ui_ctx *ui)
{
    const struct preset *p = &ui->rig->preset;
    sel.kind = SEL_ROUTE;
    sel.page = 0;
    if (ui->sel >= 0 && ui->sel < p->count) {
        sel.kind = SEL_CELL;
        sel.cell = p->route.cell[p->order[ui->sel]];
    }
}

/* ---------------------------------------------------------------- parameters of the selection */

static const uint16_t joint_labels[2][3] = {
    { MSG_CHAIN_BALANCE_AB, MSG_CHAIN_LEVEL_A, MSG_CHAIN_LEVEL_B },
    { MSG_CHAIN_MIX_AB, MSG_CHAIN_PAN_A, MSG_CHAIN_PAN_B },
};

static int joint_param(int i)
{
    return (sel.kind == SEL_MERGE ? JOINT_MIX : JOINT_BALANCE) + i;
}

/* how many parameters the selection has */
static int param_total(const struct ui_ctx *ui)
{
    if (sel.kind == SEL_TEMPO)
        return 2;
    if (sel.kind == SEL_SPLIT || sel.kind == SEL_MERGE)
        return 3;
    int slot = selected_slot(ui);
    return slot >= 0 ? rig_param_count(ui->rig, slot) : 0;
}

static int pages(const struct ui_ctx *ui)
{
    int n = param_total(ui);
    return n > 3 ? (n + 2) / 3 : 1;
}

static void param_turn(struct ui_ctx *ui, int i, int d, int fast)
{
    int k = sel.page * 3 + i;
    if (k >= param_total(ui))
        return;
    if (sel.kind == SEL_TEMPO && i == 0)
        rig_set_bpm(ui->rig, ui->rig->preset.bpm + fast);   /* like the preset screen */
    else if (sel.kind == SEL_TEMPO)
        rig_set_volume(ui->rig, ui->rig->preset.volume + d);
    else if (sel.kind == SEL_SPLIT || sel.kind == SEL_MERGE)
        rig_joint(ui->rig, joint_param(i), route(ui)->joint[joint_param(i)] + d);
    else
        rig_param_nudge(ui->rig, selected_slot(ui), k, fast);
    ui_changed();
}

static void on_param(struct ui_ctx *ui, const struct gesture *g)
{
    static int start;
    int i = g->tag, k = sel.page * 3 + i;
    if (g->kind != GESTURE_DRAG || k >= param_total(ui))
        return;
    if (sel.kind == SEL_CELL) {
        ui_param_drag(ui, selected_slot(ui), k, g);
        return;
    }
    if (sel.kind == SEL_TEMPO) {
        if (i == 0)
            ui_bpm_drag(ui, g);
        else
            ui_volume_drag(ui, g);
        return;
    }
    int j = joint_param(i);
    if (g->first)
        start = route(ui)->joint[j];
    int dv = g->vertical ? -g->dy : g->dx;
    rig_joint(ui->rig, j, start + dv * 100 / PARAM_DRAG_PX);
    ui_changed();
}

static void on_page(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    int n = pages(ui), p = sel.page + (g->tag ? 1 : -1);
    sel.page = p < 0 ? 0 : p >= n ? n - 1 : p;
    ui->rev++;
}

/* ---------------------------------------------------------------- drawing helpers */

static void show(lv_obj_t *o, int on)
{
    if (on)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *outlined(lv_obj_t *parent, int x, int y, int w, int h, int radius)
{
    lv_obj_t *o = theme_box(parent, x, y, w, h, C_BG);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, rgb(C_WIRE), 0);
    return o;
}

static void dash_create(struct dash *d, lv_obj_t *parent)
{
    d->box = theme_box(parent, 0, 0, BW, BW, C_BG);
    lv_obj_set_style_radius(d->box, 4, 0);
    for (int i = 0; i < DASHES; i++)
        d->d[i] = theme_box(d->box, 0, 0, 0, 0, C_OFF);
    d->icon = theme_icon(d->box, 0, C_OFF);
    d->text = theme_label(d->box, &font_tag_12, C_OFF, 1);
    lv_obj_add_flag(d->box, LV_OBJ_FLAG_HIDDEN);
}

/* a dashed 2 px frame on a cell, filled with bg, with an icon and a text */
static void dash_show(struct dash *d, int cell, uint32_t color, uint32_t bg, const lv_image_dsc_t *icon,
                      const char *text)
{
    int w = BW, h = BW;
    lv_obj_clear_flag(d->box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(d->box, cell_x(cell), cell_y(cell));
    theme_set_bg(d->box, bg);
    int n = 0;
    for (int edge = 0; edge < 4; edge++) {
        int horizontal = edge < 2, len = horizontal ? w : h;
        for (int p = 0; p < len && n < DASHES; p += DASH + DASH_GAP) {
            int l = len - p < DASH ? len - p : DASH;
            lv_obj_t *o = d->d[n++];
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
            theme_set_bg(o, color);
            if (horizontal) {
                lv_obj_set_pos(o, p, edge == 0 ? 0 : h - 2);
                lv_obj_set_size(o, l, 2);
            } else {
                lv_obj_set_pos(o, edge == 2 ? 0 : w - 2, p);
                lv_obj_set_size(o, 2, l);
            }
        }
    }
    for (; n < DASHES; n++)
        lv_obj_add_flag(d->d[n], LV_OBJ_FLAG_HIDDEN);
    theme_set_icon(d->icon, icon, color);
    theme_set_text(d->text, text);
    theme_set_color(d->text, color);
    if (text[0]) {
        lv_obj_align(d->icon, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_align(d->text, LV_ALIGN_BOTTOM_MID, 0, -3);
    } else {
        lv_obj_center(d->icon);
    }
}

/* a block: icon over the category tag, or "+" alone */
static void node_set(struct node *n, const lv_image_dsc_t *icon, const char *text, uint32_t fg, uint32_t bg,
                     uint32_t border, int ring)
{
    theme_set_bg(n->box, bg);
    theme_set_border(n->box, border);
    theme_set_icon(n->icon, icon, fg);
    theme_set_text(n->text, text);
    theme_set_color(n->text, fg);
    if (text[0]) {
        lv_obj_align(n->icon, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_align(n->text, LV_ALIGN_BOTTOM_MID, 0, -3);
    } else {
        lv_obj_center(n->icon);
    }
    int want = ring ? 2 : 0;
    if (lv_obj_get_style_outline_width(n->box, 0) != want)
        lv_obj_set_style_outline_width(n->box, want, 0);
}

/* the wire through the given corners, each corner rounded (a quadratic curve, like the design's roundPath) */
static void wire_set(struct wire *w, const int (*pts)[2], int n, uint32_t color)
{
    int k = 0;
    w->pts[k++] = (lv_point_precise_t){ pts[0][0], pts[0][1] };
    for (int i = 1; i < n - 1 && k + 6 < WIRE_PTS; i++) {
        float px = (float)pts[i - 1][0], py = (float)pts[i - 1][1], x = (float)pts[i][0], y = (float)pts[i][1];
        float nx = (float)pts[i + 1][0], ny = (float)pts[i + 1][1];
        /* the wires are horizontal and vertical: a segment's length is |dx| + |dy| */
        float l1 = (x > px ? x - px : px - x) + (y > py ? y - py : py - y);
        float l2 = (nx > x ? nx - x : x - nx) + (ny > y ? ny - y : y - ny);
        float r = CORNER;
        if (r > l1 / 2)
            r = l1 / 2;
        if (r > l2 / 2)
            r = l2 / 2;
        if (r < 0.5f) {
            w->pts[k++] = (lv_point_precise_t){ (int32_t)x, (int32_t)y };
            continue;
        }
        float ax = x - (x - px) / l1 * r, ay = y - (y - py) / l1 * r;
        float bx = x + (nx - x) / l2 * r, by = y + (ny - y) / l2 * r;
        for (int j = 0; j <= 4; j++) {
            float t = (float)j / 4, u = 1 - t;
            float qx = u * u * ax + 2 * u * t * x + t * t * bx, qy = u * u * ay + 2 * u * t * y + t * t * by;
            w->pts[k++] = (lv_point_precise_t){ (int32_t)(qx + 0.5f), (int32_t)(qy + 0.5f) };
        }
    }
    w->pts[k++] = (lv_point_precise_t){ pts[n - 1][0], pts[n - 1][1] };
    lv_line_set_points(w->line, w->pts, (uint32_t)k);
    lv_obj_set_style_line_color(w->line, rgb(color), 0);
    show(w->line, 1);
}

static void wire_create(struct wire *w)
{
    w->line = lv_line_create(s.graph);
    lv_obj_set_pos(w->line, 0, 0);
    lv_obj_set_style_line_width(w->line, 2, 0);
    lv_obj_set_style_line_rounded(w->line, 1, 0);
    lv_obj_clear_flag(w->line, LV_OBJ_FLAG_CLICKABLE);
}

/* ---------------------------------------------------------------- creating */

static void create_graph(lv_obj_t *root)
{
    s.graph = theme_box(root, 0, GRAPH_Y, UI_W, GRAPH_H, C_BG);
    wire_create(&s.wb);
    wire_create(&s.wa);
    for (int i = 0; i < 3; i++) {
        s.pills[i] = outlined(s.graph, 0, 0, 31, PILL_H, PILL_H / 2);
        s.pill_text[i] = theme_label(s.pills[i], &font_label_12, C_LABEL, 0);
        gesture_bind(s.pills[i], on_pill, i);
    }
    for (int c = 0; c < ROUTE_CELLS; c++) {
        struct node *n = &s.cells[c];
        n->box = outlined(s.graph, cell_x(c), cell_y(c), BW, BW, 4);
        lv_obj_set_style_outline_pad(n->box, 2, 0);
        lv_obj_set_style_outline_color(n->box, rgb(C_TEXT), 0);
        n->icon = theme_icon(n->box, icons_20[ICON_PLUS], C_WIRE);
        n->text = theme_label(n->box, &font_tag_12, C_OFF, 1);
        gesture_bind_drag(n->box, on_cell, c, MOVE_HOLD_MS);
    }
    dash_create(&s.source, s.graph);
    dash_create(&s.ghost, s.graph);
    for (int i = 0; i < 2; i++) {
        s.joint_hit[i] = theme_box(s.graph, 0, Y_A - JOINT_H / 2, JOINT_W, JOINT_H, C_BG);
        lv_obj_set_style_bg_opa(s.joint_hit[i], LV_OPA_TRANSP, 0);
        gesture_bind(s.joint_hit[i], on_joint, i);
        s.joints[i] = theme_box(s.joint_hit[i], (JOINT_W - DIAMOND) / 2, (JOINT_H - DIAMOND) / 2, DIAMOND, DIAMOND,
                                C_JOINT);
        lv_obj_set_style_outline_color(s.joints[i], rgb(C_TEXT), 0);
        lv_obj_set_style_outline_pad(s.joints[i], 3, 0);
        lv_obj_set_style_transform_pivot_x(s.joints[i], DIAMOND / 2, 0);
        lv_obj_set_style_transform_pivot_y(s.joints[i], DIAMOND / 2, 0);
        lv_obj_set_style_transform_rotation(s.joints[i], 450, 0);
    }

    if (s.fly)                              /* on the top layer: a new language builds the screens again */
        lv_obj_delete(s.fly);
    s.fly = outlined(lv_layer_top(), 0, 0, BW, BW, 4);
    s.fly_icon = theme_icon(s.fly, 0, C_TEXT);
    lv_obj_align(s.fly_icon, LV_ALIGN_TOP_MID, 0, 6);
    s.fly_text = theme_label(s.fly, &font_tag_12, C_TEXT, 1);
    lv_obj_add_flag(s.fly, LV_OBJ_FLAG_HIDDEN);
}

static void create_bar(lv_obj_t *root)
{
    s.bar_line = theme_box(root, 0, BAR_Y, UI_W, 2, C_LINE);
    theme_box(root, 0, BAR_Y + BAR_H - 1, UI_W, 1, C_LINE);
    int y = BAR_Y + 2, h = BAR_H - 3;

    s.fx = theme_box(root, 0, y, UI_W, h, C_BG);
    s.power = theme_box(s.fx, 0, 0, POWER_W, h, C_BG);
    theme_box(s.fx, POWER_W, 0, 1, h, C_LINE);
    s.power_btn = outlined(s.power, 6, (h - CHIP_H - 2) / 2, POWER_W - 12, CHIP_H + 2, 4);   /* ON / OFF, a switch */
    lv_obj_clear_flag(s.power_btn, LV_OBJ_FLAG_CLICKABLE);
    s.power_icon = theme_icon(s.power_btn, &icon_power, C_OFF);
    lv_obj_align(s.power_icon, LV_ALIGN_LEFT_MID, 10, 0);
    s.power_text = theme_label(s.power_btn, &font_tag_12, C_OFF, 2);
    s.power_scene = theme_label(s.power_btn, &font_tag_12, C_ACCENT, 1);     /* "C2": the switch goes to that scene */
    gesture_bind(s.power, on_power, 0);
    s.model = theme_box(s.fx, POWER_W + 1, 0, UI_W - POWER_W - EDIT_W - 2, h, C_BG);
    gesture_bind(s.model, on_model, 0);
    s.model_tag = theme_label(s.model, &font_tag_12, C_LABEL, 0);
    s.model_name = theme_label(s.model, &font_body_14, C_TEXT, 0);
    lv_label_set_long_mode(s.model_name, LV_LABEL_LONG_DOT);
    s.model_caret = theme_icon(s.model, &icon_next_12, C_LABEL);
    s.model_lane = theme_label(s.model, &font_label_12, C_OFF, 1);
    theme_box(s.fx, UI_W - EDIT_W - 1, 0, 1, h, C_LINE);
    s.edit = theme_box(s.fx, UI_W - EDIT_W, 0, EDIT_W, h, C_BG);
    lv_obj_t *eb = outlined(s.edit, 6, (h - CHIP_H - 2) / 2, EDIT_W - 12, CHIP_H + 2, 4);
    lv_obj_clear_flag(eb, LV_OBJ_FLAG_CLICKABLE);
    theme_set_border(eb, C_WIRE);
    lv_obj_t *et = theme_label(eb, &font_tag_12, C_TEXT, 2);
    lv_label_set_text(et, "EDIT");
    lv_obj_align(et, LV_ALIGN_CENTER, -7, 0);
    lv_obj_t *eu = theme_icon(eb, &icon_up_12, C_TEXT);
    lv_obj_align(eu, LV_ALIGN_CENTER, 17, 0);
    gesture_bind(s.edit, on_edit, 0);

    s.jt = theme_box(root, 0, y, UI_W, h, C_BG);
    lv_obj_t *dia = theme_box(s.jt, 14, (h - 7) / 2, 7, 7, C_TEXT);
    lv_obj_set_style_transform_pivot_x(dia, 3, 0);
    lv_obj_set_style_transform_pivot_y(dia, 3, 0);
    lv_obj_set_style_transform_rotation(dia, 450, 0);
    s.jt_name = theme_label(s.jt, &font_body_14, C_TEXT, 0);
    lv_obj_align(s.jt_name, LV_ALIGN_LEFT_MID, 30, 0);
    static const uint16_t names[ROUTE_MODES] = { MSG_CHAIN_MODE_SERIAL, MSG_CHAIN_MODE_PARALLEL, MSG_CHAIN_MODE_DUAL };
    static const int widths[ROUTE_MODES] = { 64, 84, 80 };
    int x = UI_W - UI_PAD + 4;
    for (int m = ROUTE_MODES - 1; m >= 0; m--) {
        x -= widths[m] + 4;
        s.modes[m] = outlined(s.jt, x, (h - CHIP_H) / 2, widths[m], CHIP_H, 3);
        s.mode_text[m] = theme_label(s.modes[m], &font_tag_12, C_LABEL, 1);
        lv_label_set_text(s.mode_text[m], T(names[m]));
        lv_obj_center(s.mode_text[m]);
        gesture_bind(s.modes[m], on_mode, m);
    }

    s.tempo = theme_box(root, 0, y, UI_W, h, C_BG);
    lv_obj_t *td = theme_box(s.tempo, 14, (h - 7) / 2, 7, 7, C_TEXT);
    lv_obj_set_style_transform_pivot_x(td, 3, 0);
    lv_obj_set_style_transform_pivot_y(td, 3, 0);
    lv_obj_set_style_transform_rotation(td, 450, 0);
    lv_obj_t *tn = theme_label(s.tempo, &font_body_14, C_TEXT, 0);
    lv_label_set_text(tn, T(MSG_CHAIN_TEMPO_VOLUME));
    lv_obj_align(tn, LV_ALIGN_LEFT_MID, 30, 0);
    lv_obj_t *tap = outlined(s.tempo, UI_W - UI_PAD - TAP_W, (h - CHIP_H) / 2, TAP_W, CHIP_H, 3);
    theme_set_border(tap, C_ACCENT);
    lv_obj_t *tt = theme_label(tap, &font_tag_12, C_ACCENT, 1);
    lv_label_set_text(tt, "TAP TEMPO");
    lv_obj_center(tt);
    gesture_bind(tap, on_tap, 0);

    s.message = theme_label(root, &font_label_12, C_LABEL, 1);
    lv_obj_set_width(s.message, UI_W - 2 * 12);
    lv_label_set_long_mode(s.message, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s.message, 12, y + (h - 14) / 2);

    s.drag_bar = theme_box(root, 0, y, UI_W, h, C_BG);
    s.drag_icon = theme_icon(s.drag_bar, &icon_trash, C_CLIP);
    lv_obj_align(s.drag_icon, LV_ALIGN_LEFT_MID, 12, 0);
    s.drag_text = theme_label(s.drag_bar, &font_tag_12, C_CLIP, 1);
    lv_obj_set_width(s.drag_text, UI_W - 2 * 12 - 23);
    lv_label_set_long_mode(s.drag_text, LV_LABEL_LONG_DOT);
}

static void create_params(lv_obj_t *root)
{
    int w = (UI_W - 2 * ARROW_W) / 3;
    s.prev = theme_box(root, 0, PARAMS_Y, ARROW_W, PARAMS_H, C_BG);
    s.prev_icon = theme_icon(s.prev, &icon_prev_16, C_TEXT);
    lv_obj_center(s.prev_icon);
    gesture_bind(s.prev, on_page, 0);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.params[i], root, ARROW_W + i * w, PARAMS_Y, w, PARAMS_H, &font_value_26, i > 0);
        gesture_bind(s.params[i].box, on_param, i);
    }
    s.next = theme_box(root, UI_W - ARROW_W, PARAMS_Y, ARROW_W, PARAMS_H, C_BG);
    s.next_icon = theme_icon(s.next, &icon_next_16, C_TEXT);
    lv_obj_align(s.next_icon, LV_ALIGN_CENTER, 0, -6);
    s.page_text = theme_label(s.next, &font_label_12, C_OFF, 0);
    gesture_bind(s.next, on_page, 1);
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    create_graph(root);
    create_bar(root);
    create_params(root);
}

/* ---------------------------------------------------------------- refreshing */

static void pill(int i, int x, int y, int w, const char *text, int lit)
{
    show(s.pills[i], 1);
    lv_obj_set_pos(s.pills[i], x, y - PILL_H / 2);
    lv_obj_set_width(s.pills[i], w);
    theme_set_border(s.pills[i], lit ? C_TEXT : C_WIRE);
    theme_set_text(s.pill_text[i], text);
    lv_obj_center(s.pill_text[i]);
}

static void refresh_wires(struct ui_ctx *ui)
{
    const struct preset_route *rt = route(ui);
    int route_lit = !drag.active && sel.kind == SEL_ROUTE;
    int joint_lit = !drag.active && (sel.kind == SEL_SPLIT || sel.kind == SEL_MERGE || drag.joint >= 0);
    for (int i = 0; i < 3; i++)
        show(s.pills[i], 0);
    pill(0, 3, Y_A, 28, "IN", route_lit);
    if (rt->mode == ROUTE_SERIAL) {
        static const int a[][2] = { { X_IN_END, Y_A }, { X_R, Y_A }, { X_R, Y_M }, { X_L, Y_M }, { X_L, Y_B },
                                    { X_OUT, Y_B } };
        wire_set(&s.wa, a, 6, C_WIRE);
        show(s.wb.line, 0);
        show(s.joint_hit[0], 0);
        show(s.joint_hit[1], 0);
        pill(1, X_OUT, Y_B, 31, "OUT", route_lit);
        return;
    }
    int xs = gap_x(rt->split), xm = drag.joint == 1 && drag.out ? X_OUT + 16 : gap_x(rt->merge);
    const int a[][2] = { { X_IN_END, Y_A }, { X_OUT, Y_A } };
    wire_set(&s.wa, a, 2, C_WIRE);
    if (rt->mode == ROUTE_DUAL) {
        const int b[][2] = { { xs, Y_A }, { xs, Y_M }, { X_L, Y_M }, { X_L, Y_B }, { X_OUT_LR, Y_B } };
        wire_set(&s.wb, b, 5, joint_lit ? C_LABEL : C_WIRE);
        pill(1, X_OUT_LR, Y_A, UI_W - 2 - X_OUT_LR, "OUT L", route_lit);
        pill(2, X_OUT_LR, Y_B, UI_W - 2 - X_OUT_LR, "OUT R", route_lit);
    } else {
        const int b[][2] = { { xs, Y_A }, { xs, Y_M }, { X_L, Y_M }, { X_L, Y_B }, { X_R, Y_B }, { X_R, Y_M },
                             { xm, Y_M }, { xm, Y_A } };
        wire_set(&s.wb, b, 8, joint_lit ? C_LABEL : C_WIRE);
        pill(1, X_OUT, Y_A, 31, "OUT", route_lit);
    }

    for (int j = 0; j < 2; j++) {
        int shown = j == 0 || rt->mode == ROUTE_PARALLEL;
        show(s.joint_hit[j], shown);
        if (!shown)
            continue;
        int lit = !drag.active && (sel.kind == (j ? SEL_MERGE : SEL_SPLIT) || drag.joint == j);
        lv_obj_set_x(s.joint_hit[j], (j ? xm : xs) - JOINT_W / 2);
        theme_set_bg(s.joints[j], lit ? C_ACCENT : C_JOINT);
        lv_obj_set_style_outline_width(s.joints[j], lit ? 2 : 0, 0);
    }
}

static void refresh_cells(struct ui_ctx *ui)
{
    const struct preset *p = &ui->rig->preset;
    const struct target *o = drag.active ? &drag.over : 0;
    show(s.ghost.box, 0);
    show(s.source.box, 0);
    for (int c = 0; c < ROUTE_CELLS; c++) {
        struct node *n = &s.cells[c];
        int slot = slot_at(ui, c), ring = !drag.active && sel.kind == SEL_CELL && sel.cell == c;
        if (o && o->cell == c && (o->kind == T_PLACE || o->kind == T_SWAP)) {
            node_set(n, 0, "", C_BG, C_BG, C_BG, 0);
            uint32_t col = theme_menu_color(drag.menu);
            dash_show(&s.ghost, c, o->kind == T_SWAP ? C_TEXT : col, theme_menu_dim(drag.menu),
                      ui_menu_icon(drag.menu, 20), lib_menu_tag(drag.menu));
            continue;
        }
        if (drag.active && drag.from == c) {
            node_set(n, 0, "", C_BG, C_BG, C_BG, 0);
            dash_show(&s.source, c, C_OFF, C_BG, icons_20[ICON_PLUS], "");
            continue;
        }
        if (slot < 0) {
            node_set(n, icons_20[ICON_PLUS], "", C_WIRE, C_BG, C_EMPTY, ring);
            continue;
        }
        int on = p->slot[slot].on, menu = rig_is_routing(ui->rig, slot) ? -1 : p->slot[slot].menu;
        uint32_t col = slot_color(ui, slot);
        const lv_image_dsc_t *icon = ui_menu_icon(menu, 20);
        if (ring)
            node_set(n, icon, ui_slot_tag(ui, slot), C_BG, on ? col : C_OFF, on ? col : C_OFF, 0);
        else
            node_set(n, icon, ui_slot_tag(ui, slot), on ? col : C_OFF, C_BG, on ? col : C_WIRE, 0);
    }
}

static void upper(char *t)
{
    for (; *t; t++)
        if (*t >= 'a' && *t <= 'z')
            *t = (char)(*t - 32);
}

/* the bar while something is dragged; 0 = nothing is */
static int drag_message(struct ui_ctx *ui, char *out, unsigned size, uint32_t *color, uint32_t *bg, int *icon)
{
    const struct preset_route *rt = route(ui);
    char name[32] = "", lane[24];
    *bg = C_BG;
    *icon = 0;
    if (drag.joint >= 0) {
        *color = C_ACCENT;
        if (drag.joint == 1 && drag.out)
            lv_snprintf(out, size, "%s", T(MSG_CHAIN_DROP_SPLIT_LR));
        else if (drag.joint == 0)
            lv_snprintf(out, size, T(MSG_CHAIN_SPLIT_BEFORE), rt->split + 1);
        else
            lv_snprintf(out, size, T(MSG_CHAIN_MERGE_AFTER), rt->merge);
        return 1;
    }
    if (!drag.active)
        return 0;
    const struct target *o = &drag.over;
    *icon = 1;
    *color = C_CLIP;
    switch (o->kind) {
    case T_TRASH:
        ui_slot_name(ui, slot_at(ui, drag.from), name, sizeof name);
        upper(name);
        lv_snprintf(out, size, T(MSG_CHAIN_DROP_DELETE), name);
        *color = C_BG;
        *bg = C_CLIP;
        break;
    case T_SWAP:
        ui_slot_name(ui, slot_at(ui, o->cell), name, sizeof name);
        upper(name);
        lv_snprintf(out, size, T(MSG_CHAIN_SWAP_WITH), name);
        *color = C_TEXT;
        break;
    case T_PLACE:
        ui_cell_lane(ui, o->cell, lane, sizeof lane);
        lv_snprintf(out, size, T(MSG_CHAIN_DROP_AT), lane);
        *color = C_ACCENT;
        break;
    default:
        lv_snprintf(out, size, "%s", T(MSG_CHAIN_DRAG_HINT));
        break;
    }
    return 1;
}

static void refresh_bar(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    const struct preset_route *rt = route(ui);
    char msg[96], name[32], lane[24];
    uint32_t color = C_LABEL, bg = C_BG;
    int slot = selected_slot(ui), tempo = sel.kind == SEL_TEMPO, joint = sel.kind != SEL_CELL && !tempo, icon = 0;
    int dragging = drag_message(ui, msg, sizeof msg, &color, &bg, &icon);
    int line = !dragging && !joint && !tempo && slot < 0;
    show(s.drag_bar, dragging);
    show(s.message, line);
    show(s.tempo, !dragging && tempo);
    show(s.fx, !dragging && !line && !joint && !tempo);
    show(s.jt, !dragging && !line && joint);
    uint32_t edge = C_LINE;
    if (dragging) {
        edge = drag.active ? C_CLIP : C_LABEL;
        theme_set_bg(s.drag_bar, bg);
        show(s.drag_icon, icon);
        theme_set_icon(s.drag_icon, &icon_trash, color);
        theme_set_text(s.drag_text, msg);
        theme_set_color(s.drag_text, color);
        lv_obj_align(s.drag_text, LV_ALIGN_LEFT_MID, icon ? 35 : 12, 0);
    } else if (line) {
        if (sel.kind == SEL_CELL) {
            ui_cell_lane(ui, sel.cell, lane, sizeof lane);
            lv_snprintf(msg, sizeof msg, T(MSG_CHAIN_EMPTY_SLOT), lane);
        } else {
            lv_snprintf(msg, sizeof msg, "%s", T(MSG_CHAIN_TAP_HINT));
        }
        theme_set_text(s.message, msg);
    } else if (tempo) {
        edge = C_LABEL;
    } else if (!joint) {
        int on = r->preset.slot[slot].on;
        uint32_t c = slot_color(ui, slot);
        edge = on ? c : C_WIRE;
        theme_set_icon(s.power_icon, &icon_power, on ? c : C_OFF);
        theme_set_text(s.power_text, on ? "ON" : "OFF");
        theme_set_color(s.power_text, on ? c : C_OFF);
        lv_obj_align(s.power_text, LV_ALIGN_LEFT_MID, 32, 0);
        if (rig_scene_editing(r)) {
            char sc[4] = { 'C', (char)('1' + rig_scene_active(r) % SCENE_COUNT), 0 };
            theme_set_text(s.power_scene, sc);
            lv_obj_align(s.power_scene, LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_clear_flag(s.power_scene, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s.power_scene, LV_OBJ_FLAG_HIDDEN);
        }
        theme_set_border(s.power_btn, on ? c : C_WIRE);
        theme_set_bg(s.power_btn, on ? 0x161514 : C_BG);
        theme_set_text(s.model_tag, ui_slot_tag(ui, slot));
        theme_set_color(s.model_tag, c);
        lv_obj_align(s.model_tag, LV_ALIGN_LEFT_MID, 10, 0);
        ui_slot_name(ui, slot, name, sizeof name);
        theme_set_text(s.model_name, name);
        ui_cell_lane(ui, sel.cell, lane, sizeof lane);
        theme_set_text(s.model_lane, lane);
        lv_obj_align(s.model_lane, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_update_layout(s.model_tag);
        lv_obj_update_layout(s.model_lane);
        int x = 10 + lv_obj_get_width(s.model_tag) + 6;
        int room = lv_obj_get_width(s.model) - x - lv_obj_get_width(s.model_lane) - 10 - 24;
        lv_obj_set_width(s.model_name, LV_SIZE_CONTENT);
        lv_obj_update_layout(s.model_name);
        if (lv_obj_get_width(s.model_name) > room)
            lv_obj_set_width(s.model_name, room);
        lv_obj_align(s.model_name, LV_ALIGN_LEFT_MID, x, 0);
        lv_obj_update_layout(s.model_name);
        lv_obj_align_to(s.model_caret, s.model_name, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    } else {
        edge = C_LABEL;
        static const uint16_t names[] = { MSG_CHAIN_ROUTING, MSG_CHAIN_SPLIT, MSG_CHAIN_MERGE, MSG_CHAIN_ROUTING };
        theme_set_text(s.jt_name, T(names[sel.kind]));      /* [SEL_CELL] is never shown */
        for (int m = 0; m < ROUTE_MODES; m++) {
            int act = rt->mode == m;
            theme_set_bg(s.modes[m], act ? C_ACCENT : C_BG);
            theme_set_border(s.modes[m], act ? C_ACCENT : C_WIRE);
            theme_set_color(s.mode_text[m], act ? C_BG : C_LABEL);
        }
    }
    theme_set_bg(s.bar_line, edge);
}

static void refresh_params(struct ui_ctx *ui)
{
    int total = param_total(ui), n = pages(ui);
    if (sel.page >= n)
        sel.page = n - 1;
    int slot = selected_slot(ui);
    for (int i = 0; i < 3; i++) {
        int k = sel.page * 3 + i;
        struct param_cell *c = &s.params[i];
        if (k >= total || sel.kind == SEL_ROUTE) {
            param_cell_set(c, "", "", "", -1, C_LABEL, C_TEXT, C_ACCENT);
            continue;
        }
        if (sel.kind == SEL_TEMPO) {
            const struct rig *r = ui->rig;
            int bpm = rig_tempo(r);
            char v[8];
            lv_snprintf(v, sizeof v, "%d", i ? r->preset.volume : bpm);
            param_cell_set(c, i ? "VOLUME" : "BPM", "", v, i ? r->preset.volume / 100.0f : (bpm - 40) / 260.0f,
                           C_LABEL, C_TEXT, C_ACCENT);
            continue;
        }
        if (sel.kind == SEL_SPLIT || sel.kind == SEL_MERGE) {
            int j = joint_param(i);
            char v[8];
            lv_snprintf(v, sizeof v, "%d", route(ui)->joint[j]);
            param_cell_set(c, T(joint_labels[sel.kind == SEL_MERGE][i]), "", v, route(ui)->joint[j] / 100.0f, C_LABEL,
                           C_TEXT, C_ACCENT);
            continue;
        }
        struct rig_param p;
        char label[24];
        if (rig_param(ui->rig, slot, k, &p) != 0) {
            param_cell_set(c, "", "", "", -1, C_LABEL, C_TEXT, C_ACCENT);
            continue;
        }
        ui_param_label(&p, label, sizeof label);
        int scene = rig_scene_param(ui->rig, slot, k);      /* its value is the active scene's */
        param_cell_set(c, label, scene ? T(MSG_CHAIN_SCENE) : "", p.text, ui_param_frac(&p), C_LABEL, C_TEXT,
                       slot_color(ui, slot));
        if (scene)
            theme_set_color(c->source, C_ACCENT);
    }
    theme_set_icon(s.prev_icon, &icon_prev_16, sel.page > 0 ? C_TEXT : 0x2A2926);
    theme_set_icon(s.next_icon, &icon_next_16, sel.page < n - 1 ? C_TEXT : 0x2A2926);
    char page[8] = "";
    if (n > 1)
        lv_snprintf(page, sizeof page, "%d/%d", sel.page + 1, n);
    theme_set_text(s.page_text, page);
    lv_obj_align(s.page_text, LV_ALIGN_CENTER, 0, 10);
}

/* the selection starts over on another preset, and leaves joints the mode does not have */
static void sync_selection(struct ui_ctx *ui)
{
    const struct preset *p = &ui->rig->preset;
    if (sel.loads != ui->rig->loads) {
        sel.loads = ui->rig->loads;
        if (sel.kind == SEL_TEMPO)                  /* swiping presets with tempo and volume open keeps them */
            return;
        sel.page = 0;
        sel.kind = SEL_ROUTE;
        if (p->count) {
            sel.kind = SEL_CELL;
            sel.cell = p->route.cell[p->order[0]];
        }
    }
    if ((sel.kind == SEL_SPLIT && p->route.mode == ROUTE_SERIAL) ||
        (sel.kind == SEL_MERGE && p->route.mode != ROUTE_PARALLEL))
        sel.kind = SEL_ROUTE;
    if (sel.kind == SEL_CELL && slot_at(ui, sel.cell) >= 0)
        ui->sel = pos_of(ui, slot_at(ui, sel.cell));
}

static void enter(struct ui_ctx *ui)
{
    sel.loads = ui->rig->loads;
    if (ui->sel >= 0 && ui->sel < ui->rig->preset.count) {      /* the block of the edit screen */
        int c = ui->rig->preset.route.cell[ui->rig->preset.order[ui->sel]];
        if (c >= 0 && !(sel.kind == SEL_CELL && sel.cell == c)) {
            sel.kind = SEL_CELL;
            sel.cell = c;
            sel.page = 0;
        }
    }
}

static void refresh(struct ui_ctx *ui)
{
    if (ui->tempo_request) {                        /* BPM / VOL in the top bar: open, or (again) close */
        int show_it = ui->tempo_request == 2 || sel.kind != SEL_TEMPO;
        ui->tempo_request = 0;
        if (show_it) {
            sel.kind = SEL_TEMPO;
            sel.page = 0;
        } else {
            tempo_close(ui);
        }
    }
    top_bar_tabs(SCR_CHAIN);
    sync_selection(ui);
    top_bar_tempo(sel.kind == SEL_TEMPO);
    refresh_wires(ui);
    refresh_cells(ui);
    refresh_bar(ui);
    refresh_params(ui);
}

static void leave(struct ui_ctx *ui)
{
    (void)ui;
    drag_cancel();
    drag.joint = -1;
}

/* ---------------------------------------------------------------- controls */

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        step(ui, d);
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++)
        if (ev->type == IN_TURN && ev->id == k) {
            param_turn(ui, k - ENC_KNOB1, ev->delta, ev->fast);
            return 1;
        }
    int slot = selected_slot(ui);
    if (ui_pressed(ev, BTN_MENU)) {
        if (sel.kind == SEL_TEMPO) {
            footswitch_tap(ui);
        } else if (sel.kind == SEL_CELL && slot >= 0) {
            ui_edit(pos_of(ui, slot));
        } else if (sel.kind == SEL_CELL) {
            browse(ui);
        } else {
            const struct preset_route *r = route(ui);
            rig_route(ui->rig, (r->mode + 1) % ROUTE_MODES, r->split, r->merge);
            ui_changed();
        }
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU && slot >= 0) {
        if (rig_remove(ui->rig, pos_of(ui, slot)) == 0)
            ui_toast(T(MSG_CHAIN_BLOCK_REMOVED));
        ui_changed();
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX) && slot >= 0) {
        rig_toggle(ui->rig, slot);
        ui_changed();
        return 1;
    }
    if (ui_pressed(ev, BTN_CTRL)) {
        browse(ui);
        return 1;
    }
    return 0;
}

const struct screen screen_chain = { create, enter, refresh, input, leave, 0 };
