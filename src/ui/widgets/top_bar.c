#include "ui/widgets/top_bar.h"
#include "dsp/pitch.h"
#include "platform/audio.h"
#include "ui/footswitch.h"
#include "ui/i18n.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "platform/sdram.h"

/*
 * One row of buttons a finger's height (36 px drawn, each taking taps over the bar's whole height) and under it a
 * 4 px strip with the meters: IN on the left, OUT in the middle, the DSP load on the right (the MENU screen shows
 * them with names and numbers). Main screens: ≡ (the MENU screen), the footswitch mode (a chooser of the three),
 * the preset with BPM / VOL inside it, SAVE; the other screens: ‹ and the title.
 */
#define ROW_H      40               /* the buttons' row; the meters below it */
#define BTN_H      36
#define BTN_Y      ((ROW_H - BTN_H) / 2)
#define METER_Y    ROW_H
#define METER_H    (UI_TOPBAR_H - ROW_H)
#define TEMPO_H    (BTN_H - 8)      /* BPM / VOL, inside the preset */
#define PAD        4
#define GAP        4
#define ICON_W     46               /* ≡, the mode, SAVE */
#define BACK_W     56
#define SEGS       24
#define SEG_W      7
#define IN_X       4
#define OUT_X      (IN_X + SEGS * (SEG_W + 1) + 6)
#define DSP_X      (OUT_X + SEGS * (SEG_W + 1) + 6)
#define DSP_W      (UI_W - 4 - DSP_X)
#define FLOOR_DB   48.0f            /* the meter shows -48..0 dBFS, 2 dB per segment */
#define FALL       0.85f            /* level kept per update (the design's ballistics) */
#define PEAK_FALL  0.012f           /* peak marker drop per update */
#define DSP_FALL   0.9f
#define C_FRAME    0x2A2926
#define C_ACTIVE   0x161514
#define POP_W      132              /* the mode chooser's buttons */
#define POP_H      104
#define POP_GAP    10
#define PRESET_X   (PAD + 2 * (ICON_W + GAP))
#define PRESET_W   (UI_W - PAD - ICON_W - GAP - PRESET_X)      /* without the scene button */
#define SCENE_X    (UI_W - PAD - 2 * ICON_W - GAP)              /* the scene button, before SAVE */
#define SP_X       20               /* the scene menu: four scenes, three actions */
#define SP_GAP     8
#define SP_W       ((UI_W - 2 * SP_X - 3 * SP_GAP) / 4)
#define SP_Y       60
#define SP_H       86
#define SP_ACT_Y   (SP_Y + SP_H + 12)
#define SP_ACT_W   ((UI_W - 2 * SP_X - 2 * SP_GAP) / 3)
#define SP_ACT_H   44
#define SP_ITEMS   (SCENE_COUNT + 3)
#define SP_OUTSIDE 9
#define MARK_H     6
#define MARK_GAP   2
#define MARK_MAX   12

enum { ACT_COPY = SCENE_COUNT, ACT_NAME, ACT_REVERT };

static const uint16_t mode_names[3] = { MSG_TOP_MODE_PRESET, MSG_TOP_MODE_STOMP, MSG_TOP_SCENES };
static const uint16_t mode_hints[3] = { MSG_TOP_HINT_PRESET, MSG_TOP_HINT_STOMP, MSG_TOP_HINT_SCENES };

static struct {
    lv_obj_t *box;
    lv_obj_t *menu_hit, *mode_hit, *mode, *mode_icon;
    lv_obj_t *preset_hit, *preset, *number, *name;
    lv_obj_t *tempo_hit, *tempo, *bpm, *vol;
    int tempo_lit;
    lv_obj_t *scene_hit, *scene, *scene_text;
    lv_obj_t *save_hit, *save, *save_icon;
    lv_obj_t *back_hit, *title;
    lv_obj_t *view_hit, *view, *view_list, *view_cards;   /* a screen's two views (CONTROLES: cards / list) */
    void (*view_fn)(void);
    lv_obj_t *seg[2][SEGS];
    lv_obj_t *dsp_fill;
    float level[2], peak[2], dsp;
    lv_obj_t *pop, *pop_title, *pop_btn[3], *pop_icon[3], *pop_name[3], *pop_hint[3];
    int pop_sel;
    lv_obj_t *sp, *sp_title, *sp_hint, *sp_item[SP_ITEMS], *sp_num[SCENE_COUNT], *sp_name[SP_ITEMS];
    lv_obj_t *sp_mark[SCENE_COUNT][PRESET_SLOTS];
    int sp_sel, sp_copy;
} s SDRAM_BSS;

static const lv_image_dsc_t *mode_icon(int mode, int big)
{
    static const lv_image_dsc_t *const small[3] = { &icon_mode_preset, &icon_mode_stomp, &icon_mode_scene };
    static const lv_image_dsc_t *const large[3] = { &icon_mode_preset_28, &icon_mode_stomp_28,
                                                    &icon_mode_scene_28 };
    return big ? large[mode] : small[mode];
}

static void show(lv_obj_t *o, int on)
{
    if (on)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------- the mode chooser */

static void pop_refresh(void)
{
    int cur = footswitch_mode(ui_context());
    for (int m = 0; m < 3; m++) {
        int on = m == cur, sel = m == s.pop_sel;
        theme_set_border(s.pop_btn[m], sel ? C_TEXT : on ? C_ACCENT : C_FRAME);
        lv_obj_set_style_border_width(s.pop_btn[m], sel ? 2 : 1, 0);
        theme_set_bg(s.pop_btn[m], on ? C_ACTIVE : C_BG);
        theme_set_icon(s.pop_icon[m], mode_icon(m, 1), on ? C_ACCENT : C_LABEL);
        theme_set_color(s.pop_name[m], on ? C_ACCENT : C_TEXT);
    }
}

/* the texts: the language may have changed since the chooser was made (ui.c builds the screens again, not the bar) */
static void pop_texts(void)
{
    lv_label_set_text_static(s.pop_title, T(MSG_TOP_FS_MODE));
    for (int m = 0; m < 3; m++) {
        lv_label_set_text_static(s.pop_name[m], T(mode_names[m]));
        lv_label_set_text_static(s.pop_hint[m], T(mode_hints[m]));
    }
}

static void pop_open(void)
{
    pop_texts();
    s.pop_sel = footswitch_mode(ui_context());
    pop_refresh();
    show(s.pop, 1);
    lv_obj_move_foreground(s.pop);
}

void top_bar_close(void)
{
    if (s.pop)
        show(s.pop, 0);
    if (s.sp)
        show(s.sp, 0);
    s.sp_copy = 0;
}

static int pop_shown(void)
{
    return s.pop && !lv_obj_has_flag(s.pop, LV_OBJ_FLAG_HIDDEN);
}

static int sp_shown(void)
{
    return s.sp && !lv_obj_has_flag(s.sp, LV_OBJ_FLAG_HIDDEN);
}

/* a mode chosen: its tab (choosing the tab chooses the mode) */
static void pop_pick(int m)
{
    static const enum screen_id tab[3] = { SCR_PRESET, SCR_STOMP, SCR_SCENE };
    top_bar_close();
    ui_tab(tab[m]);
}

static void on_pop(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind != GESTURE_TAP)
        return;
    if (g->tag < 3)
        pop_pick(g->tag);
    else
        top_bar_close();                            /* outside the buttons */
}

/* ---------------------------------------------------------------- the scene menu */

/* a scene's marks: a block of the chain each (no Split / Path B / Merge), its colour when on there */
static void sp_marks(const struct ui_ctx *ui, int n)
{
    const struct rig *r = ui->rig;
    int slots[PRESET_SLOTS], count = 0;
    for (int i = 0; i < ui->topo.count; i++)
        if (ui->topo.node[i].kind == NODE_EFFECT)
            slots[count++] = ui->topo.node[i].slot;
    int room = SP_W - 20;
    int w = count ? (room - (count - 1) * MARK_GAP) / count : 0;
    w = w > MARK_MAX ? MARK_MAX : w;
    for (int m = 0; m < PRESET_SLOTS; m++) {
        lv_obj_t *o = s.sp_mark[n][m];
        show(o, m < count);
        if (m >= count)
            continue;
        int on = r->preset.scenes.on[n] >> slots[m] & 1u;
        lv_obj_set_pos(o, 10 + m * (w + MARK_GAP), SP_H - 12 - MARK_H);
        lv_obj_set_width(o, w);
        theme_set_bg(o, on ? theme_menu_color(r->preset.slot[slots[m]].menu) : C_LINE);
    }
}

static void sp_refresh(void)
{
    const struct ui_ctx *ui = ui_context();
    const struct rig *r = ui->rig;
    int act = rig_scene_active(r), playing = rig_scene_editing(r);
    char text[72], name[SCENE_NAME_LEN + 1];
    if (s.sp_copy)
        lv_snprintf(text, sizeof text, T(MSG_TOP_COPY_TITLE), act + 1);
    else if (playing)
        lv_snprintf(text, sizeof text, T(MSG_TOP_EDITING_SCENE), act + 1);
    else
        lv_snprintf(text, sizeof text, "%s", T(MSG_TOP_SCENES));
    theme_set_text(s.sp_title, text);
    theme_set_color(s.sp_title, s.sp_copy || playing ? C_ACCENT : C_LABEL);
    const char *hint = T(s.sp_copy ? MSG_TOP_HINT_COPY
                         : !playing ? MSG_TOP_HINT_CHOOSE
                         : r->scene_auto ? MSG_TOP_HINT_AUTO
                                         : MSG_TOP_HINT_MANUAL);
    theme_set_text(s.sp_hint, hint);
    for (int i = 0; i < SP_ITEMS; i++) {
        int scene = i < SCENE_COUNT, on = scene && i == act && playing, sel = i == s.sp_sel;
        int off = s.sp_copy ? (scene ? i == act : i != ACT_COPY) : 0;     /* what copying cannot pick */
        uint32_t border = sel ? C_TEXT : on ? C_ACCENT : C_FRAME;
        theme_set_border(s.sp_item[i], border);
        lv_obj_set_style_border_width(s.sp_item[i], sel || on ? 2 : 1, 0);
        theme_set_bg(s.sp_item[i], on ? C_ACTIVE : C_BG);
        if (scene) {
            rig_scene_name(r, i, name);
            theme_set_text(s.sp_name[i], name);
            theme_set_color(s.sp_name[i], off ? C_OFF : on ? C_TEXT : C_LABEL);
            theme_set_color(s.sp_num[i], off ? C_OFF : on ? C_ACCENT : C_TEXT);
            sp_marks(ui, i);
        } else {
            static const uint16_t names[3] = { MSG_TOP_COPY_TO, MSG_TOP_RENAME, MSG_TOP_REVERT };
            theme_set_text(s.sp_name[i], T(i == ACT_COPY && s.sp_copy ? MSG_TOP_CANCEL : names[i - ACT_COPY]));
            theme_set_color(s.sp_name[i], off ? C_OFF : C_TEXT);
            lv_obj_center(s.sp_name[i]);
        }
    }
}

static void sp_open(void)
{
    s.sp_copy = 0;
    s.sp_sel = rig_scene_active(ui_context()->rig);
    sp_refresh();
    show(s.sp, 1);
    lv_obj_move_foreground(s.sp);
}

static void sp_pick(int i)
{
    struct ui_ctx *ui = ui_context();
    struct rig *r = ui->rig;
    int act = rig_scene_active(r);
    char text[48];
    if (s.sp_copy) {
        if (i < SCENE_COUNT && i != act) {
            top_bar_close();
            rig_scene_copy(r, act, i);
            lv_snprintf(text, sizeof text, T(MSG_TOP_COPIED), act + 1, i + 1);
            ui_changed();
            ui_toast(text);
        } else if (i == ACT_COPY || i == act) {
            s.sp_copy = 0;                          /* cancelled */
            sp_refresh();
        }
        return;
    }
    switch (i) {
    case ACT_COPY:
        s.sp_copy = 1;
        s.sp_sel = (act + 1) % SCENE_COUNT;
        sp_refresh();
        break;
    case ACT_NAME:
        top_bar_close();
        ui_name_scene(act);
        break;
    case ACT_REVERT:
        top_bar_close();
        if (rig_scene_revert(r, act) == 0)
            lv_snprintf(text, sizeof text, T(MSG_TOP_REVERTED), act + 1);
        else
            lv_snprintf(text, sizeof text, "%s", T(MSG_TOP_NOT_SAVED));
        ui_changed();
        ui_toast(text);
        break;
    default:                                        /* a scene: it plays, and edits go to it */
        top_bar_close();
        r->scene_chosen = 1;
        footswitch_scene(ui, i);
        break;
    }
}

static void on_sp(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind != GESTURE_TAP)
        return;
    if (g->tag < SP_ITEMS)
        sp_pick(g->tag);
    else if (s.sp_copy) {
        s.sp_copy = 0;
        sp_refresh();
    } else {
        top_bar_close();                            /* outside the buttons */
    }
}

static void sp_create(void)
{
    s.sp = theme_box(lv_layer_top(), 0, 0, UI_W, UI_H, 0x000000);
    lv_obj_set_style_bg_opa(s.sp, LV_OPA_80, 0);
    gesture_bind(s.sp, on_sp, SP_OUTSIDE);
    s.sp_title = theme_label(s.sp, &font_tag_12, C_LABEL, 2);
    lv_obj_set_pos(s.sp_title, SP_X, SP_Y - 24);
    for (int n = 0; n < SCENE_COUNT; n++) {
        lv_obj_t *b = theme_box(s.sp, SP_X + n * (SP_W + SP_GAP), SP_Y, SP_W, SP_H, C_BG);
        lv_obj_set_style_radius(b, 6, 0);
        gesture_bind(b, on_sp, n);
        s.sp_item[n] = b;
        s.sp_num[n] = theme_label(b, &font_letter_26, C_TEXT, 0);
        lv_obj_set_pos(s.sp_num[n], 10, 4);
        char num[2] = { (char)('1' + n), 0 };
        lv_label_set_text(s.sp_num[n], num);
        s.sp_name[n] = theme_label(b, &font_body_14, C_LABEL, 0);
        lv_obj_set_width(s.sp_name[n], SP_W - 20);
        lv_label_set_long_mode(s.sp_name[n], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(s.sp_name[n], 10, 40);
        for (int m = 0; m < PRESET_SLOTS; m++) {
            s.sp_mark[n][m] = theme_box(b, 0, 0, 4, MARK_H, C_LINE);
            lv_obj_set_style_radius(s.sp_mark[n][m], 1, 0);
        }
    }
    for (int a = 0; a < 3; a++) {
        lv_obj_t *b = theme_box(s.sp, SP_X + a * (SP_ACT_W + SP_GAP), SP_ACT_Y, SP_ACT_W, SP_ACT_H, C_BG);
        lv_obj_set_style_radius(b, 6, 0);
        gesture_bind(b, on_sp, ACT_COPY + a);
        s.sp_item[ACT_COPY + a] = b;
        s.sp_name[ACT_COPY + a] = theme_label(b, &font_tag_12, C_TEXT, 1);
    }
    s.sp_hint = theme_label(s.sp, &font_tag_12, C_OFF, 1);
    lv_obj_set_pos(s.sp_hint, SP_X, SP_ACT_Y + SP_ACT_H + 16);
    show(s.sp, 0);
}

static int sp_input(const struct input_event *ev)
{
    if (ev->type == IN_TURN && ev->id == ENC_MENU) {
        s.sp_sel = ((s.sp_sel + ev->delta) % SP_ITEMS + SP_ITEMS) % SP_ITEMS;
        sp_refresh();
        return 1;
    }
    if (ev->type != IN_PRESS)
        return 0;
    uint32_t b = 1u << ev->id;
    if (b == BTN_MENU) {
        sp_pick(s.sp_sel);
        return 1;
    }
    if (b == BTN_EXIT) {
        if (s.sp_copy) {
            s.sp_copy = 0;
            sp_refresh();
        } else {
            top_bar_close();
        }
        return 1;
    }
    return 0;                                       /* the footswitches (scenes in scene mode) act as always */
}

int top_bar_input(const struct input_event *ev)
{
    if (sp_shown())
        return sp_input(ev);
    if (!pop_shown())
        return 0;
    if (ev->type == IN_TURN && ev->id == ENC_MENU) {
        s.pop_sel = ((s.pop_sel + ev->delta) % 3 + 3) % 3;
        pop_refresh();
        return 1;
    }
    if (ev->type != IN_PRESS)
        return 0;
    uint32_t b = 1u << ev->id;
    if (b == BTN_MENU) {
        pop_pick(s.pop_sel);
        return 1;
    }
    if (b == BTN_EXIT) {
        top_bar_close();
        return 1;
    }
    return 0;                                       /* the footswitches and the rest act as always */
}

/* ---------------------------------------------------------------- touch */

static void on_menu(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && ui->current != SCR_MENU)
        ui_go(SCR_MENU);
}

static void on_mode(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        pop_open();
}

static void on_back(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        ui_press(BTN_EXIT);
}

static void on_save(struct ui_ctx *ui, const struct gesture *g)
{
    (void)ui;
    if (g->kind == GESTURE_TAP)
        ui_open_save();                     /* from the tuner too, which keeps the SAVE button for itself */
}

/* BPM / VOL: tempo and volume on the chain screen (from the other tabs too); again = back to the block */
static void on_tempo(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    if (ui->current != SCR_CHAIN) {
        ui->tempo_request = 2;                      /* open */
        ui_tab(SCR_CHAIN);
    } else {
        ui->tempo_request = 1;                      /* toggle */
    }
    ui->rev++;
}

/* swipe the preset sideways = next / previous preset, tap = the presets list */
static void on_preset(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {
        ui_go(SCR_PRESETS);
        return;
    }
    int d = gesture_swipe_x(g);
    if (d)
        ui_load_preset(ui->rig->index - d);         /* finger to the left = next */
}

/* the scene: tap = the scene menu, swipe sideways = next / previous scene (edits go to it) */
static void on_scene(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {
        sp_open();
        return;
    }
    int d = gesture_swipe_x(g);
    if (d) {
        ui->rig->scene_chosen = 1;
        footswitch_scene(ui, ((rig_scene_active(ui->rig) - d) % SCENE_COUNT + SCENE_COUNT) % SCENE_COUNT);
    }
}

static void on_view(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && s.view_fn) {
        s.view_fn();
        ui->rev++;
    }
}

/* ---------------------------------------------------------------- creating */

/* the view button's two drawings, 18 x 14: a list (a mark and a line, three times) and cards (two rows of three) */
static lv_obj_t *view_icon(lv_obj_t *parent, int list)
{
    lv_obj_t *o = theme_box(parent, 0, 0, 18, 14, C_BG);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_center(o);
    for (int r = 0; r < (list ? 3 : 2); r++) {
        if (list) {
            theme_box(o, 0, r * 6, 3, 2, C_TEXT);
            theme_box(o, 5, r * 6, 13, 2, C_TEXT);
        } else {
            for (int c = 0; c < 3; c++)
                theme_box(o, c * 7, r * 8, 5, 6, C_TEXT);
        }
    }
    return o;
}

/* a drawn button of w x BTN_H at x, inside a hit box over the whole height of the bar */
static lv_obj_t *button(int x, int w, lv_obj_t **hit, gesture_handler fn, int tag)
{
    *hit = theme_box(s.box, x - GAP / 2, 0, w + GAP, UI_TOPBAR_H, C_BG);
    lv_obj_set_style_bg_opa(*hit, LV_OPA_TRANSP, 0);
    gesture_bind(*hit, fn, tag);
    lv_obj_t *b = theme_box(*hit, GAP / 2, BTN_Y, w, BTN_H, C_BG);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, rgb(C_FRAME), 0);
    return b;
}

static void meter_create(int m, int x)
{
    for (int i = 0; i < SEGS; i++)
        s.seg[m][i] = theme_box(s.box, x + i * (SEG_W + 1), METER_Y, SEG_W, METER_H, C_LINE);
}

static void pop_create(void)
{
    s.pop = theme_box(lv_layer_top(), 0, 0, UI_W, UI_H, 0x000000);
    lv_obj_set_style_bg_opa(s.pop, LV_OPA_80, 0);
    gesture_bind(s.pop, on_pop, 3);
    s.pop_title = theme_label(s.pop, &font_tag_12, C_LABEL, 2);
    lv_obj_align(s.pop_title, LV_ALIGN_TOP_MID, 0, (UI_H - POP_H) / 2 - 26);
    int x = (UI_W - 3 * POP_W - 2 * POP_GAP) / 2;
    for (int m = 0; m < 3; m++) {
        lv_obj_t *b = theme_box(s.pop, x + m * (POP_W + POP_GAP), (UI_H - POP_H) / 2, POP_W, POP_H, C_BG);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        gesture_bind(b, on_pop, m);
        s.pop_btn[m] = b;
        s.pop_icon[m] = theme_icon(b, mode_icon(m, 1), C_LABEL);
        lv_obj_align(s.pop_icon[m], LV_ALIGN_TOP_MID, 0, 14);
        s.pop_name[m] = theme_label(b, &font_value_20, C_TEXT, 0);
        lv_obj_align(s.pop_name[m], LV_ALIGN_TOP_MID, 0, 48);
        s.pop_hint[m] = theme_label(b, &font_tag_12, C_OFF, 1);
        lv_obj_align(s.pop_hint[m], LV_ALIGN_TOP_MID, 0, 78);
    }
    pop_texts();
    show(s.pop, 0);
}

void top_bar_init(void)
{
    s.box = theme_box(lv_layer_top(), 0, 0, UI_W, UI_TOPBAR_H, C_BG);

    /* meters: IN, OUT and the DSP load along the bottom */
    meter_create(0, IN_X);
    meter_create(1, OUT_X);
    lv_obj_t *track = theme_box(s.box, DSP_X, METER_Y, DSP_W, METER_H, C_LINE);
    s.dsp_fill = theme_box(track, 0, 0, 0, METER_H, C_TUNED);

    /* main screens: ≡, the mode, the preset, the scene, SAVE */
    int x = PAD;
    s.menu_hit = 0;
    lv_obj_t *menu = button(x, ICON_W, &s.menu_hit, on_menu, 0);
    lv_obj_center(theme_icon(menu, &icon_menu, C_TEXT));
    x += ICON_W + GAP;
    s.mode = button(x, ICON_W, &s.mode_hit, on_mode, 0);
    s.mode_icon = theme_icon(s.mode, &icon_mode_preset, C_ACCENT);
    lv_obj_center(s.mode_icon);
    s.preset = button(PRESET_X, PRESET_W, &s.preset_hit, on_preset, 0);
    s.number = theme_label(s.preset, &font_value_20, C_ACCENT, 0);
    lv_obj_align(s.number, LV_ALIGN_LEFT_MID, 8, 0);
    s.name = theme_label(s.preset, &font_value_20, C_TEXT, 0);
    lv_label_set_long_mode(s.name, LV_LABEL_LONG_DOT);
    /* BPM / VOL: drawn inside the preset, its own hit box (over the preset's) the height of the bar */
    s.tempo_hit = theme_box(s.box, 0, 0, 0, UI_TOPBAR_H, C_BG);
    lv_obj_set_style_bg_opa(s.tempo_hit, LV_OPA_TRANSP, 0);
    gesture_bind(s.tempo_hit, on_tempo, 0);
    s.tempo = theme_box(s.tempo_hit, 0, BTN_Y + (BTN_H - TEMPO_H) / 2, 0, TEMPO_H, C_BG);
    lv_obj_set_style_radius(s.tempo, 3, 0);
    lv_obj_set_style_border_width(s.tempo, 1, 0);
    lv_obj_set_style_border_color(s.tempo, rgb(C_WIRE), 0);
    s.bpm = theme_label(s.tempo, &font_label_12, C_LABEL, 0);
    s.vol = theme_label(s.tempo, &font_label_12, C_LABEL, 0);
    s.scene = button(SCENE_X, ICON_W, &s.scene_hit, on_scene, 0);
    s.scene_text = theme_label(s.scene, &font_value_20, C_LABEL, 0);
    s.save = button(UI_W - PAD - ICON_W, ICON_W, &s.save_hit, on_save, 0);
    s.save_icon = theme_icon(s.save, &icon_save_20, C_OFF);
    lv_obj_center(s.save_icon);

    /* other screens: ‹ and the title */
    lv_obj_t *back = button(PAD, BACK_W, &s.back_hit, on_back, 0);
    theme_set_border(back, C_WIRE);
    lv_obj_center(theme_icon(back, &icon_back_18, C_TEXT));
    s.title = theme_label(s.box, &font_tag_12, C_TEXT, 1);
    lv_obj_set_pos(s.title, PAD + BACK_W + GAP + 8, BTN_Y + (BTN_H - 15) / 2);
    s.view = button(UI_W - PAD - ICON_W, ICON_W, &s.view_hit, on_view, 0);
    s.view_list = view_icon(s.view, 1);
    s.view_cards = view_icon(s.view, 0);
    show(s.view_hit, 0);

    pop_create();
    sp_create();
}

/* ---------------------------------------------------------------- refreshing */

static void refresh_preset(void)
{
    const struct ui_ctx *ui = ui_context();
    const struct rig *r = ui->rig;
    char label[8], text[16];
    preset_label(r->index, label);
    theme_set_text(s.number, label);
    lv_snprintf(text, sizeof text, "BPM %d", rig_tempo(r));
    theme_set_text(s.bpm, text);
    lv_snprintf(text, sizeof text, "VOL %d", r->preset.volume);
    theme_set_text(s.vol, text);
    theme_set_text(s.name, r->preset.name[0] ? r->preset.name : T(MSG_SCREENS_EMPTY));
    /* BPM / VOL: a button at the right end of the preset, amber while the chain screen shows them */
    lv_obj_update_layout(s.bpm);
    lv_obj_update_layout(s.vol);
    int bw = lv_obj_get_width(s.bpm), tw = 8 + bw + 8 + lv_obj_get_width(s.vol) + 8;
    int px = lv_obj_get_x(s.preset_hit) + GAP / 2, pw = lv_obj_get_width(s.preset);
    int tx = px + pw - 4 - tw;
    lv_obj_set_pos(s.tempo_hit, tx - 3, 0);
    lv_obj_set_width(s.tempo_hit, tw + 6);
    lv_obj_set_pos(s.tempo, 3, BTN_Y + (BTN_H - TEMPO_H) / 2);
    lv_obj_set_width(s.tempo, tw);
    lv_obj_align(s.bpm, LV_ALIGN_LEFT_MID, 7, 0);
    lv_obj_align(s.vol, LV_ALIGN_LEFT_MID, 7 + bw + 8, 0);
    uint32_t tc = s.tempo_lit ? C_ACCENT : C_LABEL;
    theme_set_border(s.tempo, s.tempo_lit ? C_ACCENT : C_WIRE);
    theme_set_color(s.bpm, tc);
    theme_set_color(s.vol, tc);
    lv_obj_update_layout(s.number);
    int x = 8 + lv_obj_get_width(s.number) + 8;
    int room = tx - px - 8 - x;
    lv_obj_set_width(s.name, LV_SIZE_CONTENT);
    lv_obj_update_layout(s.name);
    if (lv_obj_get_width(s.name) > room)
        lv_obj_set_width(s.name, room > 0 ? room : 0);
    lv_obj_align(s.name, LV_ALIGN_LEFT_MID, x, 0);
    /* SAVE lights up while the preset has unsaved changes */
    theme_set_border(s.save, r->dirty ? C_ACCENT : C_FRAME);
    theme_set_icon(s.save_icon, &icon_save_20, r->dirty ? C_ACCENT : C_OFF);
}

/* the scene button at x (its drawn left edge), "C2": amber framed while the scenes are in play (edits go there) */
static void scene_place(int x)
{
    const struct rig *r = ui_context()->rig;
    int playing = rig_scene_editing(r);
    char text[4] = { 'C', (char)('1' + rig_scene_active(r) % SCENE_COUNT), 0 };
    lv_obj_set_x(s.scene_hit, x - GAP / 2);
    theme_set_text(s.scene_text, text);
    theme_set_color(s.scene_text, playing ? C_ACCENT : C_LABEL);
    lv_obj_center(s.scene_text);
    theme_set_border(s.scene, playing ? C_ACCENT : C_FRAME);
    theme_set_bg(s.scene, playing ? C_ACTIVE : C_BG);
    show(s.scene_hit, 1);
    if (sp_shown())
        sp_refresh();
}

void top_bar_scene(void)
{
    scene_place(UI_W - PAD - ICON_W);
}

void top_bar_tabs(enum screen_id active)
{
    show(s.back_hit, 0);
    show(s.title, 0);
    show(s.view_hit, 0);
    show(s.menu_hit, 1);
    show(s.mode_hit, 1);
    show(s.preset_hit, 1);
    show(s.tempo_hit, 1);
    show(s.save_hit, 1);
    s.tempo_lit = 0;
    /* the scene: always on the chain tab (where scenes are edited), on the others while they are in play; not on
       CENAS, which is the scenes */
    int scene = active == SCR_CHAIN || (active != SCR_SCENE && rig_scene_editing(ui_context()->rig));
    int pw = scene ? PRESET_W - ICON_W - GAP : PRESET_W;
    lv_obj_set_width(s.preset_hit, pw + GAP);
    lv_obj_set_width(s.preset, pw);
    if (scene)
        scene_place(SCENE_X);
    else
        show(s.scene_hit, 0);
    /* the footswitch mode, framed while its own tab is shown */
    int mode = footswitch_mode(ui_context());
    int home = active == ui_mode_tab();
    theme_set_icon(s.mode_icon, mode_icon(mode, 0), C_ACCENT);
    theme_set_border(s.mode, home ? C_ACCENT : C_FRAME);
    theme_set_bg(s.mode, home ? C_ACTIVE : C_BG);
    refresh_preset();
    if (pop_shown())
        pop_refresh();
}

void top_bar_tempo(int lit)
{
    if (s.tempo_lit != lit) {
        s.tempo_lit = lit;
        refresh_preset();
    }
}

void top_bar_sub(const char *title)
{
    show(s.menu_hit, 0);
    show(s.mode_hit, 0);
    show(s.preset_hit, 0);
    show(s.tempo_hit, 0);
    show(s.save_hit, 0);
    show(s.scene_hit, 0);
    show(s.view_hit, 0);
    show(s.back_hit, 1);
    show(s.title, 1);
    theme_set_text(s.title, title);
}

void top_bar_view(int list, void (*fn)(void))
{
    s.view_fn = fn;
    show(s.view_list, !list);                       /* the button shows the other view */
    show(s.view_cards, list);
    show(s.view_hit, 1);
}

/* 0..1 on the meter's dB scale */
float top_bar_meter_pos(float peak)
{
    if (peak <= 0)
        return 0;
    float db = 6.0206f * pitch_log2(peak);
    float p = (db + FLOOR_DB) / FLOOR_DB;
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

/* the DSP load in %: the slowest block against the block period */
int top_bar_dsp_percent(float block_ms)
{
    int p = (int)(block_ms * 100.0f * 44100.0f / (1000.0f * AUDIO_FRAMES) + 0.5f);
    return p > 100 ? 100 : p;
}

void top_bar_meters(float in, float out, float block_ms)
{
    float v[2] = { top_bar_meter_pos(in), top_bar_meter_pos(out) };
    for (int m = 0; m < 2; m++) {
        float fall = s.level[m] * FALL;
        s.level[m] = v[m] > fall ? v[m] : fall;
        s.peak[m] = s.level[m] > s.peak[m] - PEAK_FALL ? s.level[m] : s.peak[m] - PEAK_FALL;
        int lit = (int)(s.level[m] * SEGS + 0.5f), pk = (int)(s.peak[m] * SEGS + 0.5f) - 1;
        if (pk > SEGS - 1)
            pk = SEGS - 1;
        for (int i = 0; i < SEGS; i++) {
            uint32_t on = i >= 21 ? C_CLIP : i >= 16 ? C_ACCENT : C_TUNED;
            theme_set_bg(s.seg[m][i], i < lit || i == pk ? on : C_LINE);
        }
    }

    /* DSP: amber from 75 %, red from 90 % (the design's) */
    float pct = (float)top_bar_dsp_percent(block_ms);
    float fall = s.dsp * DSP_FALL;
    s.dsp = pct > fall ? pct : fall;
    int p = (int)(s.dsp + 0.5f);
    p = p > 100 ? 100 : p;
    uint32_t c = p >= 90 ? C_CLIP : p >= 75 ? C_ACCENT : C_TUNED;
    int w = DSP_W * p / 100;
    if (lv_obj_get_width(s.dsp_fill) != w)
        lv_obj_set_width(s.dsp_fill, w);
    theme_set_bg(s.dsp_fill, c);
}
