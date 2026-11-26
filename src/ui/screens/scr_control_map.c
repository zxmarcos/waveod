/*
 * Controls (CTRL on the PRESET / STOMP / CENAS tabs): the footswitches and the
 * pedals drawn where they sit, like the stomp tab's cards (A-D along the bottom, BANK - and BANK + above C and D,
 * EXP 1 and EXP 2 over A and B), each with what it does in the mode shown: one line per link, held ones marked
 * SEG. EXP 1's card has its groups' links (A, B) and its toe switch's (BICO). Below, MIDI: the CCs that have links
 * (every mode), and KNOBS: the preset's quick knobs (opens the assignments, scr_assign.c; not on the global table).
 * Not in the design. On a preset the cards also show the global links that play there (the groups the
 * preset has none in) in grey; opened from GLOBAL › CONTROLES GLOBAIS (ui->ctl_global) it shows the global table.
 *
 * Controls: MENU turns = a card (MIDI and KNOBS last), MENU press = its links (scr_controls.c: the pairs and the active
 * preset's key are there too, on knob 1 of TECLA; MIDI: the first CC's, or CC 1), EFX = the mode shown (preset,
 * stomp, scenes), EXIT or CTRL = back.
 * Touch: tap a card = its links.
 */
#include "model/control.h"
#include "platform/sdram.h"
#include "ui/footswitch.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define CARDS    8
#define LINES    3
#define GRID_GAP 7
#define GRID_Y   (UI_TOPBAR_H + 8)
#define CARD_W   ((UI_W - 2 * UI_PAD - 3 * GRID_GAP) / 4)
#define BAR_H    28
#define BAR_Y    (UI_H - 6 - BAR_H)
#define CARD_H   ((BAR_Y - 6 - GRID_Y - GRID_GAP) / 2)
#define CARD_PAD 7
#define LINE_Y   32
#define LINE_H   17
#define MIDI     CARDS                      /* the MIDI bar, after the cards */
#define KNOBS    (CARDS + 1)                /* the quick knobs, right of it */
#define KNOBS_W  170

struct card {
    lv_obj_t *box, *key, *count, *line[LINES];
};

static struct {
    struct card cards[CARDS];
    lv_obj_t *midi, *midi_text, *knobs, *knobs_text;
    int sel;
    int mode;                               /* enum control_mode bit shown */
    int opened;                             /* a card's links were opened: coming back keeps the mode */
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

/* does link l belong on card i in the mode shown */
static int on_card(int i, const struct control *l)
{
    if (card_key[i] == CK_EXP1A)
        return l->key == CK_EXP1A || l->key == CK_EXP1B || l->key == CK_TOE;
    return l->key == card_key[i] && (pedal_card(i) || (l->modes & s.mode));
}

/* the table a link of the card runs from: the edited one (global), or on a preset the preset's for the groups it
   has links in and the global one for the others */
static const struct preset_controls *runs_from(struct ui_ctx *ui, const struct control *l)
{
    if (ui->ctl_global)
        return &ui->rig->global;
    int mode = l->key >= CK_TOE ? CM_ALL : s.mode;
    return control_table(ui->rig, l->key, mode, l->when == CW_HOLD);
}

/* the links card i shows, in table order (the preset's, then the global ones); *global[n] = from the global table */
#define CARD_MAX (2 * CONTROL_MAX)
static int card_links(struct ui_ctx *ui, int i, const struct control **out, uint8_t *global)
{
    int n = 0;
    const struct preset_controls *tabs[2] = { ui_ctl_table(ui), &ui->rig->global };
    for (int t = 0; t < (ui->ctl_global ? 1 : 2); t++)
        for (int k = 0; k < tabs[t]->count && n < CARD_MAX; k++) {
            const struct control *l = &tabs[t]->c[k];
            if (on_card(i, l) && l->action != CA_NOTHING && runs_from(ui, l) == tabs[t]) {
                global[n] = (uint8_t)(t == 1);
                out[n++] = l;
            }
        }
    return n;
}

/* ---------------------------------------------------------------- text */

static void brief(struct ui_ctx *ui, const struct control *l, char *out, unsigned size)
{
    const char *pre = l->key == CK_EXP1A ? "A " : l->key == CK_EXP1B ? "B " : l->key == CK_TOE ? "BICO " :
                      l->when == CW_HOLD ? "SEG " : "";
    char name[24] = "?";
    if (l->action == CA_BLOCK) {
        ui_slot_name(ui, l->slot, name, sizeof name);
        lv_snprintf(out, size, "%s%s %s%s", pre, ui_slot_tag(ui, l->slot), name,
                    CK_IS_PEDAL(l->key) && l->arg ? " AUTO" : "");
    } else if (l->action == CA_PARAM) {
        struct rig_param p;
        int knob = control_knob(ui->rig, l);
        for (int k = 0; knob >= 0 && k < rig_param_count(ui->rig, l->slot); k++)
            if (rig_param(ui->rig, l->slot, k, &p) == 0 && p.def.idx == knob) {
                ui_param_label(&p, name, sizeof name);
                break;
            }
        lv_snprintf(out, size, "%s%s %s", pre, ui_slot_tag(ui, l->slot), name);
    } else if (l->action == CA_SCENE) {
        if (l->arg < SCENE_COUNT)
            rig_scene_name(ui->rig, l->arg, name);
        else
            lv_snprintf(name, sizeof name, "%s", l->arg == SCENE_NEXT ? "CENA +" : "CENA \xE2\x88\x92");
        lv_snprintf(out, size, "%s%s", pre, name);
    } else if (l->action == CA_MIDI) {
        lv_snprintf(out, size, "%sMIDI %s %d", pre, l->flags & CF_PC ? "PC" : "CC", l->arg);
    } else {
        lv_snprintf(out, size, "%s%s", pre, assign_fn_name(l->arg));
    }
}

/* the card's colour: the key's own, else its first block's category, else none (0) */
static uint32_t card_colour(struct ui_ctx *ui, int i)
{
    const struct preset_controls *c = ui_ctl_table(ui);
    if (!ui->ctl_global && card_key[i] < CK_KEYS)
        c = control_table(ui->rig, card_key[i], s.mode, 0);
    if (card_key[i] < CK_KEYS && c->colour[card_key[i]])
        return theme_led_colour(c->colour[card_key[i]]);
    for (int k = 0; k < c->count; k++)
        if (on_card(i, &c->c[k]) && (c->c[k].action == CA_BLOCK || c->c[k].action == CA_PARAM) &&
            rig_slot_used(ui->rig, c->c[k].slot) && !rig_is_routing(ui->rig, c->c[k].slot))
            return theme_menu_color(ui->rig->preset.slot[c->c[k].slot].menu);
    return 0;
}

/* ---------------------------------------------------------------- changes */

/* the first link MIDI CC cc runs (the edited table's; on a preset, the global one where the preset has none), or 0 */
static const struct control *midi_first(struct ui_ctx *ui, int cc, int *global)
{
    const struct preset_controls *c = ui->ctl_global ? &ui->rig->global
                                                     : control_table(ui->rig, CK_MIDI + cc, CM_ALL, 0);
    *global = !ui->ctl_global && c == &ui->rig->global;
    for (int k = 0; k < c->count; k++)
        if (c->c[k].key == CK_MIDI + cc && c->c[k].action != CA_NOTHING)
            return &c->c[k];
    return 0;
}

/* the first MIDI key with links, or 0 */
static int first_midi(struct ui_ctx *ui)
{
    int global;
    for (int cc = 1; cc < 128; cc++)
        if (CK_MIDI_CC_OK(cc) && midi_first(ui, cc, &global))
            return CK_MIDI + cc;
    return 0;
}

/* items MENU turns through: the cards, MIDI and (on a preset) KNOBS */
static int items(const struct ui_ctx *ui)
{
    return ui->ctl_global ? CARDS + 1 : CARDS + 2;
}

static void open(struct ui_ctx *ui, int i)
{
    if (i == KNOBS) {
        s.opened = 1;
        ui->assign_row = -1;
        ui_go(SCR_ASSIGN);
        return;
    }
    if (i == MIDI) {
        int key = first_midi(ui);
        s.opened = 1;
        ui_controls(key ? key : CK_MIDI + 1, s.mode, -1);
        return;
    }
    int key = card_key[i];
    if (key == CK_EXP1A && ui->rig->preset.exp_group)
        key = CK_EXP1B;
    s.opened = 1;
    ui_controls(key, s.mode, -1);
}

static void on_tap(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {
        s.sel = g->tag;
        open(ui, g->tag);
    }
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
        gesture_bind(c->box, on_tap, i);
        lv_obj_set_style_radius(c->box, 3, 0);
        lv_obj_set_style_border_width(c->box, 2, 0);
        lv_obj_set_style_outline_color(c->box, rgb(C_ACCENT), 0);
        lv_obj_set_style_outline_pad(c->box, 2, 0);
        c->key = theme_label(c->box, pedal_card(i) ? &font_value_20 : &font_letter_26, C_TEXT, 0);
        lv_obj_set_pos(c->key, CARD_PAD, pedal_card(i) ? CARD_PAD : CARD_PAD - 6);
        lv_label_set_text_static(c->key, card_name[i]);
        c->count = theme_label(c->box, &font_tag_12, C_OFF, 1);
        lv_obj_align(c->count, LV_ALIGN_TOP_RIGHT, -CARD_PAD, CARD_PAD);
        for (int n = 0; n < LINES; n++) {
            c->line[n] = theme_label(c->box, &font_body_14, C_OFF, 0);
            lv_obj_set_width(c->line[n], CARD_W - 2 * CARD_PAD - 4);
            lv_obj_set_height(c->line[n], lv_font_get_line_height(&font_body_14));
            lv_label_set_long_mode(c->line[n], LV_LABEL_LONG_DOT);
            lv_obj_set_pos(c->line[n], CARD_PAD, LINE_Y + n * LINE_H);
        }
    }
    s.midi = theme_box(root, UI_PAD, BAR_Y, UI_W - 2 * UI_PAD, BAR_H, C_BG);
    gesture_bind(s.midi, on_tap, MIDI);
    lv_obj_set_style_radius(s.midi, 3, 0);
    lv_obj_set_style_border_width(s.midi, 2, 0);
    lv_obj_set_style_outline_color(s.midi, rgb(C_ACCENT), 0);
    lv_obj_set_style_outline_pad(s.midi, 2, 0);
    s.midi_text = theme_label(s.midi, &font_body_14, C_OFF, 0);
    lv_label_set_long_mode(s.midi_text, LV_LABEL_LONG_DOT);

    s.knobs = theme_box(root, UI_W - UI_PAD - KNOBS_W, BAR_Y, KNOBS_W, BAR_H, C_BG);
    gesture_bind(s.knobs, on_tap, KNOBS);
    lv_obj_set_style_radius(s.knobs, 3, 0);
    lv_obj_set_style_border_width(s.knobs, 2, 0);
    lv_obj_set_style_outline_color(s.knobs, rgb(C_ACCENT), 0);
    lv_obj_set_style_outline_pad(s.knobs, 2, 0);
    s.knobs_text = theme_label(s.knobs, &font_body_14, C_TEXT, 0);
    lv_obj_set_width(s.knobs_text, KNOBS_W - 2 * CARD_PAD - 4);
    lv_label_set_long_mode(s.knobs_text, LV_LABEL_LONG_DOT);
    lv_obj_align(s.knobs_text, LV_ALIGN_LEFT_MID, CARD_PAD, 0);
}

/* "KNOBS · GAIN · MIX · VOL": what the three quick knobs turn (on a preset; the global table has none) */
static void refresh_knobs(struct ui_ctx *ui)
{
    int global = ui->ctl_global, w = UI_W - 2 * UI_PAD - (global ? 0 : KNOBS_W + GRID_GAP);
    lv_obj_set_width(s.midi, w);
    lv_obj_set_width(s.midi_text, w - 2 * CARD_PAD - 4);
    lv_obj_align(s.midi_text, LV_ALIGN_LEFT_MID, CARD_PAD, 0);
    if (global) {
        lv_obj_add_flag(s.knobs, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s.knobs, LV_OBJ_FLAG_HIDDEN);
    char text[96] = "KNOBS";
    for (int i = 0; i < QUICK_COUNT; i++) {
        const struct quick_knob *q = &ui->assign.quick[i];
        char name[24] = "\xE2\x80\x93";
        struct rig_param p;
        if (q->slot == QUICK_VOLUME)
            lv_snprintf(name, sizeof name, "VOL");
        else if (q->slot == QUICK_BPM)
            lv_snprintf(name, sizeof name, "BPM");
        else if (q->slot >= 0 && rig_param(ui->rig, q->slot, q->k, &p) == 0)
            ui_param_label(&p, name, sizeof name);
        unsigned at = (unsigned)strlen(text);
        lv_snprintf(text + at, sizeof text - at, " \xC2\xB7 %s", name);
    }
    theme_set_text(s.knobs_text, text);
    theme_set_border(s.knobs, C_ACCENT);
    lv_obj_set_style_outline_width(s.knobs, s.sel == KNOBS ? 2 : 0, 0);
}

/* "MIDI · CC 7 VOLUME · CC 80 ..." (each CC once, its first link; G = a global one on a preset) */
static void refresh_midi(struct ui_ctx *ui)
{
    char text[160] = "MIDI";
    int n = 0;
    unsigned at = 4;
    for (int cc = 1; cc < 128; cc++) {
        int global;
        const struct control *l = CK_MIDI_CC_OK(cc) ? midi_first(ui, cc, &global) : 0;
        if (!l)
            continue;
        n++;
        if (at + 8 >= sizeof text)
            continue;
        char what[48];
        brief(ui, l, what, sizeof what);
        lv_snprintf(text + at, sizeof text - at, " \xC2\xB7 CC %d %s%s", cc, what, global ? " (G)" : "");
        at = (unsigned)strlen(text);
    }
    if (!n)
        lv_snprintf(text + at, sizeof text - at, " \xC2\xB7 \xE2\x80\x93");
    theme_set_text(s.midi_text, text);
    theme_set_color(s.midi_text, n ? C_TEXT : C_OFF);
    theme_set_border(s.midi, n ? C_ACCENT : 0x2A2926);
    lv_obj_set_style_outline_width(s.midi, s.sel == MIDI ? 2 : 0, 0);
}

static void enter(struct ui_ctx *ui)
{
    if (!s.opened || !s.mode)
        s.mode = footswitch_mode_bit(ui);
    s.opened = 0;
    if (s.sel >= items(ui))
        s.sel = 0;
}

static void refresh(struct ui_ctx *ui)
{
    static const char *const modes[] = { "", "MODO PRESET", "MODO STOMP", "", "MODO CENAS" };
    char title[48];
    lv_snprintf(title, sizeof title, "%s \xC2\xB7 %s", ui->ctl_global ? "CONTROLES GLOBAIS" : "CONTROLES",
                modes[s.mode]);
    top_bar_sub(title);
    static const struct control *links[CARD_MAX];
    static uint8_t global[CARD_MAX];
    for (int i = 0; i < CARDS; i++) {
        struct card *cd = &s.cards[i];
        int n = card_links(ui, i, links, global), shown = 0;
        for (int k = 0; k < n && shown < LINES; k++) {
            char text[48];
            int more = shown == LINES - 1 && n > LINES;
            if (more)
                lv_snprintf(text, sizeof text, "+%d MAIS", n - shown);
            else
                brief(ui, links[k], text, sizeof text);
            theme_set_text(cd->line[shown], text);
            theme_set_color(cd->line[shown], global[k] && !more ? C_LABEL : C_TEXT);   /* global: grey */
            shown++;
        }
        for (; shown < LINES; shown++)
            theme_set_text(cd->line[shown], shown == 0 && !n ? "\xE2\x80\x93" : "");
        if (!n)
            theme_set_color(cd->line[0], C_OFF);
        char count[8] = "";
        if (n)
            lv_snprintf(count, sizeof count, "%d", n);
        theme_set_text(cd->count, count);
        uint32_t col = card_colour(ui, i);
        theme_set_border(cd->box, col ? col : 0x2A2926);
        theme_set_color(cd->count, col ? col : C_OFF);
        theme_set_color(cd->key, n ? C_TEXT : C_OFF);
        lv_obj_set_style_outline_width(cd->box, i == s.sel ? 2 : 0, 0);
    }
    refresh_midi(ui);
    refresh_knobs(ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        int n = items(ui);
        s.sel = ((s.sel + d) % n + n) % n;
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        open(ui, s.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {                  /* the next mode */
        s.mode = s.mode == CM_PRESET ? CM_STOMP : s.mode == CM_STOMP ? CM_SCENE : CM_PRESET;
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_EXP)) {
        open(ui, 4);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_CTRL)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_control_map = { create, enter, refresh, input, 0, 0 };
