/*
 * Touch input for the screens: the panel as an LVGL pointer and a small gesture layer on top of the LVGL
 * press events, so that a screen only says which objects react and what a tap, a long press or a drag on
 * them does.
 *
 * A press that moves more than GESTURE_SLOP pixels becomes a drag: the handler then gets GESTURE_DRAG
 * while the finger moves and GESTURE_DRAG_END on release, never a tap or a long press.
 *
 * "Hold to switch" (the design's rule for blocks): on objects bound with gesture_bind_hold a bar grows along
 * the bottom of the object while the finger stays down; when it is full the handler gets GESTURE_LONG, and
 * lifting the finger before that is a plain tap.
 *
 * "Hold to drag" (blocks on the chain screen): on objects bound with gesture_bind_drag a long press picks the
 * object up: the handler gets GESTURE_LONG, then GESTURE_DRAG while the finger moves (with `held` set) and
 * GESTURE_DRAG_END when it lifts. Moving before the time is up is a plain drag (`held` clear).
 */
#pragma once

#include "lvgl.h"

#define GESTURE_SLOP       8        /* px before a press counts as a drag */
#define GESTURE_SWIPE      50       /* px of a drag that make a swipe */
#define GESTURE_HOLD_MS    500      /* hold to switch: time to fill the bar */
#define GESTURE_HOLD_DELAY 100      /* the bar appears after this, so a tap shows nothing */

struct ui_ctx;

enum gesture_kind { GESTURE_TAP, GESTURE_LONG, GESTURE_DRAG, GESTURE_DRAG_END };

struct gesture {
    enum gesture_kind kind;
    int tag;                        /* given to gesture_bind */
    int x, y;                       /* display position */
    int dx, dy;                     /* movement since the press */
    int first;                      /* first GESTURE_DRAG of this press */
    int vertical;                   /* drags: the finger left the slop mostly vertically */
    int held;                       /* drags: started by holding still (gesture_bind_drag) */
};

typedef void (*gesture_handler)(struct ui_ctx *ui, const struct gesture *g);

/* Create the pointer input device (no-op without a touch panel). */
void gesture_init(struct ui_ctx *ui);

/* Make `obj` clickable and send its gestures to `fn`. */
void gesture_bind(lv_obj_t *obj, gesture_handler fn, int tag);

/* The same with "hold to switch"; the bar grows inside `track` (obj itself when 0). */
void gesture_bind_hold(lv_obj_t *obj, gesture_handler fn, int tag, lv_obj_t *track);

/* The same with "hold to drag" after hold_ms. */
void gesture_bind_drag(lv_obj_t *obj, gesture_handler fn, int tag, int hold_ms);

/* Horizontal / vertical swipe direction of a finished drag: -1, 0 or +1. */
int gesture_swipe_x(const struct gesture *g);
int gesture_swipe_y(const struct gesture *g);

/* A marker and the coordinates of the finger on top of every screen (a setting). */
void gesture_cursor_set(int on);
int gesture_cursor_on(void);
void gesture_tick(void);
