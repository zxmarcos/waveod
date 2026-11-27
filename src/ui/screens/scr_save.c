/*
 * Save: choose where the edited preset goes, then confirm. Not in the "Display Multiefeitos" design; drawn
 * in its language (big number of the destination, its current content, two wide buttons).
 *
 * Controls: MENU or knob 1 turns = destination, knob 2 = destination bank, footswitches A-D = letter in the
 * destination bank, BANK up / down = bank, CTRL = name, MENU press or SAVE = save, EFX = swap (the edited preset
 * goes to the destination, the destination's preset to where it came from), EXIT = cancel.
 * Touch: SALVAR / TROCAR / NOME / CANCELAR, the < > buttons, swipe the destination sideways (one preset) or up / down (bank);
 * ‹ = cancel.
 */
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define BODY_Y    UI_TOPBAR_H
#define BUTTONS_H 56
#define BODY_H    (UI_H - BODY_Y - BUTTONS_H)
#define ARROW     40

static struct {
    lv_obj_t *body, *caption, *number, *name, *state, *as, *hint;
    lv_obj_t *arrows[2];
    lv_obj_t *cancel, *save, *save_text;
} s SDRAM_BSS;

static void move(struct ui_ctx *ui, int d)
{
    ui->save_target = ((ui->save_target + d) % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT;
    ui->rev++;
}

static void leave(struct ui_ctx *ui)
{
    (void)ui;
    ui_back();
}

static void save(struct ui_ctx *ui)
{
    char label[8], text[48];
    theme_set_text(s.save_text, T(MSG_SAVE_WRITING));
    lv_refr_now(NULL);                      /* the write blocks the loop for a moment */
    preset_label(ui->save_target, label);
    if (rig_save(ui->rig, ui->save_target) == 0) {
        lv_snprintf(text, sizeof text, T(MSG_SAVE_SAVED), label);
        ui_changed();
        ui_toast(text);
        leave(ui);
    }
    theme_set_text(s.save_text, T(MSG_SCREENS_SAVE));
}

static void swap(struct ui_ctx *ui)
{
    char from[8], to[8], text[48];
    if (ui->save_target == ui->rig->index) {
        ui_toast(T(MSG_SAVE_PICK_OTHER));
        return;
    }
    preset_label(ui->rig->index, from);
    preset_label(ui->save_target, to);
    theme_set_text(s.save_text, T(MSG_SAVE_WRITING));
    lv_refr_now(NULL);
    if (rig_swap(ui->rig, ui->save_target) == 0) {
        lv_snprintf(text, sizeof text, T(MSG_SAVE_SWAPPED), from, to);
        ui_changed();
        ui_toast(text);
        leave(ui);
    }
    theme_set_text(s.save_text, T(MSG_SCREENS_SAVE));
}

/* ---------------------------------------------------------------- touch */

static void on_body(struct ui_ctx *ui, const struct gesture *g)
{
    int d = gesture_swipe_x(g);
    if (d)
        move(ui, -d);                       /* finger to the left = next */
    d = gesture_swipe_y(g);
    if (d)
        move(ui, d < 0 ? 4 : -4);
}

static void on_arrow(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP || g->kind == GESTURE_LONG)
        move(ui, g->tag);
}

static void on_button(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    if (g->tag == 1)
        save(ui);
    else if (g->tag == 3)
        swap(ui);
    else if (g->tag == 2)
        ui_go(SCR_NAME);
    else
        leave(ui);
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *button(lv_obj_t *root, int x, int w, const char *text, uint32_t bg, uint32_t fg, int tag,
                        lv_obj_t **label)
{
    lv_obj_t *b = theme_box(root, x, UI_H - BUTTONS_H + 8, w, BUTTONS_H - 16, bg);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, rgb(bg == C_BG ? C_BORDER_OFF : bg), 0);
    lv_obj_t *t = theme_label(b, &font_value_20, fg, 1);
    lv_label_set_text(t, text);
    lv_obj_center(t);
    gesture_bind(b, on_button, tag);
    if (label)
        *label = t;
    return b;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    s.body = theme_box(root, 0, BODY_Y, UI_W, BODY_H, C_BG);
    gesture_bind(s.body, on_body, 0);
    s.caption = theme_label(root, &font_label_12, C_LABEL, 1);
    lv_label_set_text(s.caption, T(MSG_SAVE_SAVE_TO));
    lv_obj_set_pos(s.caption, UI_PAD, BODY_Y + 12);
    s.number = theme_label(root, &font_hero_76, C_ACCENT, -1);
    lv_obj_set_pos(s.number, UI_PAD, BODY_Y + 32);
    s.name = theme_label(root, &font_title_40, C_TEXT, 0);
    lv_label_set_long_mode(s.name, LV_LABEL_LONG_DOT);
    s.state = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_label_set_long_mode(s.state, LV_LABEL_LONG_DOT);
    s.as = theme_label(root, &font_body_14, C_TEXT, 0);
    lv_label_set_long_mode(s.as, LV_LABEL_LONG_DOT);
    s.hint = theme_label(root, &font_label_12, C_OFF, 1);
    lv_label_set_text(s.hint, T(MSG_SAVE_HINT));
    lv_obj_set_pos(s.hint, UI_PAD, BODY_Y + BODY_H - 22);
    for (int i = 0; i < 2; i++) {
        s.arrows[i] = theme_box(root, UI_W - UI_PAD - (2 - i) * ARROW - (1 - i) * 8, BODY_Y + BODY_H - 30 - ARROW,
                                ARROW, ARROW, C_BG);
        lv_obj_set_style_radius(s.arrows[i], 3, 0);
        lv_obj_set_style_border_width(s.arrows[i], 2, 0);
        lv_obj_set_style_border_color(s.arrows[i], rgb(C_BORDER_OFF), 0);
        lv_obj_t *t = theme_label(s.arrows[i], &font_value_20, C_LABEL, 0);
        lv_label_set_text(t, i ? ">" : "<");
        lv_obj_center(t);
        gesture_bind(s.arrows[i], on_arrow, i ? 1 : -1);
    }
    theme_box(root, 0, UI_H - BUTTONS_H, UI_W, 1, C_LINE);
    int bw = (UI_W - 2 * UI_PAD - 3 * 8) / 4;
    s.cancel = button(root, UI_PAD, bw, T(MSG_SCREENS_CANCEL), C_BG, C_LABEL, 0, 0);
    button(root, UI_PAD + bw + 8, bw, T(MSG_SAVE_NAME), C_BG, C_TEXT, 2, 0);
    button(root, UI_PAD + 2 * (bw + 8), bw, T(MSG_SAVE_SWAP), C_BG, C_TEXT, 3, 0);
    s.save = button(root, UI_PAD + 3 * (bw + 8), bw, T(MSG_SCREENS_SAVE), C_ACCENT, C_BG, 1, &s.save_text);
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    char title[48], cur[40], label[8], name[PRESET_NAME_LEN + 1];
    ui_preset_title(ui, cur, sizeof cur);
    lv_snprintf(title, sizeof title, T(MSG_SAVE_TITLE), cur);
    top_bar_sub(title);

    int t = ui->save_target;
    preset_label(t, label);
    theme_set_text(s.number, label);
    enum preset_origin origin = presets_origin(&r->src, t);
    presets_name(&r->src, t, name);
    if (t == r->index)
        theme_set_text(s.name, r->preset.name[0] ? r->preset.name : T(MSG_SCREENS_UNNAMED));
    else
        theme_set_text(s.name, origin == PRESET_EMPTY ? T(MSG_SCREENS_EMPTY) : name);
    enum i18n_msg state = t == r->index ? MSG_SAVE_STATE_SAME
                         : origin == PRESET_USER    ? MSG_SAVE_STATE_USER
                         : origin == PRESET_FACTORY ? MSG_SAVE_STATE_FACTORY
                                                    : MSG_SAVE_STATE_FREE;
    theme_set_text(s.state, T(state));
    lv_obj_update_layout(s.number);
    int x = UI_PAD + lv_obj_get_width(s.number) + 19;
    int name_h = lv_font_get_line_height(&font_title_40);
    lv_obj_set_pos(s.name, x, BODY_Y + 40);
    lv_obj_set_width(s.name, UI_W - UI_PAD - x);
    lv_obj_set_pos(s.state, x + 2, BODY_Y + 40 + name_h + 2);
    lv_obj_set_width(s.state, UI_W - UI_PAD - x);
    char as[48];
    lv_snprintf(as, sizeof as, T(MSG_SAVE_SAVE_AS), r->preset.name[0] ? r->preset.name : T(MSG_SCREENS_UNNAMED));
    theme_set_text(s.as, as);
    lv_obj_set_pos(s.as, x + 2, BODY_Y + 40 + name_h + 22);
    lv_obj_set_width(s.as, UI_W - UI_PAD - x - 2 * ARROW - 16);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU) + ui_turned(ev, ENC_KNOB1);
    if (d) {
        move(ui, d);
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB2);
    if (d) {
        move(ui, 4 * d);
        return 1;
    }
    if (ev->type != IN_PRESS)
        return 0;
    uint32_t b = 1u << ev->id;
    if (b & (BTN_FS_A | BTN_FS_B | BTN_FS_C | BTN_FS_D)) {
        int letter = b == BTN_FS_A ? 0 : b == BTN_FS_B ? 1 : b == BTN_FS_C ? 2 : 3;
        ui->save_target = ui->save_target / 4 * 4 + letter;
        ui->rev++;
        return 1;
    }
    if (b == BTN_BANK_UP || b == BTN_BANK_DOWN) {
        move(ui, b == BTN_BANK_UP ? 4 : -4);
        return 1;
    }
    if (b == BTN_MENU || b == BTN_SAVE) {
        save(ui);
        return 1;
    }
    if (b == BTN_CTRL) {
        ui_go(SCR_NAME);
        return 1;
    }
    if (b == BTN_EFX) {
        swap(ui);
        return 1;
    }
    if (b == BTN_EXIT) {
        leave(ui);
        return 1;
    }
    return 0;
}

const struct screen screen_save = { create, 0, refresh, input };
