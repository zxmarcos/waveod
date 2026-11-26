/*
 * Assign a parameter (hold a parameter on the edit screen, or EXP there for the row's last parameter turned): the
 * footswitches and the pedals drawn where they sit, like CONTROLES, the ones that already move the parameter lit
 * with their values. While the screen is shown the next footswitch pressed (held = on the long press) or pedal
 * moved or MIDI CC received gets a link to the parameter, as does tapping a card; then the destination screen opens
 * on it for its values. Footswitches get it in stomp mode, the toe switch, the pedals and MIDI in every mode. Below:
 * UM VALOR POR CENA (a scene parameter), MIDI (lit when a CC moves the parameter) and the parameter (knob 1: another
 * of the block). Not in the design.
 *
 * Controls: MENU turns = a card (MIDI last), MENU press = its link (a new one when unlit; MIDI: send a CC), MENU
 * hold = remove the card's links to the parameter, knob 1 = the parameter, EFX = UM VALOR POR CENA, EXIT or EXP =
 * back.
 * Touch: tap a card = as MENU press, hold it = remove; tap UM VALOR POR CENA = flip it.
 */
#include "model/control.h"
#include "model/param_format.h"
#include "platform/sdram.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define CARDS    8
#define GRID_GAP 7
#define GRID_Y   (UI_TOPBAR_H + 8)
#define BAR_H    34
#define BAR_Y    (UI_H - 4 - BAR_H)
#define CARD_W   ((UI_W - 2 * UI_PAD - 3 * GRID_GAP) / 4)
#define CARD_H   ((BAR_Y - 6 - GRID_Y - GRID_GAP) / 2)
#define CARD_PAD 7
#define LINE_Y   34
#define LINE_H   18
#define SCENE_W  172
#define MIDI_X   (UI_PAD + SCENE_W + 8)
#define MIDI_W   150
#define MIDI     CARDS                      /* the MIDI box, after the cards */

struct card {
    lv_obj_t *box, *key, *line[2];
};

static struct {
    struct card cards[CARDS];
    lv_obj_t *scene, *scene_text, *param_text, *midi, *midi_text;
    int sel;
} s SDRAM_BSS;

/* cards: A-D, then EXP 1, EXP 2, BANK -, BANK + (the top row, left to right) */
static const uint8_t card_key[CARDS] = { CK_A, CK_B, CK_C, CK_D, CK_EXP1A, CK_EXP2, CK_BANK_DOWN, CK_BANK_UP };
static const char *const card_name[CARDS] = { "A", "B", "C", "D", "EXP 1", "EXP 2", "\xE2\x88\x92", "+" };

static void cell(int i, int *x, int *y)
{
    int col = i < 4 ? i : i - 4;
    *x = UI_PAD + col * (CARD_W + GRID_GAP);
    *y = GRID_Y + (i < 4 ? CARD_H + GRID_GAP : 0);
}

static int pedal_card(int i)
{
    return card_key[i] >= CK_TOE;
}

static int on_card(int i, int key)
{
    if (i == MIDI)
        return CK_IS_MIDI(key);
    if (card_key[i] == CK_EXP1A)
        return key == CK_EXP1A || key == CK_EXP1B || key == CK_TOE;
    return key == card_key[i];
}

static int param(struct ui_ctx *ui, struct rig_param *out)
{
    return rig_param(ui->rig, ui->link_slot, ui->link_param, out) == 0 ? out->def.idx : -1;
}

/* the card's first link to the parameter (from table index `from`), or -1 */
static int card_link(struct ui_ctx *ui, int i, int from)
{
    struct rig_param p;
    int knob = param(ui, &p);
    for (int k = control_find_param(ui->rig, ui->link_slot, knob, from); knob >= 0 && k >= 0;
         k = control_find_param(ui->rig, ui->link_slot, knob, k + 1))
        if (on_card(i, ui->rig->preset.controls.c[k].key))
            return k;
    return -1;
}

static void edited(struct ui_ctx *ui)
{
    rig_set_dirty(ui->rig, 1);
    ui_changed();
}

/* ---------------------------------------------------------------- changes */

/* a new link from key (also what the learning gives), then its values on the destination screen */
static void add(struct ui_ctx *ui, int key, int hold)
{
    int i = control_add_param(ui->rig, key, CM_STOMP, hold ? CW_HOLD : CW_PRESS, ui->link_slot, ui->link_param);
    if (i < 0) {
        ui_toast("Tabela de controles cheia");
        ui_learn(add);
        return;
    }
    edited(ui);
    ui_target(i, 0);
}

static void open(struct ui_ctx *ui, int i)
{
    int k = card_link(ui, i, 0);
    if (k >= 0)
        ui_target(k, 0);
    else if (i == MIDI)
        ui_toast("MANDE UM CC MIDI PARA LIGAR");     /* learning is on */
    else
        add(ui, card_key[i] == CK_EXP1A && ui->rig->preset.exp_group ? CK_EXP1B : card_key[i], 0);
}

static void remove_card(struct ui_ctx *ui, int i)
{
    int n = 0;
    for (int k = card_link(ui, i, 0); k >= 0; k = card_link(ui, i, k)) {
        preset_control_remove(&ui->rig->preset.controls, k);
        n++;
    }
    if (n) {
        ui_toast("LIGA\xC3\x87\xC3\x83O REMOVIDA");
        edited(ui);
    }
}

static void flip_scene(struct ui_ctx *ui)
{
    rig_scene_param_toggle(ui->rig, ui->link_slot, ui->link_param);
    ui_changed();
}

static void on_card_touch(struct ui_ctx *ui, const struct gesture *g)
{
    s.sel = g->tag;
    if (g->kind == GESTURE_TAP)
        open(ui, g->tag);
    else if (g->kind == GESTURE_LONG)
        remove_card(ui, g->tag);
}

static void on_scene(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        flip_scene(ui);
}

/* ---------------------------------------------------------------- text */

/* "A 0 » 100", "SEG 50 » 100", "PRESET 0 » 1" */
static void link_line(struct ui_ctx *ui, const struct control *l, char *out, unsigned size)
{
    struct rig_param p;
    char lo[16], hi[16];
    if (control_param_index(ui->rig, l, &p) < 0) {
        out[0] = 0;
        return;
    }
    param_format(&p.def, l->off, 0, lo, sizeof lo);
    param_format(&p.def, l->on, 0, hi, sizeof hi);
    char cc[12] = "";
    if (CK_IS_MIDI(l->key))
        lv_snprintf(cc, sizeof cc, "CC %d ", l->key - CK_MIDI);
    const char *pre = CK_IS_MIDI(l->key) ? cc : l->key == CK_EXP1A ? "A " : l->key == CK_EXP1B ? "B " :
                      l->key == CK_TOE ? "BICO " :
                      l->key >= CK_TOE ? "" : l->when == CW_HOLD ? "SEG " : l->when == CW_MOMENT ? "MOM " :
                      !(l->modes & CM_STOMP) ? (l->modes & CM_SCENE ? "CENAS " : "PRESET ") : "";
    lv_snprintf(out, size, "%s%s \xC2\xBB %s", pre, lo, hi);
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    for (int i = 0; i < CARDS; i++) {
        struct card *c = &s.cards[i];
        int x, y;
        cell(i, &x, &y);
        c->box = theme_box(root, x, y, CARD_W, CARD_H, C_BG);
        gesture_bind_hold(c->box, on_card_touch, i, 0);
        lv_obj_set_style_radius(c->box, 3, 0);
        lv_obj_set_style_border_width(c->box, 2, 0);
        lv_obj_set_style_outline_color(c->box, rgb(C_ACCENT), 0);
        lv_obj_set_style_outline_pad(c->box, 2, 0);
        c->key = theme_label(c->box, pedal_card(i) ? &font_value_20 : &font_letter_26, C_TEXT, 0);
        lv_obj_set_pos(c->key, CARD_PAD, pedal_card(i) ? CARD_PAD : CARD_PAD - 6);
        lv_label_set_text_static(c->key, card_name[i]);

        for (int n = 0; n < 2; n++) {
            c->line[n] = theme_label(c->box, n ? &font_body_14 : &font_value_20, C_OFF, 0);
            lv_obj_set_width(c->line[n], CARD_W - 2 * CARD_PAD - 4);
            lv_obj_set_height(c->line[n], lv_font_get_line_height(n ? &font_body_14 : &font_value_20));
            lv_label_set_long_mode(c->line[n], LV_LABEL_LONG_DOT);
            lv_obj_set_pos(c->line[n], CARD_PAD, LINE_Y + n * (LINE_H + 6));
        }
    }
    s.scene = theme_box(root, UI_PAD, BAR_Y, SCENE_W, BAR_H, C_BG);
    lv_obj_set_style_radius(s.scene, 3, 0);
    lv_obj_set_style_border_width(s.scene, 2, 0);
    gesture_bind(s.scene, on_scene, 0);
    s.scene_text = theme_label(s.scene, &font_tag_12, C_LABEL, 1);
    lv_obj_center(s.scene_text);
    s.midi = theme_box(root, MIDI_X, BAR_Y, MIDI_W, BAR_H, C_BG);
    lv_obj_set_style_radius(s.midi, 3, 0);
    lv_obj_set_style_border_width(s.midi, 2, 0);
    lv_obj_set_style_outline_color(s.midi, rgb(C_ACCENT), 0);
    lv_obj_set_style_outline_pad(s.midi, 2, 0);
    gesture_bind_hold(s.midi, on_card_touch, MIDI, 0);
    s.midi_text = theme_label(s.midi, &font_tag_12, C_LABEL, 1);
    lv_obj_set_width(s.midi_text, MIDI_W - 12);
    lv_label_set_long_mode(s.midi_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s.midi_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s.midi_text);
    s.param_text = theme_label(root, &font_tag_12, C_LABEL, 1);
    lv_obj_set_width(s.param_text, UI_W - UI_PAD - (MIDI_X + MIDI_W + 8));
    lv_obj_set_height(s.param_text, lv_font_get_line_height(&font_tag_12));
    lv_label_set_long_mode(s.param_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s.param_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s.param_text, MIDI_X + MIDI_W + 8, BAR_Y + (BAR_H - 14) / 2);
}

static void enter(struct ui_ctx *ui)
{
    ui_learn(add);
}

static void refresh(struct ui_ctx *ui)
{
    struct rig_param p;
    char title[64], label[24] = "?";
    if (param(ui, &p) >= 0)
        ui_param_label(&p, label, sizeof label);
    lv_snprintf(title, sizeof title, "%s \xC2\xBB DE ONDE \xC2\xB7 PISE, MEXA, MANDE OU TOQUE", label);
    top_bar_sub(title);

    uint32_t col = rig_slot_used(ui->rig, ui->link_slot) ? theme_menu_color(ui->rig->preset.slot[ui->link_slot].menu)
                                                          : C_ACCENT;
    const struct preset_controls *c = &ui->rig->preset.controls;
    for (int i = 0; i < CARDS; i++) {
        struct card *cd = &s.cards[i];
        int k = card_link(ui, i, 0), lit = k >= 0;
        char text[32] = "", more[24] = "";
        if (lit) {
            link_line(ui, &c->c[k], text, sizeof text);
            int n = 0;
            for (int j = k; j >= 0; j = card_link(ui, i, j + 1))
                n++;
            if (n > 1)
                lv_snprintf(more, sizeof more, "+%d LIGA\xC3\x87\xC3\x95" "ES", n - 1);
        } else {
            int others = 0;                         /* what the key does already (stomp mode) */
            for (int j = 0; j < c->count; j++)
                others += on_card(i, c->c[j].key) && (pedal_card(i) || (c->c[j].modes & CM_STOMP));
            lv_snprintf(text, sizeof text, "+");
            if (others)
                lv_snprintf(more, sizeof more, "J\xC3\x81 FAZ %d", others);
        }
        theme_set_text(cd->line[0], text);
        theme_set_color(cd->line[0], lit ? C_TEXT : C_DIM);
        theme_set_text(cd->line[1], more);
        theme_set_color(cd->line[1], lit ? C_LABEL : C_DIM);

        theme_set_border(cd->box, lit ? col : 0x2A2926);
        theme_set_bg(cd->box, lit ? 0x161514 : C_BG);
        theme_set_color(cd->key, lit ? C_TEXT : C_OFF);
        lv_obj_set_style_outline_width(cd->box, i == s.sel ? 2 : 0, 0);
    }

    int m = card_link(ui, MIDI, 0);
    char mtext[40] = "MIDI: MANDE UM CC";
    if (m >= 0) {
        int n = 0;
        for (int j = m; j >= 0; j = card_link(ui, MIDI, j + 1))
            n++;
        link_line(ui, &c->c[m], mtext, sizeof mtext);
        if (n > 1)
            lv_snprintf(mtext + strlen(mtext), sizeof mtext - strlen(mtext), " +%d", n - 1);
    }
    theme_set_text(s.midi_text, mtext);
    theme_set_color(s.midi_text, m >= 0 ? C_TEXT : C_LABEL);
    theme_set_border(s.midi, m >= 0 ? col : 0x2A2926);
    theme_set_bg(s.midi, m >= 0 ? 0x161514 : C_BG);
    lv_obj_set_style_outline_width(s.midi, s.sel == MIDI ? 2 : 0, 0);
    lv_obj_center(s.midi_text);

    int scene = rig_scene_param(ui->rig, ui->link_slot, ui->link_param);
    theme_set_text(s.scene_text, scene ? "UM VALOR POR CENA: SIM" : "UM VALOR POR CENA: N\xC3\x83O");
    theme_set_color(s.scene_text, scene ? C_TEXT : C_LABEL);
    theme_set_border(s.scene, scene ? C_ACCENT : 0x2A2926);
    lv_obj_center(s.scene_text);
    char ptext[48];
    lv_snprintf(ptext, sizeof ptext, "%s \xC2\xB7 %s \xC2\xB7 K1", ui_slot_tag(ui, ui->link_slot), label);
    theme_set_text(s.param_text, ptext);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        s.sel = ((s.sel + d) % (CARDS + 1) + CARDS + 1) % (CARDS + 1);
        ui->rev++;
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB1);
    if (d) {
        int n = rig_param_count(ui->rig, ui->link_slot);
        if (n) {
            ui->link_param = ((ui->link_param + d) % n + n) % n;
            ui->rev++;
        }
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU) {
        remove_card(ui, s.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        open(ui, s.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {
        flip_scene(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_EXP)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_link = { create, enter, refresh, input, 0, 0 };
