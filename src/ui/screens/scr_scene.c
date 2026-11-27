/*
 * Scenes (tab CENAS, waveod's; footswitch scene mode): the four scenes of the preset on the footswitches A-D,
 * like the stomp tab's cards, with the blocks each one has on (a mark per block of the chain, in its colour
 * when on); the active scene is lit. BANK - and BANK + above C and D step to the previous / next preset; the
 * preset's number, its name and the active scene fill the corner over A and B.
 *
 * This tab is for playing: scenes are edited on the chain (and block) screens, with the scene button of the top
 * bar (C1..C4: choose, copy, rename, revert); there switching blocks and turning parameters change the active
 * scene only (KNOBS NA CENA, GLOBAL › PRESETS).
 *
 * Controls: footswitches (ui/footswitch.c), the rest as on the home screen.
 * Touch: tap a scene = recall it, hold it = edit it (the chain tab in that scene); tap BANK -/+ = previous / next
 * preset.
 */
#include "ui/footswitch.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define GRID_GAP 7
#define GRID_Y   (UI_TOPBAR_H + 8)
#define CARD_W   ((UI_W - 2 * UI_PAD - 3 * GRID_GAP) / 4)
#define CARD_H   ((UI_H - GRID_Y - UI_PAD - GRID_GAP) / 2)
#define CARD_PAD 8
#define MARK_H   8
#define MARK_GAP 2
#define MARK_Y   (CARD_H - CARD_PAD - 2 - MARK_H)
#define MARK_MAX 14                         /* widest mark (few blocks) */
#define C_ACTIVE 0x161514

struct card {
    lv_obj_t *box, *key, *tag, *name, *strip;
    lv_obj_t *mark[PRESET_SLOTS];
};

static struct {
    lv_obj_t *number, *title, *scene;
    struct card cards[6];                   /* scenes 1-4, BANK -, BANK + */
} s SDRAM_BSS;

/* grid cell of card i: the scenes in the bottom row, BANK -/+ in the top row's last two columns */
static void cell(int i, int *x, int *y)
{
    int col = i < 4 ? i : i - 2;
    *x = UI_PAD + col * (CARD_W + GRID_GAP);
    *y = GRID_Y + (i < 4 ? CARD_H + GRID_GAP : 0);
}

static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->tag >= 4) {
        if (g->kind == GESTURE_TAP)
            ui_load_preset(((ui->rig->index + (g->tag == 4 ? -1 : 1)) % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT);
        return;
    }
    if (g->kind == GESTURE_TAP)
        footswitch_scene(ui, g->tag);
    else if (g->kind == GESTURE_LONG) {
        ui->rig->scene_chosen = 1;
        footswitch_scene(ui, g->tag);
        ui_tab(SCR_CHAIN);
    }
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    static const char *const keys[6] = { "A", "B", "C", "D", "\xE2\x88\x92", "+" };
    s.number = theme_label(root, &font_value_26, C_ACCENT, 0);
    lv_obj_set_pos(s.number, UI_PAD, GRID_Y + 2);
    s.title = theme_label(root, &font_value_26, C_TEXT, 0);
    lv_obj_set_width(s.title, 2 * CARD_W + GRID_GAP - 4);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s.title, UI_PAD, GRID_Y + 34);
    s.scene = theme_label(root, &font_tag_12, C_ACCENT, 1);
    lv_obj_set_pos(s.scene, UI_PAD, GRID_Y + CARD_H - 16);
    for (int i = 0; i < 6; i++) {
        struct card *c = &s.cards[i];
        int x, y;
        cell(i, &x, &y);
        c->box = theme_box(root, x, y, CARD_W, CARD_H, C_BG);
        if (i < 4)
            gesture_bind_hold(c->box, on_card, i, 0);
        else
            gesture_bind(c->box, on_card, i);
        lv_obj_set_style_radius(c->box, 3, 0);
        lv_obj_set_style_border_width(c->box, 2, 0);
        c->key = theme_label(c->box, &font_letter_26, C_OFF, 0);
        lv_obj_set_pos(c->key, CARD_PAD, CARD_PAD - 6);
        lv_label_set_text_static(c->key, keys[i]);
        c->tag = theme_label(c->box, &font_tag_12, C_OFF, 1);
        lv_obj_align(c->tag, LV_ALIGN_TOP_RIGHT, -CARD_PAD, CARD_PAD);
        c->name = theme_label(c->box, &font_value_20, C_OFF, 0);
        lv_obj_set_width(c->name, CARD_W - 2 * CARD_PAD - 4);
        lv_label_set_long_mode(c->name, LV_LABEL_LONG_WRAP);
        c->strip = theme_box(c->box, CARD_PAD, MARK_Y, CARD_W - 2 * CARD_PAD - 4, MARK_H, C_BG);
        lv_obj_set_style_bg_opa(c->strip, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(c->strip, LV_OBJ_FLAG_CLICKABLE);
        for (int m = 0; m < PRESET_SLOTS && i < 4; m++) {
            c->mark[m] = theme_box(c->strip, 0, 0, 4, MARK_H, C_LINE);
            lv_obj_set_style_radius(c->mark[m], 1, 0);
        }
    }
}

/* the marks of scene n: a block of the chain each (no Split / Path B / Merge), its colour when on there */
static void marks(struct ui_ctx *ui, struct card *c, int n, int active)
{
    const struct rig *r = ui->rig;
    int slots[PRESET_SLOTS], count = 0;
    for (int i = 0; i < ui->topo.count; i++)
        if (ui->topo.node[i].kind == NODE_EFFECT)
            slots[count++] = ui->topo.node[i].slot;
    int room = CARD_W - 2 * CARD_PAD - 4;
    int w = count ? (room - (count - 1) * MARK_GAP) / count : 0;
    w = w > MARK_MAX ? MARK_MAX : w;
    for (int m = 0; m < PRESET_SLOTS; m++) {
        if (m >= count) {
            lv_obj_add_flag(c->mark[m], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int slot = slots[m];
        int on = r->preset.scenes.on[n] >> slot & 1u;
        lv_obj_clear_flag(c->mark[m], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(c->mark[m], m * (w + MARK_GAP), 0);
        lv_obj_set_width(c->mark[m], w);
        theme_set_bg(c->mark[m], on ? theme_menu_color(r->preset.slot[slot].menu) : active ? C_WIRE : C_LINE);
    }
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    char label[8], text[40], name[SCENE_NAME_LEN + 1];
    top_bar_tabs(SCR_SCENE);
    preset_label(r->index, label);
    theme_set_text(s.number, label);
    theme_set_text(s.title, r->preset.name);
    int act = rig_scene_active(r);
    rig_scene_name(r, act, name);
    lv_snprintf(text, sizeof text, T(MSG_SCENE_ACTIVE), name);
    theme_set_text(s.scene, text);

    for (int i = 0; i < 4; i++) {
        struct card *c = &s.cards[i];
        int on = i == act;
        rig_scene_name(r, i, name);
        theme_set_bg(c->box, on ? C_ACTIVE : C_BG);
        theme_set_border(c->box, on ? C_ACCENT : 0x2A2926);
        theme_set_color(c->key, on ? C_TEXT : C_OFF);
        lv_snprintf(text, sizeof text, "%d", i + 1);
        theme_set_text(c->tag, text);
        theme_set_color(c->tag, on ? C_ACCENT : C_OFF);
        theme_set_text(c->name, name);
        theme_set_color(c->name, on ? C_TEXT : C_LABEL);
        lv_obj_align_to(c->name, c->strip, LV_ALIGN_OUT_TOP_LEFT, 0, -8);
        marks(ui, c, i, on);
    }
    for (int i = 4; i < 6; i++) {               /* the neighbouring presets */
        struct card *c = &s.cards[i];
        int index = ((r->index + (i == 4 ? -1 : 1)) % PRESET_COUNT + PRESET_COUNT) % PRESET_COUNT;
        char pname[PRESET_NAME_LEN + 1];
        presets_name(&r->src, index, pname);
        preset_label(index, label);
        lv_snprintf(text, sizeof text, "%s %s", label, pname[0] ? pname : T(MSG_SCREENS_EMPTY));
        theme_set_bg(c->box, C_BG);
        theme_set_border(c->box, 0x2A2926);
        theme_set_color(c->key, C_LABEL);
        theme_set_text(c->tag, "PRESET");
        theme_set_color(c->tag, C_OFF);
        theme_set_text(c->name, text);
        theme_set_color(c->name, C_LABEL);
        lv_obj_align(c->name, LV_ALIGN_BOTTOM_LEFT, CARD_PAD - 2, -CARD_PAD + 2);
    }
}

/* knobs, MENU and EFX behave as on the preset tab; EXIT stays (this is home in scene mode) */
static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    if (ui_pressed(ev, BTN_CTRL)) {                 /* the footswitches and pedals */
        ui_go(SCR_CONTROL_MAP);
        return 1;
    }
    return screen_preset.input(ui, ev);
}

const struct screen screen_scene = { create, 0, refresh, input };
