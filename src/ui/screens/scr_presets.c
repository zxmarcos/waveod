/*
 * PRESETS (the original's bank screen; waveod has a list): the 240 presets as 60 banks of four, a row per bank
 * with A-D like the footswitches. The active preset is lit amber, the cursor framed.
 *
 * Opened by: a tap on the preset in the top bar or on the name on the PRESET tab, the footswitch function BANCOS,
 * MIDI CC 26-28.
 *
 * Controls: MENU turns = cursor, knob 1 = cursor a bank down / up, MENU press = load the cursor's preset (the list
 * stays), EXIT = back.
 * Footswitches (the original's bank menu): BANK - / + = the cursor a bank down / up (nothing loads), A-D = load
 * that preset of the cursor's bank and go back. LEDs: BANK - / + blinking cyan, A-D cyan, the active preset's
 * key white (ui/leds.c).
 * Touch: tap a preset = load it (the list stays), drag the list up / down = scroll it.
 */
#include "fw/presets.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define BANKS     (PRESET_COUNT / 4)
#define HEAD_Y    (UI_TOPBAR_H + 4)
#define HEAD_H    16
#define LIST_Y    (HEAD_Y + HEAD_H)
#define ROWS      5
#define ROW_H     ((UI_H - LIST_Y - 2) / ROWS)
#define BANK_W    30
#define GAP       5
#define CELL_X    (UI_PAD + BANK_W)
#define CELL_W    ((UI_W - UI_PAD - CELL_X - 3 * GAP) / 4)
#define TRACK_X   8                         /* the scroll mark, this far from the right edge */
#define CELL_H    (ROW_H - 4)
#define C_FRAME   0x2A2926
#define C_ACTIVE  0x161514

static struct {
    lv_obj_t *bank[ROWS], *cell[ROWS][4], *name[ROWS][4], *track, *thumb;
    char names[PRESET_COUNT][PRESET_NAME_LEN + 1];
    int top, drag_top;                      /* the first bank shown */
    int shown_cursor;                       /* the cursor the list last scrolled to */
    int active;                             /* the preset that was active at the last refresh */
    uint8_t down;                           /* footswitches pressed on this screen (A-D, BANK -, BANK +) */
} s SDRAM_BSS;

static int clamp_top(int top)
{
    return top < 0 ? 0 : top > BANKS - ROWS ? BANKS - ROWS : top;
}

static void set_cursor(struct ui_ctx *ui, int index)
{
    ui->preset_cursor = (index % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT;
    ui->rev++;
}

/* keep the cursor's bank in view when the cursor moves (a list scrolled by hand stays where it is) */
static void follow(struct ui_ctx *ui)
{
    if (ui->preset_cursor == s.shown_cursor)
        return;
    s.shown_cursor = ui->preset_cursor;
    int bank = ui->preset_cursor / 4;
    if (bank < s.top || bank >= s.top + ROWS)
        s.top = clamp_top(bank - ROWS / 2);
}

static void load(struct ui_ctx *ui, int index)
{
    set_cursor(ui, index);
    if (index != ui->rig->index)
        ui_load_preset(index);
}

/* ---------------------------------------------------------------- touch */

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_DRAG && g->vertical) {           /* scroll */
        if (g->first)
            s.drag_top = s.top;
        s.top = clamp_top(s.drag_top - g->dy / ROW_H);
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        load(ui, (s.top + g->tag / 4) * 4 + g->tag % 4);
    }
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    static const char *const letters[4] = { "A", "B", "C", "D" };
    for (int c = 0; c < 4; c++) {
        lv_obj_t *h = theme_label(root, &font_tag_12, C_OFF, 1);
        lv_label_set_text_static(h, letters[c]);
        lv_obj_set_pos(h, CELL_X + c * (CELL_W + GAP) + 6, HEAD_Y);
    }
    for (int r = 0; r < ROWS; r++) {
        int y = LIST_Y + r * ROW_H;
        s.bank[r] = theme_label(root, &font_value_20, C_LABEL, 0);
        lv_obj_set_pos(s.bank[r], UI_PAD - 4, y + (CELL_H - 22) / 2);
        for (int c = 0; c < 4; c++) {
            lv_obj_t *b = theme_box(root, CELL_X + c * (CELL_W + GAP), y, CELL_W, CELL_H, C_BG);
            lv_obj_set_style_radius(b, 3, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            gesture_bind(b, on_cell, r * 4 + c);
            s.cell[r][c] = b;
            lv_obj_t *t = theme_label(b, &font_body_14, C_LABEL, 0);
            lv_obj_set_width(t, CELL_W - 10);
            lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_max_height(t, CELL_H - 4, 0);
            s.name[r][c] = t;
        }
    }
    /* where the five banks shown are among the 60 */
    s.track = theme_box(root, UI_W - TRACK_X, LIST_Y, 3, ROWS * ROW_H - 4, C_LINE);
    lv_obj_set_style_radius(s.track, 1, 0);
    s.thumb = theme_box(s.track, 0, 0, 3, (ROWS * ROW_H - 4) * ROWS / BANKS, C_OFF);
    lv_obj_set_style_radius(s.thumb, 1, 0);
}

static void enter(struct ui_ctx *ui)
{
    for (int i = 0; i < PRESET_COUNT; i++)          /* the names (a save or rename since: read again) */
        presets_name(&ui->rig->src, i, s.names[i]);
    s.down = 0;
    s.shown_cursor = -1;
    s.active = ui->rig->index;
    ui->preset_cursor = ui->rig->index;
    follow(ui);
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    if (r->index != s.active) {             /* loaded here or elsewhere (MIDI, the editor): the cursor follows */
        s.active = r->index;
        ui->preset_cursor = r->index;
    }
    follow(ui);
    top_bar_sub(T(MSG_MENU_PRESETS));
    lv_obj_set_y(s.thumb, (ROWS * ROW_H - 4) * s.top / BANKS);
    char text[PRESET_NAME_LEN + 4];
    for (int row = 0; row < ROWS; row++) {
        int bank = s.top + row;
        lv_snprintf(text, sizeof text, "%02d", bank + 1);
        theme_set_text(s.bank[row], text);
        theme_set_color(s.bank[row], bank == ui->preset_cursor / 4 ? C_TEXT : C_OFF);
        for (int c = 0; c < 4; c++) {
            int i = bank * 4 + c, active = i == r->index, cursor = i == ui->preset_cursor;
            const char *name = active ? r->preset.name : s.names[i];   /* the active one: as edited */
            lv_obj_t *b = s.cell[row][c], *t = s.name[row][c];
            if (name[0])
                lv_snprintf(text, sizeof text, "%s%s", name, active && r->dirty ? "*" : "");
            else
                lv_snprintf(text, sizeof text, "%s", T(MSG_SCREENS_EMPTY));
            theme_set_text(t, text);
            theme_set_color(t, active ? C_ACCENT : cursor ? C_TEXT : name[0] ? C_LABEL : C_DIM);
            lv_obj_align(t, LV_ALIGN_LEFT_MID, 4, 0);
            theme_set_bg(b, active || cursor ? C_ACTIVE : C_BG);
            theme_set_border(b, cursor ? (active ? C_ACCENT : C_TEXT) : active ? C_ACCENT : C_FRAME);
            lv_obj_set_style_border_width(b, cursor ? 2 : 1, 0);
        }
    }
}

/* footswitch k (0..3 = A..D, 4 = BANK -, 5 = BANK +) of this screen, -1 = another control */
static int key(const struct input_event *ev)
{
    static const uint32_t keys[6] = { BTN_FS_A, BTN_FS_B, BTN_FS_C, BTN_FS_D, BTN_BANK_DOWN, BTN_BANK_UP };
    if (ev->type == IN_TURN)
        return -1;
    for (int k = 0; k < 6; k++)
        if (keys[k] == 1u << ev->id)
            return k;
    return -1;
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int k = key(ev);
    if (k >= 0) {
        uint8_t bit = (uint8_t)(1u << k);
        if (ev->type == IN_PRESS) {
            s.down |= bit;
            if (k >= 4)
                set_cursor(ui, ui->preset_cursor + (k == 5 ? 4 : -4));
        } else if (ev->type == IN_RELEASE && (s.down & bit)) {   /* pressed here (not the key that opened us) */
            s.down &= (uint8_t)~bit;
            if (k < 4) {
                load(ui, ui->preset_cursor / 4 * 4 + k);
                ui_back();
            }
        }
        return 1;
    }
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        set_cursor(ui, ui->preset_cursor + d);
        return 1;
    }
    if ((d = ui_turned(ev, ENC_KNOB1))) {
        set_cursor(ui, ui->preset_cursor + 4 * d);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        load(ui, ui->preset_cursor);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_presets = { create, enter, refresh, input };
