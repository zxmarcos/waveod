/*
 * Preset or scene name (ui->name_scene): an on-screen keyboard, opened from the save screen or by holding a scene.
 * Not in the design; keys are the chain
 * blocks' outlined rectangles, the name is shown large on top with its length.
 *
 * Touch: tap the keys; hold APAGAR = clear the name. Without touch: MENU turns = key (knob 1 = row), MENU
 * press = type it. SAVE or OK = keep the name, EXIT = back without changes. Either way the save screen comes
 * back; the name is written with the preset.
 */
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define FIELD_Y   UI_TOPBAR_H
#define FIELD_H   46
#define KEYS_Y    (FIELD_Y + FIELD_H + 2)
#define ROWS      5
#define PITCH_Y   ((UI_H - KEYS_Y - 2) / ROWS)
#define PITCH_X   45
#define KEY_GAP   4
#define KEYS_X    (UI_PAD + (UI_W - 2 * UI_PAD - (10 * PITCH_X - KEY_GAP)) / 2)

static const char *const letters[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM.'/" };

enum { K_CHARS = 40, K_SHIFT = K_CHARS, K_SPACE, K_BACK, K_OK, K_COUNT };

/* bottom row: key, width in key units */
static const struct { int key, units; } bottom[] = { { K_SHIFT, 2 }, { K_SPACE, 4 }, { K_BACK, 2 }, { K_OK, 2 } };

static struct {
    lv_obj_t *text, *caret, *count;
    lv_obj_t *keys[K_COUNT], *labels[K_COUNT];
    char name[PRESET_NAME_LEN + 1];
    int selected, lower;
    int max;                                /* PRESET_NAME_LEN or SCENE_NAME_LEN */
} s SDRAM_BSS;

static char key_char(int k)
{
    char c = letters[k / 10][k % 10];
    return s.lower && c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static void type(int k)
{
    size_t len = strlen(s.name);
    if (k < K_CHARS || k == K_SPACE) {
        if ((int)len < s.max && !(k == K_SPACE && len == 0)) {
            s.name[len] = k == K_SPACE ? ' ' : key_char(k);
            s.name[len + 1] = 0;
        }
    } else if (k == K_BACK) {
        if (len)
            s.name[len - 1] = 0;
    } else if (k == K_SHIFT) {
        s.lower = !s.lower;
    }
}

static void finish(struct ui_ctx *ui, int keep)
{
    if (keep) {
        if (ui->name_key >= 0 && ui->name_key < CK_KEYS) {
            char *label = ui_ctl_table(ui)->label[ui->name_key];
            if (strcmp(label, s.name) != 0) {
                memcpy(label, s.name, CONTROL_LABEL);
                label[CONTROL_LABEL] = 0;
                ui_ctl_edited(ui);
            }
        } else if (ui->name_scene >= 0) {
            rig_scene_set_name(ui->rig, ui->name_scene, s.name);
        } else {
            rig_set_name(ui->rig, s.name);
        }
        ui_changed();
    }
    ui->name_scene = -1;
    ui->name_key = -1;
    ui_back();
}

static void press(struct ui_ctx *ui, int k)
{
    if (k == K_OK) {
        finish(ui, 1);
        return;
    }
    type(k);
    ui->rev++;
}

/* ---------------------------------------------------------------- touch */

static void on_key(struct ui_ctx *ui, const struct gesture *g)
{
    s.selected = g->tag;
    if (g->kind == GESTURE_TAP) {
        press(ui, g->tag);
    } else if (g->kind == GESTURE_LONG && g->tag == K_BACK) {
        s.name[0] = 0;
        ui->rev++;
    }
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *key_box(lv_obj_t *root, int k, int x, int y, int w)
{
    int ok = k == K_OK;
    lv_obj_t *b = theme_box(root, x, y, w, PITCH_Y - KEY_GAP, ok ? C_ACCENT : C_BG);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, rgb(ok ? C_ACCENT : C_BORDER_OFF), 0);
    lv_obj_set_style_bg_color(b, rgb(ok ? C_TEXT : C_TRACK), LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(b, rgb(C_TEXT), 0);
    lv_obj_set_style_outline_pad(b, 2, 0);
    s.labels[k] = theme_label(b, k < K_CHARS ? &font_value_20 : &font_tag_12, ok ? C_BG : C_TEXT, k < K_CHARS ? 0 : 1);
    lv_obj_center(s.labels[k]);
    gesture_bind(b, on_key, k);
    return b;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    theme_box(root, 0, FIELD_Y + FIELD_H, UI_W, 1, C_LINE);
    s.text = theme_label(root, &font_value_26, C_TEXT, 0);
    lv_obj_set_pos(s.text, UI_PAD, FIELD_Y + (FIELD_H - 30) / 2);
    s.caret = theme_box(root, UI_PAD, FIELD_Y + FIELD_H - 10, 14, 3, C_ACCENT);
    s.count = theme_label(root, &font_label_12, C_LABEL, 1);
    for (int k = 0; k < K_CHARS; k++)
        s.keys[k] = key_box(root, k, KEYS_X + (k % 10) * PITCH_X, KEYS_Y + (k / 10) * PITCH_Y, PITCH_X - KEY_GAP);
    int x = KEYS_X;
    for (unsigned i = 0; i < sizeof bottom / sizeof bottom[0]; i++) {
        int w = bottom[i].units * PITCH_X - KEY_GAP;
        s.keys[bottom[i].key] = key_box(root, bottom[i].key, x, KEYS_Y + 4 * PITCH_Y, w);
        x += bottom[i].units * PITCH_X;
    }
    theme_set_text(s.labels[K_SPACE], T(MSG_NAME_SPACE));
    theme_set_text(s.labels[K_BACK], T(MSG_NAME_DELETE));
    theme_set_text(s.labels[K_OK], "OK");
    for (int k = K_SPACE; k <= K_OK; k++)
        lv_obj_center(s.labels[k]);
}

static void enter(struct ui_ctx *ui)
{
    if (ui->name_key >= 0 && ui->name_key < CK_KEYS) {
        memcpy(s.name, ui_ctl_table(ui)->label[ui->name_key], CONTROL_LABEL + 1);
        s.max = CONTROL_LABEL;
    } else if (ui->name_scene >= 0) {
        rig_scene_name(ui->rig, ui->name_scene, s.name);
        s.max = SCENE_NAME_LEN;
    } else {
        memcpy(s.name, ui->rig->preset.name, sizeof s.name);
        s.max = PRESET_NAME_LEN;
    }
    s.selected = K_OK;
}

static void refresh(struct ui_ctx *ui)
{
    char title[48], cur[40], count[16];
    ui_preset_title(ui, cur, sizeof cur);
    if (ui->name_key >= 0)
        lv_snprintf(title, sizeof title, T(MSG_NAME_TITLE_FOOTSWITCH), ui->ctl_global ? "GLOBAL" : cur);
    else if (ui->name_scene >= 0)
        lv_snprintf(title, sizeof title, T(MSG_NAME_TITLE_SCENE), ui->name_scene + 1, cur);
    else
        lv_snprintf(title, sizeof title, T(MSG_NAME_TITLE), cur);
    top_bar_sub(title);
    theme_set_text(s.text, s.name);
    lv_obj_update_layout(s.text);
    size_t len = strlen(s.name);
    lv_obj_set_x(s.caret, UI_PAD + lv_obj_get_width(s.text) + 2);
    theme_set_bg(s.caret, (int)len < s.max ? C_ACCENT : C_OFF);
    lv_snprintf(count, sizeof count, "%d / %d", (int)len, s.max);
    theme_set_text(s.count, count);
    lv_obj_align(s.count, LV_ALIGN_TOP_RIGHT, -UI_PAD, FIELD_Y + (FIELD_H - 14) / 2);
    for (int k = 0; k < K_CHARS; k++) {
        char c[2] = { key_char(k), 0 };
        theme_set_text(s.labels[k], c);
        lv_obj_center(s.labels[k]);
    }
    theme_set_text(s.labels[K_SHIFT], s.lower ? "ABC" : "abc");
    lv_obj_center(s.labels[K_SHIFT]);
    for (int k = 0; k < K_COUNT; k++)
        if (lv_obj_get_style_outline_width(s.keys[k], 0) != (k == s.selected ? 2 : 0))
            lv_obj_set_style_outline_width(s.keys[k], k == s.selected ? 2 : 0, 0);
}

/* the key below / above in the encoder order: rows of 10, then the bottom row */
static int row_move(int k, int d)
{
    if (k >= K_CHARS) {
        int col = k == K_SHIFT ? 0 : k == K_SPACE ? 3 : k == K_BACK ? 6 : 8;
        return d < 0 ? 30 + col : k;
    }
    int to = k + 10 * d;
    if (to < 0)
        return k;
    if (to >= K_CHARS) {
        int col = k % 10;
        return col < 2 ? K_SHIFT : col < 6 ? K_SPACE : col < 8 ? K_BACK : K_OK;
    }
    return to;
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        s.selected = ((s.selected + d) % K_COUNT + K_COUNT) % K_COUNT;
        ui->rev++;
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB1);
    if (d) {
        for (int i = 0; i < (d < 0 ? -d : d); i++)
            s.selected = row_move(s.selected, d < 0 ? -1 : 1);
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        press(ui, s.selected);
        return 1;
    }
    if (ui_pressed(ev, BTN_SAVE)) {
        finish(ui, 1);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT)) {
        finish(ui, 0);
        return 1;
    }
    return 1;                               /* nothing else applies while typing */
}

/* left any way (EXIT held goes home): the next opening names the preset unless asked otherwise */
static void leave(struct ui_ctx *ui)
{
    ui->name_scene = -1;
    ui->name_key = -1;
}

const struct screen screen_name = { create, enter, refresh, input, leave };
