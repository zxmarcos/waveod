#include "ui/widgets/row_list.h"
#include "ui/theme.h"

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void row_list_create(struct row_list *l, lv_obj_t *root, int count, int y0, int row_h, gesture_handler fn)
{
    l->count = count > ROW_LIST_MAX ? ROW_LIST_MAX : count;
    l->sel = l->top = 0;
    l->y0 = y0;
    l->row_h = row_h;
    l->visible = (UI_H - y0) / row_h;
    l->last_stop = l->count - 1;
    for (int r = 0; r < l->count; r++) {
        l->rows[r] = theme_box(root, 0, y0 + r * row_h, UI_W, row_h, C_BG);
        theme_box(l->rows[r], 0, row_h - 1, UI_W, 1, C_LINE);
        l->edge[r] = theme_box(l->rows[r], 0, 0, 3, row_h - 1, C_ACCENT);
        l->title[r] = theme_label(l->rows[r], &font_tag_12, C_LABEL, 2);
        lv_obj_align(l->title[r], LV_ALIGN_LEFT_MID, UI_PAD, 0);
        l->value[r] = theme_label(l->rows[r], &font_value_20, C_TEXT, 0);
        gesture_bind(l->rows[r], fn, r);
    }
}

void row_list_title(struct row_list *l, int row, const char *title)
{
    if (row >= 0 && row < l->count)
        lv_label_set_text(l->title[row], title);
}

void row_list_step(struct row_list *l, int d)
{
    int n = l->last_stop + 1;
    l->sel = ((l->sel + d) % n + n) % n;
}

int row_list_scroll(struct row_list *l, const struct gesture *g)
{
    if (g->kind != GESTURE_DRAG || !g->vertical)
        return 0;
    if (g->first)
        l->drag_top = l->top;
    int max_top = l->count > l->visible ? l->count - l->visible : 0;
    l->top = clamp(l->drag_top - g->dy / l->row_h, 0, max_top);
    l->sel = clamp(l->sel, l->top, l->top + l->visible - 1);
    if (l->sel > l->last_stop)
        l->sel = l->last_stop;
    return 1;
}

void row_list_refresh(struct row_list *l, row_value_fn value, void *ctx)
{
    if (l->sel < l->top)
        l->top = l->sel;
    if (l->sel >= l->top + l->visible)
        l->top = l->sel - l->visible + 1;
    if (l->sel == l->last_stop && l->last_stop < l->count - 1)    /* the dim rows below it stay in view */
        l->top = l->count > l->visible ? l->count - l->visible : 0;
    for (int r = 0; r < l->count; r++) {
        int y = r - l->top;
        if (y < 0 || y >= l->visible) {
            lv_obj_add_flag(l->rows[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(l->rows[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(l->rows[r], l->y0 + y * l->row_h);
        int sel = r == l->sel;
        char v[48];
        value(ctx, r, v, sizeof v);
        theme_set_bg(l->rows[r], sel ? C_LINE : C_BG);
        if (sel)
            lv_obj_clear_flag(l->edge[r], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(l->edge[r], LV_OBJ_FLAG_HIDDEN);
        theme_set_color(l->title[r], sel ? C_TEXT : C_LABEL);
        theme_set_text(l->value[r], v);
        theme_set_color(l->value[r], r > l->last_stop ? C_OFF : sel ? C_ACCENT : C_TEXT);
        lv_obj_align(l->value[r], LV_ALIGN_RIGHT_MID, -UI_PAD, 0);
    }
}
