#include "ui/alert.h"
#include "ui/i18n.h"
#include "ui/theme.h"

#define BOX_W 360

static lv_obj_t *veil, *title, *text, *hint;

static void hide(void)
{
    lv_obj_add_flag(veil, LV_OBJ_FLAG_HIDDEN);
}

static void tapped(lv_event_t *e)
{
    (void)e;
    hide();
}

static void create(void)
{
    /* the veil covers the screen and takes the touches meant for the screen below */
    veil = theme_box(lv_layer_top(), 0, 0, UI_W, UI_H, C_BG);
    lv_obj_set_style_bg_opa(veil, LV_OPA_80, 0);
    lv_obj_add_flag(veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(veil, tapped, LV_EVENT_CLICKED, 0);

    lv_obj_t *box = theme_box(veil, 0, 0, BOX_W, 10, C_LINE);
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, rgb(C_CLIP), 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_set_style_pad_row(box, 8, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(box);

    title = theme_label(box, &font_value_20, C_CLIP, 1);
    text = theme_label(box, &font_body_14, C_TEXT, 0);
    lv_obj_set_width(text, BOX_W - 32);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    hint = theme_label(box, &font_label_12, C_LABEL, 1);
}

void alert_show(const char *t, const char *body)
{
    if (!veil)
        create();
    theme_set_text(title, t);
    theme_set_text(text, body);
    theme_set_text(hint, T(MSG_SYS_ALERT_HINT));       /* here: the language may have changed since create() */
    lv_obj_clear_flag(veil, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(veil);
}

void alert_keep_on_top(void)
{
    if (alert_shown())
        lv_obj_move_foreground(veil);
}

int alert_shown(void)
{
    return veil && !lv_obj_has_flag(veil, LV_OBJ_FLAG_HIDDEN);
}

int alert_input(const struct input_event *ev)
{
    static int closer = -1;                 /* the button that closed the alert, until it is released */
    if (!alert_shown()) {
        if (closer < 0 || ev->id != closer || (ev->type != IN_HOLD && ev->type != IN_RELEASE))
            return 0;
        if (ev->type == IN_RELEASE)
            closer = -1;
        return 1;
    }
    if (ev->type == IN_PRESS) {
        hide();
        closer = ev->id;
    }
    return 1;
}
