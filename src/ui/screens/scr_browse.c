/*
 * 06 Models (browse), after the design's "Fluxo de uso 4,3" v2": choosing the effect of a block, or of an empty
 * slot, in three columns: category (with its icon; scrolls with the finger), type (the library's subgroup) and
 * model. Tapping a model loads it at once, so it is heard; below, the three parameters of the loaded effect
 * follow the knobs. With nothing loaded yet the knobs walk category / type / model.
 *
 * Controls: MENU turns = the model under the cursor (whole category), MENU press = load it; knobs 1-3 = the
 * parameters shown, or with nothing loaded category / type / model; EXIT or ‹ = back (the model stays).
 * Touch: tap a category, a type or a model (= load); drag a column up / down = scroll; ‹ › = parameter pages.
 * Clone: the original's captures under "Capturas", NAM captures under "NAM" with their rate at the row's end
 * ("48k" and the resampler's icon when they run through it, "44.1k" otherwise).
 */
#include "ui/i18n.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define AREA_Y     (UI_TOPBAR_H + 4)
#define FOOT_H     52
#define FOOT_Y     (UI_H - FOOT_H)
#define AREA_H     (FOOT_Y - 4 - AREA_Y)
/* the design's 13-14 px text is too small on the pedal: 20 px names, taller rows, fewer of them in view */
#define CAT_X      4
#define CAT_W      132
#define CAT_ROW    34
#define CAT_ROWS   7                        /* objects (one more than fit, for the partly shown one) */
#define CAT_VIS    (AREA_H / CAT_ROW)       /* rows fully in view */
#define GRP_X      (CAT_X + CAT_W + 4)
#define GRP_W      112
#define GRP_ROW    32
#define GRP_PITCH  35
#define GRP_ROWS   6
#define GRP_VIS    ((AREA_H - 6) / GRP_PITCH)
#define MOD_X      (GRP_X + GRP_W + 4)
#define MOD_W      (UI_W - 4 - MOD_X)
#define MOD_ROW    32
#define MOD_PITCH  35
#define MOD_ROWS   6
#define MOD_VIS    ((AREA_H - 6) / MOD_PITCH)
#define NAME_W     (MOD_W - 8 - 4 - 38 - 6)
#define TAG_W      48                       /* a NAM capture's rate and icon at the row's right end */
#define MAX_GROUPS 24
#define MAX_MODELS 160
#define ARROW_W    30
#define C_FRAME    0x2A2926
#define C_ITEM     0xC9C6BF
#define C_ACTIVE   0x161514

static struct {
    lv_obj_t *cat_box, *cat_rows[CAT_ROWS], *cat_bar[CAT_ROWS], *cat_icon[CAT_ROWS], *cat_name[CAT_ROWS];
    lv_obj_t *cat_track, *cat_thumb;
    lv_obj_t *grp_box, *grp_rows[GRP_ROWS], *grp_bar[GRP_ROWS], *grp_name[GRP_ROWS];
    lv_obj_t *mod_box, *mod_rows[MOD_ROWS], *mod_num[MOD_ROWS], *mod_name[MOD_ROWS];
    lv_obj_t *mod_tag[MOD_ROWS], *mod_rs[MOD_ROWS];     /* a NAM capture's rate, the resampler's icon */
    lv_obj_t *mod_track, *mod_thumb;
    struct param_cell params[3];
    lv_obj_t *prev, *next, *prev_icon, *next_icon, *page_text, *hint;
    int menus[LIB_MAX_MENUS], nmenus;
    int cat_top, grp_top, mod_top;          /* first row shown */
    int follow;                             /* the cursor moved: the next refresh scrolls the type and model
                                               columns to it (a drag only scrolls: the cursor may leave the view) */
    int page;
    /* the category's types and the models of the current one */
    const char *groups[MAX_GROUPS];
    int ngroups, group;
    int16_t models[MAX_MODELS];             /* catalog indices */
    int nmodels;
} s SDRAM_BSS;

/* ---------------------------------------------------------------- the catalog by type */

static const char *group_of(int menu, const struct catalog_item *item)
{
    if (menu == MENU_CLONE)                     /* the original's captures, then NAM's (engine/nam_block.c) */
        return item->block ? "NAM" : T(MSG_BROWSE_CAPTURES);
    if (menu == MENU_IR)
        return T(MSG_BROWSE_USER);
    if (catalog_is_special(menu))
        return T(MSG_BROWSE_CABS);
    return item->engine.category && item->engine.category[0] ? item->engine.category : "-";
}

static int group_index(const char *g)
{
    for (int i = 0; i < s.ngroups; i++)
        if (strcmp(s.groups[i], g) == 0)
            return i;
    return -1;
}

/* the category's types, the type of the cursor, and that type's models */
static void build(struct ui_ctx *ui)
{
    const struct catalog *cat = &ui->rig->cat;
    int n = catalog_count(cat, ui->browse_menu);
    s.ngroups = 0;
    const char *cursor_group = 0;
    for (int k = 0; k < n; k++) {
        struct catalog_item item;
        if (catalog_item(cat, ui->browse_menu, k, &item) != 0)
            continue;
        const char *g = group_of(ui->browse_menu, &item);
        if (group_index(g) < 0 && s.ngroups < MAX_GROUPS)
            s.groups[s.ngroups++] = g;
        if (k == ui->browse_index)
            cursor_group = g;
    }
    if (cursor_group)
        s.group = group_index(cursor_group);
    if (s.group < 0 || s.group >= s.ngroups)
        s.group = 0;
    s.nmodels = 0;
    for (int k = 0; k < n && s.nmodels < MAX_MODELS; k++) {
        struct catalog_item item;
        if (catalog_item(cat, ui->browse_menu, k, &item) == 0 && s.ngroups &&
            strcmp(group_of(ui->browse_menu, &item), s.groups[s.group]) == 0)
            s.models[s.nmodels++] = (int16_t)k;
    }
}

static int loaded_slot(const struct ui_ctx *ui)
{
    return ui->browse_slot >= 0 && rig_slot_used(ui->rig, ui->browse_slot) ? ui->browse_slot : -1;
}

/* the model is the one in the block being browsed for */
static int loaded_here(const struct ui_ctx *ui, int menu, int k)
{
    struct catalog_item item;
    int slot = loaded_slot(ui);
    return slot >= 0 && catalog_item(&ui->rig->cat, menu, k, &item) == 0 && ui->rig->preset.slot[slot].id == item.id;
}

static int menu_pos(const struct ui_ctx *ui)
{
    for (int i = 0; i < s.nmenus; i++)
        if (s.menus[i] == ui->browse_menu)
            return i;
    return 0;
}

/* the first row shown, inside the list */
static void clamp_top(int *top, int rows, int total)
{
    if (*top > total - rows)
        *top = total - rows;
    if (*top < 0)
        *top = 0;
}

static void keep_visible(int *top, int cur, int rows, int total)
{
    if (cur < *top)
        *top = cur;
    if (cur > *top + rows - 1)
        *top = cur - rows + 1;
    if (*top > total - rows)
        *top = total - rows;
    if (*top < 0)
        *top = 0;
}

static void set_menu(struct ui_ctx *ui, int i)
{
    i = i < 0 ? 0 : i >= s.nmenus ? s.nmenus - 1 : i;
    if (s.menus[i] != ui->browse_menu) {
        ui->browse_menu = s.menus[i];
        ui->browse_index = 0;
        s.group = 0;
        s.grp_top = s.mod_top = 0;
    }
    s.follow = 1;
    ui->rev++;
}

static void set_group(struct ui_ctx *ui, int g)
{
    g = g < 0 ? 0 : g >= s.ngroups ? s.ngroups - 1 : g;
    s.group = g;
    /* the cursor goes to the type's first model */
    int n = catalog_count(&ui->rig->cat, ui->browse_menu);
    for (int k = 0; k < n; k++) {
        struct catalog_item item;
        if (catalog_item(&ui->rig->cat, ui->browse_menu, k, &item) == 0 &&
            strcmp(group_of(ui->browse_menu, &item), s.groups[g]) == 0) {
            ui->browse_index = k;
            break;
        }
    }
    s.mod_top = 0;
    s.follow = 1;
    ui->rev++;
}

static void set_index(struct ui_ctx *ui, int i)
{
    int n = catalog_count(&ui->rig->cat, ui->browse_menu);
    i = i >= n ? n - 1 : i;
    i = i < 0 ? 0 : i;
    if (i != ui->browse_index) {
        ui->browse_index = i;
        ui->rev++;
    }
    s.follow = 1;
}

/* into the chain at once; a new block is placed the first time and replaced from then on */
static void load(struct ui_ctx *ui)
{
    if (loaded_here(ui, ui->browse_menu, ui->browse_index))
        return;                                 /* loading it again would reset its parameters */
    if (ui->browse_slot >= 0) {
        rig_replace(ui->rig, ui->browse_slot, ui->browse_menu, ui->browse_index);
    } else if (ui->browse_cell >= 0) {
        int slot = rig_place(ui->rig, ui->browse_cell, ui->browse_menu, ui->browse_index);
        if (slot >= 0) {
            ui->browse_slot = slot;
            ui->browse_cell = -1;
            for (int pos = 0; pos < ui->rig->preset.count; pos++)
                if (ui->rig->preset.order[pos] == slot)
                    ui->browse_pos = ui->sel = pos;
            ui->edit_row = 0;
        }
    } else if (rig_insert(ui->rig, ui->browse_pos, ui->browse_menu, ui->browse_index) == 0) {
        ui->browse_slot = ui->rig->preset.order[ui->browse_pos];
        ui->sel = ui->browse_pos;
        ui->edit_row = 0;
    }
    s.page = 0;
    ui_changed();
}

/* ---------------------------------------------------------------- touch */

/* a column: tap = the row under the finger, drag = scroll by rows */
static int column_touch(const struct gesture *g, int *top, int pitch, int y0, int rows, int total)
{
    static int base;
    if (g->kind == GESTURE_DRAG && g->vertical) {
        if (g->first)
            base = *top;
        int t = base - g->dy / pitch;
        t = t > total - rows ? total - rows : t;
        *top = t < 0 ? 0 : t;
        return -2;
    }
    if (g->kind != GESTURE_TAP)
        return -1;
    int i = *top + (g->y - y0) / pitch;
    return i >= 0 && i < total ? i : -1;
}

static void on_cats(struct ui_ctx *ui, const struct gesture *g)
{
    int i = column_touch(g, &s.cat_top, CAT_ROW, AREA_Y + 1, CAT_VIS, s.nmenus);
    if (i >= 0)
        set_menu(ui, i);
    else if (i == -2)
        ui->rev++;
}

static void on_groups(struct ui_ctx *ui, const struct gesture *g)
{
    int i = column_touch(g, &s.grp_top, GRP_PITCH, AREA_Y + 3, GRP_VIS, s.ngroups);
    if (i >= 0)
        set_group(ui, i);
    else if (i == -2)
        ui->rev++;
}

static void on_models(struct ui_ctx *ui, const struct gesture *g)
{
    int i = column_touch(g, &s.mod_top, MOD_PITCH, AREA_Y + 3, MOD_VIS, s.nmodels);
    if (i >= 0) {
        set_index(ui, s.models[i]);
        load(ui);
    } else if (i == -2) {
        ui->rev++;
    }
}

static int param_total(const struct ui_ctx *ui)
{
    int slot = loaded_slot(ui);
    return slot >= 0 ? rig_param_count(ui->rig, slot) : 0;
}

static int pages(const struct ui_ctx *ui)
{
    int n = param_total(ui);
    return n > 3 ? (n + 2) / 3 : 1;
}

static void on_param(struct ui_ctx *ui, const struct gesture *g)
{
    int k = s.page * 3 + g->tag;
    if (g->kind == GESTURE_DRAG && k < param_total(ui))
        ui_param_drag(ui, loaded_slot(ui), k, g);
}

static void on_page(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    int n = pages(ui), p = s.page + (g->tag ? 1 : -1);
    s.page = p < 0 ? 0 : p >= n ? n - 1 : p;
    ui->rev++;
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *frame(lv_obj_t *root, int x, int w, uint32_t border)
{
    lv_obj_t *o = theme_box(root, x, AREA_Y, w, AREA_H, C_BG);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, rgb(border), 0);
    lv_obj_set_style_clip_corner(o, 1, 0);
    return o;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    /* categories */
    s.cat_box = frame(root, CAT_X, CAT_W, C_FRAME);
    gesture_bind(s.cat_box, on_cats, 0);
    for (int i = 0; i < CAT_ROWS; i++) {
        lv_obj_t *r = theme_box(s.cat_box, 1, 1 + i * CAT_ROW, CAT_W - 2 - 4, CAT_ROW, C_BG);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
        s.cat_rows[i] = r;
        s.cat_bar[i] = theme_box(r, 0, 0, 3, CAT_ROW, C_BG);
        s.cat_icon[i] = theme_icon(r, 0, C_LABEL);
        lv_obj_align(s.cat_icon[i], LV_ALIGN_LEFT_MID, 9, 0);
        s.cat_name[i] = theme_label(r, &font_value_20, C_ITEM, 0);
        lv_obj_set_width(s.cat_name[i], CAT_W - 2 - 4 - 36 - 2);
        lv_label_set_long_mode(s.cat_name[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s.cat_name[i], LV_ALIGN_LEFT_MID, 36, 0);
    }
    s.cat_track = theme_box(s.cat_box, CAT_W - 4, 1, 2, AREA_H - 2, C_LINE);
    s.cat_thumb = theme_box(s.cat_track, 0, 0, 2, 10, C_OFF);

    /* types */
    s.grp_box = frame(root, GRP_X, GRP_W, C_FRAME);
    gesture_bind(s.grp_box, on_groups, 0);
    for (int i = 0; i < GRP_ROWS; i++) {
        lv_obj_t *r = theme_box(s.grp_box, 3, 3 + i * GRP_PITCH, GRP_W - 8, GRP_ROW, C_BG);
        lv_obj_set_style_radius(r, 3, 0);
        s.grp_rows[i] = r;
        s.grp_bar[i] = theme_box(r, 0, 4, 3, GRP_ROW - 8, C_BG);
        s.grp_name[i] = theme_label(r, &font_value_20, C_ITEM, 0);
        lv_obj_set_width(s.grp_name[i], GRP_W - 8 - 16);
        lv_label_set_long_mode(s.grp_name[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s.grp_name[i], LV_ALIGN_LEFT_MID, 8, 0);
    }

    /* models */
    s.mod_box = frame(root, MOD_X, MOD_W, C_FRAME);
    gesture_bind(s.mod_box, on_models, 0);
    for (int i = 0; i < MOD_ROWS; i++) {
        lv_obj_t *r = theme_box(s.mod_box, 3, 3 + i * MOD_PITCH, MOD_W - 8 - 4, MOD_ROW, C_BG);
        lv_obj_set_style_radius(r, 3, 0);
        lv_obj_set_style_border_width(r, 2, 0);
        s.mod_rows[i] = r;
        s.mod_num[i] = theme_label(r, &font_body_14, C_OFF, 0);
        lv_obj_align(s.mod_num[i], LV_ALIGN_LEFT_MID, 8, 0);
        s.mod_name[i] = theme_label(r, &font_value_20, C_ITEM, 0);
        lv_obj_set_width(s.mod_name[i], NAME_W);
        lv_label_set_long_mode(s.mod_name[i], LV_LABEL_LONG_DOT);
        lv_obj_align(s.mod_name[i], LV_ALIGN_LEFT_MID, 36, 0);
        s.mod_tag[i] = theme_label(r, &font_label_12, C_LABEL, 0);
        s.mod_rs[i] = theme_icon(r, &icon_resample, C_LABEL);
    }
    s.mod_track = theme_box(s.mod_box, MOD_W - 5, 3, 2, AREA_H - 6, C_LINE);
    s.mod_thumb = theme_box(s.mod_track, 0, 0, 2, 10, C_OFF);

    /* parameters of the loaded effect, or the hint */
    theme_box(root, 0, FOOT_Y, UI_W, 1, C_LINE);
    int w = (UI_W - 2 * ARROW_W) / 3;
    s.prev = theme_box(root, 0, FOOT_Y + 1, ARROW_W, FOOT_H - 1, C_BG);
    s.prev_icon = theme_icon(s.prev, &icon_prev_16, C_TEXT);
    lv_obj_center(s.prev_icon);
    gesture_bind(s.prev, on_page, 0);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.params[i], root, ARROW_W + i * w, FOOT_Y + 1, w, FOOT_H - 1, &font_value_20, i > 0);
        gesture_bind(s.params[i].box, on_param, i);
    }
    s.next = theme_box(root, UI_W - ARROW_W, FOOT_Y + 1, ARROW_W, FOOT_H - 1, C_BG);
    s.next_icon = theme_icon(s.next, &icon_next_16, C_TEXT);
    lv_obj_align(s.next_icon, LV_ALIGN_CENTER, 0, -6);
    s.page_text = theme_label(s.next, &font_label_12, C_OFF, 0);
    gesture_bind(s.next, on_page, 1);
    s.hint = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_obj_set_pos(s.hint, 14, FOOT_Y + (FOOT_H - 18) / 2);
}

static void enter(struct ui_ctx *ui)
{
    s.nmenus = ui_menus(ui, s.menus);
    int slot = loaded_slot(ui);
    if (slot >= 0) {
        ui->browse_menu = ui->rig->preset.slot[slot].menu;
        ui->browse_index = ui->rig->preset.slot[slot].index;
        if (ui->browse_index < 0)                   /* an effect of waveod's not installed */
            ui->browse_index = 0;
    } else if (catalog_count(&ui->rig->cat, ui->browse_menu) < 1) {
        ui->browse_menu = MENU_DRV;
        ui->browse_index = 0;
    }
    s.page = 0;
    s.group = -1;
    s.cat_top = menu_pos(ui) - 3;                   /* the category near the middle of its column */
    keep_visible(&s.cat_top, menu_pos(ui), CAT_VIS, s.nmenus);
    s.grp_top = s.mod_top = 0;
    s.follow = 1;                                   /* the loaded model in view */
}

static void thumb(lv_obj_t *track, lv_obj_t *t, int top, int rows, int total, int h)
{
    if (total <= rows) {
        lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(t, h * rows / total);
    lv_obj_set_y(t, h * top / total);
}

/* model row i: a NAM capture's rate at its right end, with the resampler's icon when it runs at 48 kHz */
static void mod_tag(struct ui_ctx *ui, int i, int k, uint32_t color)
{
    enum clone_kind kind = ui->browse_menu == MENU_CLONE ? catalog_clone_kind(&ui->rig->cat, k) : CLONE_ORIGINAL;
    lv_obj_set_width(s.mod_name[i], kind == CLONE_ORIGINAL ? NAME_W : NAME_W - TAG_W);
    if (kind == CLONE_ORIGINAL) {
        lv_obj_add_flag(s.mod_tag[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s.mod_rs[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s.mod_tag[i], LV_OBJ_FLAG_HIDDEN);
    theme_set_text(s.mod_tag[i], kind == CLONE_NAM_48K ? "48k" : "44.1k");
    theme_set_color(s.mod_tag[i], color);
    lv_obj_align(s.mod_tag[i], LV_ALIGN_RIGHT_MID, -6, 0);
    if (kind == CLONE_NAM_48K) {
        lv_obj_clear_flag(s.mod_rs[i], LV_OBJ_FLAG_HIDDEN);
        theme_set_icon(s.mod_rs[i], &icon_resample, color);
        lv_obj_align_to(s.mod_rs[i], s.mod_tag[i], LV_ALIGN_OUT_LEFT_MID, -3, 0);
    } else {
        lv_obj_add_flag(s.mod_rs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_columns(struct ui_ctx *ui)
{
    uint32_t col = theme_menu_color(ui->browse_menu), dim = theme_menu_dim(ui->browse_menu);

    for (int i = 0; i < CAT_ROWS; i++) {
        int k = s.cat_top + i;
        lv_obj_t *r = s.cat_rows[i];
        if (k >= s.nmenus) {
            lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int m = s.menus[k], act = m == ui->browse_menu;
        uint32_t c = theme_menu_color(m);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(r, act ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_bg(r, theme_menu_dim(m));
        lv_obj_set_style_bg_opa(s.cat_bar[i], act ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_bg(s.cat_bar[i], c);
        theme_set_icon(s.cat_icon[i], ui_menu_icon(m, 20), act ? c : C_LABEL);
        theme_set_text(s.cat_name[i], ui_menu_name(m));
        theme_set_color(s.cat_name[i], act ? c : C_ITEM);
    }
    thumb(s.cat_track, s.cat_thumb, s.cat_top, CAT_VIS, s.nmenus, AREA_H - 2);

    if (s.follow)
        keep_visible(&s.grp_top, s.group, GRP_VIS, s.ngroups);
    else
        clamp_top(&s.grp_top, GRP_VIS, s.ngroups);
    for (int i = 0; i < GRP_ROWS; i++) {
        int g = s.grp_top + i;
        lv_obj_t *r = s.grp_rows[i];
        if (g >= s.ngroups) {
            lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int act = g == s.group;
        lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(r, act ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_bg(r, C_ACTIVE);
        lv_obj_set_style_bg_opa(s.grp_bar[i], act ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_bg(s.grp_bar[i], col);
        theme_set_text(s.grp_name[i], s.groups[g]);
        theme_set_color(s.grp_name[i], act ? col : C_ITEM);
    }

    lv_obj_set_style_border_color(s.mod_box, rgb(col), 0);
    int cur = 0;
    for (int i = 0; i < s.nmodels; i++)
        if (s.models[i] == ui->browse_index)
            cur = i;
    if (s.follow)
        keep_visible(&s.mod_top, cur, MOD_VIS, s.nmodels);
    else
        clamp_top(&s.mod_top, MOD_VIS, s.nmodels);
    s.follow = 0;
    for (int i = 0; i < MOD_ROWS; i++) {
        int j = s.mod_top + i;
        lv_obj_t *r = s.mod_rows[i];
        struct catalog_item item;
        if (j >= s.nmodels || catalog_item(&ui->rig->cat, ui->browse_menu, s.models[j], &item) != 0) {
            lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int on = loaded_here(ui, ui->browse_menu, s.models[j]), cursor = s.models[j] == ui->browse_index;
        char num[8];
        lv_snprintf(num, sizeof num, "%02d.", j + 1);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(r, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_bg(r, dim);
        lv_obj_set_style_border_opa(r, on || cursor ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        theme_set_border(r, on ? col : C_OFF);
        theme_set_text(s.mod_num[i], num);
        theme_set_color(s.mod_num[i], on ? col : C_OFF);
        theme_set_text(s.mod_name[i], item.name);
        theme_set_color(s.mod_name[i], on ? C_TEXT : C_ITEM);
        mod_tag(ui, i, s.models[j], on ? col : C_LABEL);
    }
    thumb(s.mod_track, s.mod_thumb, s.mod_top, MOD_VIS, s.nmodels, AREA_H - 6);
}

static void refresh_foot(struct ui_ctx *ui)
{
    int slot = loaded_slot(ui), total = param_total(ui), n = pages(ui);
    int has = slot >= 0;
    if (s.page >= n)
        s.page = n - 1;
    lv_obj_t *objs[] = { s.prev, s.next, s.params[0].box, s.params[1].box, s.params[2].box };
    for (unsigned i = 0; i < sizeof objs / sizeof objs[0]; i++) {
        if (has)
            lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!has) {
        char lane[24], hint[64];
        if (ui->browse_cell >= 0)
            ui_cell_lane(ui, ui->browse_cell, lane, sizeof lane);
        else
            lv_snprintf(lane, sizeof lane, "%s", T(MSG_BROWSE_NEW_BLOCK));
        lv_snprintf(hint, sizeof hint, T(MSG_BROWSE_TAP_HINT), lane);
        theme_set_text(s.hint, hint);
        lv_obj_clear_flag(s.hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s.hint, LV_OBJ_FLAG_HIDDEN);
    uint32_t col = theme_menu_color(ui->rig->preset.slot[slot].menu);
    for (int i = 0; i < 3; i++) {
        int k = s.page * 3 + i;
        struct rig_param p;
        char label[24];
        if (k >= total || rig_param(ui->rig, slot, k, &p) != 0) {
            param_cell_set(&s.params[i], "", "", "", -1, C_LABEL, C_TEXT, col);
            continue;
        }
        ui_param_label(&p, label, sizeof label);
        param_cell_set(&s.params[i], label, "", p.text, ui_param_frac(&p), C_LABEL, C_TEXT, col);
    }
    theme_set_icon(s.prev_icon, &icon_prev_16, s.page > 0 ? C_TEXT : C_FRAME);
    theme_set_icon(s.next_icon, &icon_next_16, s.page < n - 1 ? C_TEXT : C_FRAME);
    char page[8] = "";
    if (n > 1)
        lv_snprintf(page, sizeof page, "%d/%d", s.page + 1, n);
    theme_set_text(s.page_text, page);
    lv_obj_align(s.page_text, LV_ALIGN_CENTER, 0, 10);
}

static void refresh(struct ui_ctx *ui)
{
    char lane[24], title[48];
    if (ui->browse_slot >= 0)
        ui_lane_name(ui, ui->browse_pos, lane, sizeof lane);
    else if (ui->browse_cell >= 0)
        ui_cell_lane(ui, ui->browse_cell, lane, sizeof lane);
    else
        lv_snprintf(lane, sizeof lane, "%s", T(MSG_BROWSE_NEW_BLOCK));
    lv_snprintf(title, sizeof title, T(MSG_BROWSE_TITLE), lane);
    top_bar_sub(title);
    build(ui);
    refresh_columns(ui);
    refresh_foot(ui);
}

/* the model cursor through the whole category (the type follows) */
static void step_model(struct ui_ctx *ui, int d)
{
    set_index(ui, ui->browse_index + d);
    s.group = -1;                               /* build() takes the cursor's */
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        step_model(ui, d);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        load(ui);
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        d = ui_turned(ev, k);
        if (!d)
            continue;
        int i = k - ENC_KNOB1, slot = loaded_slot(ui);
        if (slot >= 0) {
            if (s.page * 3 + i < param_total(ui)) {
                rig_param_nudge(ui->rig, slot, s.page * 3 + i, ev->fast);
                ui_changed();
            }
        } else if (i == 0) {
            set_menu(ui, menu_pos(ui) + d);
            keep_visible(&s.cat_top, menu_pos(ui), CAT_VIS, s.nmenus);
        } else if (i == 1) {
            set_group(ui, s.group + d);
        } else {
            int cur = 0;
            for (int j = 0; j < s.nmodels; j++)
                if (s.models[j] == ui->browse_index)
                    cur = j;
            cur += d;
            cur = cur < 0 ? 0 : cur >= s.nmodels ? s.nmodels - 1 : cur;
            if (s.nmodels)
                set_index(ui, s.models[cur]);
        }
        return 1;
    }
    return 0;
}

const struct screen screen_browse = { create, enter, refresh, input };
