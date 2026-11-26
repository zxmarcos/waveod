/*
 * One parameter: name (and optional source tag) on top, value below, thin bar showing the position in
 * its range. Cells sit side by side, separated by a 1 px rule.
 */
#pragma once

#include "lvgl.h"

struct param_cell {
    lv_obj_t *box, *divider;
    lv_obj_t *label, *source, *value;
    lv_obj_t *track, *fill;
    int bar_w;
};

void param_cell_create(struct param_cell *c, lv_obj_t *parent, int x, int y, int w, int h,
                       const lv_font_t *value_font, int divider);

/* frac < 0 hides the bar; colours: label, value, bar fill */
void param_cell_set(struct param_cell *c, const char *label, const char *source, const char *value,
                    float frac, uint32_t label_color, uint32_t value_color, uint32_t fill_color);
