/*
 * A scrolling list of rows in the design's language: name on the left, value on the right, the selected row
 * on a lighter band with an amber edge. Used by the settings and the preset assignments.
 *
 * Touch (through the gesture handler given at creation): vertical drags scroll the list (row_list_scroll);
 * taps and sideways drags are the screen's, with the row as the gesture's tag.
 */
#pragma once

#include "ui/gesture.h"

#define ROW_LIST_MAX 32

typedef void (*row_value_fn)(void *ctx, int row, char *buf, unsigned size);

struct row_list {
    lv_obj_t *rows[ROW_LIST_MAX], *edge[ROW_LIST_MAX], *title[ROW_LIST_MAX], *value[ROW_LIST_MAX];
    int count, sel, top, visible, y0, row_h;
    int last_stop;                  /* rows after this cannot be selected and are drawn dim (a version line) */
    int drag_top;
};

/* `count` rows from y0 to the bottom of the screen; gestures on a row go to `fn` with tag = row. */
void row_list_create(struct row_list *l, lv_obj_t *root, int count, int y0, int row_h, gesture_handler fn);

void row_list_title(struct row_list *l, int row, const char *title);

/* Move the selection by d, wrapping over the rows that can be selected. */
void row_list_step(struct row_list *l, int d);

/* A vertical drag on a row scrolls the list (the selection stays in view): 1 when it was one. */
int row_list_scroll(struct row_list *l, const struct gesture *g);

/* Lay the rows out around the selection, each visible row's value from `value`. */
void row_list_refresh(struct row_list *l, row_value_fn value, void *ctx);
