#include "ui/widgets/param_cell.h"
#include "ui/theme.h"

#define CELL_PAD 14

void param_cell_create(struct param_cell *c, lv_obj_t *parent, int x, int y, int w, int h,
                       const lv_font_t *value_font, int divider)
{
    c->box = theme_box(parent, x, y, w, h, C_BG);
    lv_obj_set_style_bg_opa(c->box, LV_OPA_TRANSP, 0);
    c->divider = divider ? theme_box(c->box, 0, 0, 1, h, C_LINE) : 0;
    int vh = lv_font_get_line_height(value_font);
    int top = (h - (14 + 3 + vh + 5 + 4)) / 2;
    if (top < 1)                            /* a 52 px row (models screen) just fits */
        top = 1;
    c->label = theme_label(c->box, &font_label_12, C_LABEL, 1);
    lv_obj_set_pos(c->label, CELL_PAD, top);
    lv_obj_set_width(c->label, w - 2 * CELL_PAD - 30);
    lv_label_set_long_mode(c->label, LV_LABEL_LONG_CLIP);
    c->source = theme_label(c->box, &font_label_12, C_OFF, 0);
    lv_obj_align(c->source, LV_ALIGN_TOP_RIGHT, -CELL_PAD, top);
    c->value = theme_label(c->box, value_font, C_TEXT, 0);
    lv_obj_set_pos(c->value, CELL_PAD, top + 14 + 3);
    lv_obj_set_width(c->value, w - 2 * CELL_PAD);
    lv_label_set_long_mode(c->value, LV_LABEL_LONG_CLIP);
    c->bar_w = w - 2 * CELL_PAD;
    c->track = theme_box(c->box, CELL_PAD, top + 14 + 3 + vh + 5, c->bar_w, 4, C_TRACK);
    c->fill = theme_box(c->track, 0, 0, 0, 4, C_ACCENT);
}

void param_cell_set(struct param_cell *c, const char *label, const char *source, const char *value,
                    float frac, uint32_t label_color, uint32_t value_color, uint32_t fill_color)
{
    theme_set_text(c->label, label);
    int label_w = source[0] ? c->bar_w - 30 : c->bar_w;     /* no source tag: the name has the whole width */
    if (lv_obj_get_width(c->label) != label_w)
        lv_obj_set_width(c->label, label_w);
    theme_set_text(c->source, source);
    lv_obj_align(c->source, LV_ALIGN_TOP_RIGHT, -CELL_PAD, lv_obj_get_y(c->label));
    theme_set_text(c->value, value);
    theme_set_color(c->label, label_color);
    theme_set_color(c->value, value_color);
    if (frac < 0) {
        lv_obj_add_flag(c->track, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(c->track, LV_OBJ_FLAG_HIDDEN);
    if (frac > 1)
        frac = 1;
    int w = (int)(frac * (float)c->bar_w + 0.5f);
    if (lv_obj_get_width(c->fill) != w)
        lv_obj_set_width(c->fill, w);
    theme_set_bg(c->fill, fill_color);
}
