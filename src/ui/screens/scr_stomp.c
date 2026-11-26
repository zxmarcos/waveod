/*
 * 02 Stomp (tab STOMP): one card per footswitch with what it does in stomp mode, from the preset (usually
 * CTRL 1-4, each switching its member blocks; the card shows the first one and how many more). The cards sit
 * like the footswitches on the pedal: A-D along the bottom, BANK - and BANK + above C and D; the preset's
 * number and name fill the corner over A and B.
 *
 * Controls: footswitches (ui/footswitch.c), EFX = the next mode (scenes), MENU press = chain tab, CTRL = the
 * footswitches and pedals (scr_control_map.c), knobs 1-3 = quick knobs (same as the home screen).
 * Touch: a card is its footswitch: tap = step on it, hold = its detail (a bar fills along its bottom): the block's
 * edit screen, or the key's links (CONTROLES) when it has no block.
 */
#include "ui/footswitch.h"
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
#define STATE_W  26                         /* "OFF" right of the bar */
#define BAR_Y    (CARD_H - CARD_PAD - 2 - 10)

struct card {
    lv_obj_t *box, *key, *tag, *name, *bar, *state;
};

static struct {
    lv_obj_t *number, *title;
    struct card cards[STOMP_COUNT];
} s SDRAM_BSS;

/* grid cell of footswitch i: A-D in the bottom row, BANK -/+ in the top row's last two columns */
static void cell(int i, int *x, int *y)
{
    int col = i < 4 ? i : i - 2;
    *x = UI_PAD + col * (CARD_W + GRID_GAP);
    *y = GRID_Y + (i < 4 ? CARD_H + GRID_GAP : 0);
}

/* a card is its footswitch: tap = step on it, hold = its detail (the block's edit screen, else the key's links) */
static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    int slot = ui->assign.stomp[g->tag].slot;
    if (g->kind == GESTURE_TAP) {
        footswitch_press(ui, g->tag);
        ui_changed();
    } else if (g->kind == GESTURE_LONG) {
        if (slot >= 0 && rig_slot_used(ui->rig, slot))
            for (int pos = 0; pos < ui->rig->preset.count; pos++)
                if (ui->rig->preset.order[pos] == slot) {
                    ui_edit(pos);
                    return;
                }
        ui_controls(g->tag, CM_STOMP, -1);
    }
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    static const char *const keys[STOMP_COUNT] = { "A", "B", "C", "D", "\xE2\x88\x92", "+" };
    s.number = theme_label(root, &font_value_26, C_ACCENT, 0);
    lv_obj_set_pos(s.number, UI_PAD, GRID_Y + 2);
    s.title = theme_label(root, &font_value_26, C_TEXT, 0);
    lv_obj_set_width(s.title, 2 * CARD_W + GRID_GAP - 4);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s.title, UI_PAD, GRID_Y + 34);
    for (int i = 0; i < STOMP_COUNT; i++) {
        struct card *c = &s.cards[i];
        int x, y;
        cell(i, &x, &y);
        c->box = theme_box(root, x, y, CARD_W, CARD_H, C_BG);
        gesture_bind_hold(c->box, on_card, i, 0);
        lv_obj_set_style_radius(c->box, 3, 0);
        lv_obj_set_style_border_width(c->box, 2, 0);
        c->key = theme_label(c->box, &font_letter_26, C_OFF, 0);
        lv_obj_set_pos(c->key, CARD_PAD, CARD_PAD - 6);
        lv_label_set_text_static(c->key, keys[i]);
        c->tag = theme_label(c->box, &font_tag_12, C_OFF, 1);
        lv_obj_align(c->tag, LV_ALIGN_TOP_RIGHT, -CARD_PAD, CARD_PAD);
        c->bar = theme_box(c->box, CARD_PAD, BAR_Y, CARD_W - 2 * CARD_PAD - 4 - STATE_W, 6, C_LINE);
        c->state = theme_label(c->box, &font_tag_12, C_OFF, 1);
        lv_obj_align(c->state, LV_ALIGN_TOP_RIGHT, -CARD_PAD, BAR_Y - 5);
        c->name = theme_label(c->box, &font_value_20, C_OFF, 0);
        lv_obj_set_width(c->name, CARD_W - 2 * CARD_PAD - 4);
        lv_label_set_long_mode(c->name, LV_LABEL_LONG_WRAP);
        lv_obj_align_to(c->name, c->bar, LV_ALIGN_OUT_TOP_LEFT, 0, -6);
    }
}

static void refresh(struct ui_ctx *ui)
{
    const struct rig *r = ui->rig;
    char label[8];
    top_bar_tabs(SCR_STOMP);
    preset_label(r->index, label);
    theme_set_text(s.number, label);
    theme_set_text(s.title, r->preset.name);

    for (int i = 0; i < STOMP_COUNT; i++) {
        struct card *c = &s.cards[i];
        const struct stomp_key *k = &ui->assign.stomp[i];
        int slot = k->slot;
        int used = slot >= 0 && rig_slot_used(r, slot);
        int stateful = k->state >= 0, on = k->state == 1;
        int menu = used ? r->preset.slot[slot].menu : -1;
        const struct preset_controls *tab = control_table(r, i, CM_STOMP, 0);
        int own = tab->colour[i];
        uint32_t col = own ? theme_led_colour(own) : used ? theme_menu_color(menu) : C_ACCENT;
        const char *label = tab->label[i];
        char name[32], tag[16] = "";
        if (label[0]) {
            lv_snprintf(name, sizeof name, "%s", label);
        } else if (used && k->param >= 0) {               /* a parameter: its name */
            struct rig_param p;
            if (rig_param(r, slot, k->param, &p) == 0)
                ui_param_label(&p, name, sizeof name);
            else
                name[0] = 0;
        } else if (used && k->members > 1) {
            char first[24];
            ui_slot_name(ui, slot, first, sizeof first);
            lv_snprintf(name, sizeof name, "%s +%d", first, k->members - 1);
        } else if (used) {
            ui_slot_name(ui, slot, name, sizeof name);
        } else if (k->scene >= 0 && k->scene < SCENE_COUNT) {
            char sname[SCENE_NAME_LEN + 1];
            rig_scene_name(r, k->scene, sname);
            lv_snprintf(name, sizeof name, "%s", sname);
        } else if (k->scene != -1) {
            lv_snprintf(name, sizeof name, "%s", (uint8_t)k->scene == SCENE_NEXT ? "CENA +" : "CENA \xE2\x88\x92");
        } else if (k->midi >= 0 && k->fn == FSF_NONE) {
            lv_snprintf(name, sizeof name, "%s %d", k->midi & 0x100 ? "PC" : "CC", k->midi & 0x7f);
        } else {
            lv_snprintf(name, sizeof name, "%s", assign_fn_name(k->fn));
        }
        if (used)
            lv_snprintf(tag, sizeof tag, "%s", ui_slot_tag(ui, slot));
        else if (k->scene != -1)
            lv_snprintf(tag, sizeof tag, "CENA");
        else if (k->midi >= 0 && k->fn == FSF_NONE)
            lv_snprintf(tag, sizeof tag, "MIDI");
        int lit = stateful ? on : k->fn != FSF_NONE || k->scene != -1 || k->midi >= 0;
        theme_set_bg(c->box, stateful && on ? (used && !own ? theme_menu_dim(menu) : 0x161514) : C_BG);
        theme_set_border(c->box, stateful && on ? col : 0x2A2926);
        theme_set_color(c->key, stateful && on ? C_TEXT : C_OFF);
        theme_set_text(c->tag, tag);
        theme_set_color(c->tag, stateful && on ? col : C_OFF);
        /* a parameter's name is one word: one line with "..." rather than broken in the middle */
        int one_line = used && k->param >= 0 && !label[0];
        lv_label_set_long_mode(c->name, one_line ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
        lv_obj_set_height(c->name, one_line ? lv_font_get_line_height(&font_value_20) : LV_SIZE_CONTENT);
        theme_set_text(c->name, name);
        theme_set_color(c->name, lit ? C_TEXT : C_OFF);
        lv_obj_align_to(c->name, c->bar, LV_ALIGN_OUT_TOP_LEFT, 0, -6);
        theme_set_bg(c->bar, stateful && on ? col : C_LINE);
        theme_set_text(c->state, stateful ? (on ? "ON" : "OFF") : "");
        theme_set_color(c->state, on ? col : C_OFF);
    }
}
/* knobs, MENU and EFX behave as on the preset tab; EXIT stays (this is home in stomp mode) */
static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    if (ui_pressed(ev, BTN_CTRL)) {
        ui_go(SCR_CONTROL_MAP);
        return 1;
    }
    return screen_preset.input(ui, ev);
}

const struct screen screen_stomp = { create, 0, refresh, input };
