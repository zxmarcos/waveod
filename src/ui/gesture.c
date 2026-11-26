#include "ui/gesture.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "platform/touch.h"
#include "ui/theme.h"

#define MAX_BINDINGS  768           /* every screen's, made at start-up (a row list takes 32) */
#define READ_MS       15
#define CURSOR_SIZE   15
#define HOLD_BAR_H    3             /* in tracks taller than HOLD_BAR_FILL; thinner ones are filled */
#define HOLD_BAR_FILL 8

struct binding {
    gesture_handler fn;
    int tag;
    uint16_t hold_ms;               /* timed long press (hold bindings), 0 = LVGL's */
    uint8_t drag_after;             /* the long press starts a drag */
    lv_obj_t *track;                /* hold to switch: where the bar grows, else 0 */
};

static struct binding bindings[MAX_BINDINGS] SDRAM_BSS;   /* only read on touches */
static int nbindings;
static struct ui_ctx *ctx;
static struct touch_point last;

/* the press in progress (one finger) */
static struct {
    lv_point_t start, at;
    int dragging, vertical, sent_drag, long_done, held;
    const struct binding *hold;     /* timed long press running on this binding */
    uint32_t since;
} press;

static lv_obj_t *cursor, *cursor_text, *hold_bar;

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_read(&last);
    data->point.x = last.x;
    data->point.y = last.y;
    data->state = last.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void gesture_init(struct ui_ctx *ui)
{
    ctx = ui;
    if (!touch_ready())
        return;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), READ_MS);
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static void emit(const struct binding *b, enum gesture_kind kind, const lv_point_t *pt, int first)
{
    struct gesture g = {
        .kind = kind,
        .tag = b->tag,
        .x = pt->x,
        .y = pt->y,
        .dx = pt->x - press.start.x,
        .dy = pt->y - press.start.y,
        .first = first,
        .vertical = press.vertical,
        .held = press.held,
    };
    b->fn(ctx, &g);
}

static void hold_stop(void)
{
    press.hold = 0;
    if (hold_bar)
        lv_obj_add_flag(hold_bar, LV_OBJ_FLAG_HIDDEN);
}

/* timed long press: draw the hold bar, fire when the time is up (runs every loop) */
static void hold_tick(void)
{
    const struct binding *b = press.hold;
    if (!b)
        return;
    uint32_t t = time_ms() - press.since;
    if (t >= b->hold_ms) {
        hold_stop();
        if (b->drag_after) {                        /* the finger now carries something */
            press.dragging = press.held = 1;
            press.vertical = press.sent_drag = 0;
        } else {
            press.long_done = 1;
        }
        emit(b, GESTURE_LONG, &press.at, 0);
        return;
    }
    if (t < GESTURE_HOLD_DELAY || !b->track)
        return;
    if (!hold_bar)
        hold_bar = theme_box(lv_layer_top(), 0, 0, 0, 0, C_TEXT);
    /* on the top layer at the track's place: dark on a lit block, light on a dark one */
    lv_area_t a;
    lv_obj_get_coords(b->track, &a);
    int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    int bh = h > HOLD_BAR_FILL ? HOLD_BAR_H : h;
    int fill = (int)((t - GESTURE_HOLD_DELAY) * (uint32_t)w / (b->hold_ms - GESTURE_HOLD_DELAY));
    lv_color_t bg = lv_obj_get_style_bg_color(b->track, 0);
    theme_set_bg(hold_bar, lv_color_luminance(bg) > 128 ? C_BG : C_TEXT);
    lv_obj_set_pos(hold_bar, a.x1, a.y2 + 1 - bh);
    lv_obj_set_size(hold_bar, fill, bh);
    lv_obj_clear_flag(hold_bar, LV_OBJ_FLAG_HIDDEN);
}

static void event_cb(lv_event_t *e)
{
    const struct binding *b = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    press.at = pt;
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        press.start = pt;
        press.dragging = press.sent_drag = press.long_done = press.held = 0;
        hold_stop();
        if (b->hold_ms) {
            press.hold = b;
            press.since = time_ms();
        }
        break;
    case LV_EVENT_PRESSING: {
        int dx = pt.x - press.start.x, dy = pt.y - press.start.y;
        if (!press.dragging && !press.long_done && (iabs(dx) > GESTURE_SLOP || iabs(dy) > GESTURE_SLOP)) {
            press.dragging = 1;
            press.vertical = iabs(dy) > iabs(dx);
            hold_stop();
        }
        if (press.dragging) {
            emit(b, GESTURE_DRAG, &pt, !press.sent_drag);
            press.sent_drag = 1;
        }
        break;
    }
    case LV_EVENT_LONG_PRESSED:
        if (!press.dragging && !b->hold_ms) {       /* hold bindings time their own long press */
            press.long_done = 1;
            emit(b, GESTURE_LONG, &pt, 0);
        }
        break;
    case LV_EVENT_RELEASED:
        hold_stop();
        if (press.dragging)
            emit(b, GESTURE_DRAG_END, &pt, !press.sent_drag);
        else if (!press.long_done)
            emit(b, GESTURE_TAP, &pt, 0);
        press.dragging = press.long_done = 0;
        break;
    case LV_EVENT_PRESS_LOST:
        hold_stop();
        if (press.dragging)                         /* a drag always ends */
            emit(b, GESTURE_DRAG_END, &pt, !press.sent_drag);
        press.dragging = press.long_done = 0;
        break;
    default:
        break;
    }
}

static struct binding *bind(lv_obj_t *obj, gesture_handler fn, int tag)
{
    LV_ASSERT_MSG(nbindings < MAX_BINDINGS, "raise MAX_BINDINGS");
    if (nbindings >= MAX_BINDINGS)
        return 0;
    struct binding *b = &bindings[nbindings++];
    b->fn = fn;
    b->tag = tag;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(obj, event_cb, LV_EVENT_ALL, b);
    return b;
}

void gesture_bind(lv_obj_t *obj, gesture_handler fn, int tag)
{
    bind(obj, fn, tag);
}

void gesture_bind_hold(lv_obj_t *obj, gesture_handler fn, int tag, lv_obj_t *track)
{
    struct binding *b = bind(obj, fn, tag);
    if (b) {
        b->hold_ms = GESTURE_HOLD_MS;
        b->track = track ? track : obj;
    }
}

void gesture_bind_drag(lv_obj_t *obj, gesture_handler fn, int tag, int hold_ms)
{
    struct binding *b = bind(obj, fn, tag);
    if (b) {
        b->hold_ms = (uint16_t)hold_ms;
        b->drag_after = 1;
    }
}

int gesture_swipe_x(const struct gesture *g)
{
    if (g->kind != GESTURE_DRAG_END || g->vertical || iabs(g->dx) < GESTURE_SWIPE)
        return 0;
    return g->dx > 0 ? 1 : -1;
}

int gesture_swipe_y(const struct gesture *g)
{
    if (g->kind != GESTURE_DRAG_END || !g->vertical || iabs(g->dy) < GESTURE_SWIPE)
        return 0;
    return g->dy > 0 ? 1 : -1;
}

/* ---------------------------------------------------------------- diagnostics cursor */

void gesture_cursor_set(int on)
{
    if (!cursor) {
        cursor = theme_box(lv_layer_top(), 0, 0, CURSOR_SIZE, CURSOR_SIZE, C_BG);
        lv_obj_set_style_bg_opa(cursor, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(cursor, 2, 0);
        cursor_text = theme_label(lv_layer_top(), &font_label_12, C_ACCENT, 1);
        lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cursor_text, LV_OBJ_FLAG_HIDDEN);
    }
    if (on) {
        lv_obj_clear_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cursor_text, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cursor_text, LV_OBJ_FLAG_HIDDEN);
    }
}

int gesture_cursor_on(void)
{
    return cursor && !lv_obj_has_flag(cursor, LV_OBJ_FLAG_HIDDEN);
}

void gesture_tick(void)
{
    hold_tick();
    if (!gesture_cursor_on())
        return;
    char text[24];
    lv_snprintf(text, sizeof text, "%d,%d%s", last.x, last.y, last.pressed ? "" : " \xC2\xB7 SOLTO");
    theme_set_text(cursor_text, text);
    theme_set_border(cursor, last.pressed ? C_ACCENT : C_OFF);
    lv_obj_set_pos(cursor, last.x - CURSOR_SIZE / 2, last.y - CURSOR_SIZE / 2);
    /* the coordinates go to the side of the finger that has room */
    int tx = last.x + 12 < UI_W - 90 ? last.x + 12 : last.x - 100;
    int ty = last.y > 20 ? last.y - 20 : last.y + 12;
    lv_obj_set_pos(cursor_text, tx, ty);
}
