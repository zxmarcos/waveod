/*
 * MENU (≡ in the top bar): the meters with their names and the DSP load in %, and the places that are not the
 * footswitch modes as eight cards: CADEIA, AFINADOR, PRESETS, CONTROLES, LOOPER, BATERIA, EQ GLOBAL, GLOBAL.
 * A place chosen replaces the menu (‹ / EXIT there come back to where ≡ was tapped).
 *
 * Controls: MENU turns = a card, MENU press = open it, EXIT = back. Touch: tap a card = open it.
 */
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define METER_Y   (UI_TOPBAR_H + 10)
#define METER_H   10
#define METER_W   150
#define GRID_Y    (UI_TOPBAR_H + 38)
#define GRID_GAP  8
#define CARDS     8
#define CARD_W    ((UI_W - 2 * UI_PAD - 3 * GRID_GAP) / 4)
#define CARD_H    ((UI_H - GRID_Y - 6 - GRID_GAP) / 2)
#define FALL      0.85f
#define C_FRAME   0x2A2926

static const struct {
    const char *name;
    const lv_image_dsc_t *icon;
    enum screen_id id;
    int tab;                                /* a main screen: shown as a tab (no way back) */
} items[CARDS] = {
    { "CADEIA", &icon_m_chain, SCR_CHAIN, 1 },       { "AFINADOR", &icon_g_tuner, SCR_TUNER, 1 },
    { "PRESETS", &icon_g_presets, SCR_PRESETS, 0 },  { "CONTROLES", &icon_g_controls, SCR_CONTROL_MAP, 0 },
    { "LOOPER", &icon_m_looper, SCR_LOOPER, 0 },     { "BATERIA", &icon_m_drum, SCR_DRUM, 0 },
    { "EQ GLOBAL", &icon_g_eq, SCR_EQ, 0 },          { "GLOBAL", &icon_m_global, SCR_SETTINGS, 0 },
};

static struct {
    lv_obj_t *card[CARDS], *icon[CARDS], *name[CARDS];
    lv_obj_t *fill[2], *dsp;
    float level[2];
    int sel;
    uint32_t meter_at;
} s SDRAM_BSS;

static void open(struct ui_ctx *ui, int i)
{
    s.sel = i;
    ui_back();                              /* the menu goes; back from the place returns where ≡ was tapped */
    if (items[i].tab)
        ui_tab(items[i].id);
    else if (ui->current != items[i].id)
        ui_go(items[i].id);
}

static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        open(ui, g->tag);
}

static void meter_create(lv_obj_t *root, int m, int x, const char *name)
{
    lv_obj_t *l = theme_label(root, &font_tag_12, C_LABEL, 1);
    lv_label_set_text_static(l, name);
    lv_obj_set_pos(l, x, METER_Y - 3);
    lv_obj_t *track = theme_box(root, x + 34, METER_Y, METER_W, METER_H, C_LINE);
    lv_obj_set_style_radius(track, 2, 0);
    s.fill[m] = theme_box(track, 0, 0, 0, METER_H, C_TUNED);
    lv_obj_set_style_radius(s.fill[m], 2, 0);
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    meter_create(root, 0, UI_PAD, "IN");
    meter_create(root, 1, UI_PAD + 34 + METER_W + 16, "OUT");
    s.dsp = theme_label(root, &font_tag_12, C_LABEL, 1);
    lv_obj_align(s.dsp, LV_ALIGN_TOP_RIGHT, -UI_PAD, METER_Y - 3);
    for (int i = 0; i < CARDS; i++) {
        int x = UI_PAD + i % 4 * (CARD_W + GRID_GAP), y = GRID_Y + i / 4 * (CARD_H + GRID_GAP);
        lv_obj_t *c = theme_box(root, x, y, CARD_W, CARD_H, C_BG);
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_outline_color(c, rgb(C_ACCENT), 0);
        lv_obj_set_style_outline_pad(c, 2, 0);
        gesture_bind(c, on_card, i);
        s.card[i] = c;
        s.icon[i] = theme_icon(c, items[i].icon, C_LABEL);
        lv_obj_align(s.icon[i], LV_ALIGN_TOP_MID, 0, CARD_H / 2 - 26);
        s.name[i] = theme_label(c, &font_tag_12, C_TEXT, 1);
        lv_label_set_text_static(s.name[i], items[i].name);
        lv_obj_align(s.name[i], LV_ALIGN_TOP_MID, 0, CARD_H / 2 + 6);
    }
}

static void enter(struct ui_ctx *ui)
{
    (void)ui;
    s.level[0] = s.level[1] = 0;
}

static void refresh(struct ui_ctx *ui)
{
    (void)ui;
    top_bar_sub("MENU");
    for (int i = 0; i < CARDS; i++) {
        int on = i == s.sel;
        theme_set_border(s.card[i], on ? C_ACCENT : C_FRAME);
        lv_obj_set_style_outline_width(s.card[i], on ? 2 : 0, 0);
        theme_set_icon(s.icon[i], items[i].icon, on ? C_ACCENT : C_LABEL);
    }
}

/* the meters, every 50 ms like the top bar's */
static void tick(struct ui_ctx *ui)
{
    uint32_t now = lv_tick_get();
    if (now - s.meter_at < 50)
        return;
    s.meter_at = now;
    float v[2] = { top_bar_meter_pos(ui->stats.in_peak), top_bar_meter_pos(ui->stats.out_peak) };
    for (int m = 0; m < 2; m++) {
        s.level[m] = v[m] > s.level[m] * FALL ? v[m] : s.level[m] * FALL;
        int w = (int)(s.level[m] * METER_W + 0.5f);
        if (lv_obj_get_width(s.fill[m]) != w)
            lv_obj_set_width(s.fill[m], w);
        theme_set_bg(s.fill[m], s.level[m] >= 21.0f / 24 ? C_CLIP : s.level[m] >= 16.0f / 24 ? C_ACCENT : C_TUNED);
    }
    int p = top_bar_dsp_percent(ui->stats.worst_ms);
    char text[16];
    lv_snprintf(text, sizeof text, "DSP %d%%", p);
    theme_set_text(s.dsp, text);
    theme_set_color(s.dsp, p >= 90 ? C_CLIP : p >= 75 ? C_ACCENT : C_LABEL);
    lv_obj_align(s.dsp, LV_ALIGN_TOP_RIGHT, -UI_PAD, METER_Y - 3);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        s.sel = ((s.sel + d) % CARDS + CARDS) % CARDS;
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        open(ui, s.sel);
        return 1;
    }
    return 0;
}

const struct screen screen_menu = { create, enter, refresh, input, 0, tick };
