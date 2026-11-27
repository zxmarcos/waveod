/*
 * Drum machine (DRUM button; footswitch functions BATERIA, RITMO + / -), laid out like the original's: the
 * genres and the patterns of the genre in two lists, a play / stop button with the bar's four beats, and the
 * tempo / volume in cells.
 *
 * Controls: DRUM, MENU press or EFX = start / stop; MENU turns = pattern; knob 1 = genre; knob 2 = tempo
 * (when it follows the effects' tempo, that tempo: the preset's or the global one); knob 3 = volume;
 * CTRL = follow the effects' tempo on / off; EXIT = back. Touch: tap a genre or a pattern = choose it, drag a
 * list up / down = scroll it; the button = start / stop; tap TEMPO = follow on / off; drag a cell sideways = its
 * knob. Pattern, tempo, volume and following are global settings shared with the stock firmware, written when
 * the screen is left.
 *
 * The last style, MEUS RITMOS, lists waveod's own patterns (model/drum_user.h: MIDI files imported by waveod
 * Studio, 20 places); choosing one plays it instead of the bank's (a waveod setting: the stock firmware keeps its
 * own choice), and its tempo becomes the drums' while they do not follow the effects'. Empty places are listed
 * but skipped by MENU turns.
 */
#include "engine/drums.h"
#include "model/drum_names.h"
#include "model/drum_user.h"
#include "model/globals.h"
#include "platform/time.h"
#include "ui/i18n.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define LIST_Y    (UI_TOPBAR_H + 6)
#define CELLS_Y   (UI_H - 72)
#define ROW_H     25
#define ROWS      5                         /* rows shown in a list */
#define HEAD_H    18                        /* the lists' headings */
#define GENRE_X   UI_PAD
#define GENRE_W   124
#define PAT_X     (GENRE_X + GENRE_W + 6)
#define PAT_W     150
#define SIDE_X    (PAT_X + PAT_W + 12)
#define SIDE_W    (UI_W - UI_PAD - SIDE_X)
#define BUTTON    60
#define DOT       18
#define DRAG_PX   10
#define C_SEL_BG  0x161514
#define C_FRAME   0x2A2926

enum list { L_GENRE, L_PATTERN };
#define G_USER  DRUM_GENRES                 /* MEUS RITMOS, after the bank's styles */
#define GENRES  (DRUM_GENRES + 1)

struct list_obj {
    lv_obj_t *box, *row[ROWS], *edge[ROWS], *text[ROWS];
    int top, drag_top, shown_sel;           /* shown_sel: the choice it last scrolled to */
};

static struct {
    struct list_obj list[2];
    lv_obj_t *button, *icon, *state, *dots[4];
    struct param_cell cells[3];
    int drag_start;
    int user_view;                          /* MEUS RITMOS shown though none of them plays (none imported) */
    int was_playing;
} s SDRAM_BSS;

static int user_genre(void)
{
    return drum_user_selected() >= 0 || s.user_view;
}

static int cur_genre(const struct ui_ctx *ui)
{
    return user_genre() ? G_USER : ui->globals->drum_genre % DRUM_GENRES;
}

static void no_user_patterns(void)
{
    ui_toast(T(MSG_DRUM_NO_USER));
}

/* a pattern of the bank: it plays instead of MEUS RITMOS */
static void bank_pattern(struct ui_ctx *ui, int p)
{
    s.user_view = 0;
    drum_user_deselect();
    globals_set_drum_pattern(ui->globals, p);
}

static void play(struct ui_ctx *ui, int on)
{
    if (!drums_available()) {
        ui_toast(T(MSG_DRUM_NO_PATTERNS));
        return;
    }
    drums_play(on);
    ui->rev++;
}

static void pattern(struct ui_ctx *ui, int d)
{
    if (user_genre()) {
        int next = drum_user_step(drum_user_selected(), d);
        if (next < 0)
            no_user_patterns();
        else
            drum_user_select(next);
    } else {
        bank_pattern(ui, globals_drum_pattern(ui->globals) + d);
    }
    ui->rev++;
}

static void genre(struct ui_ctx *ui, int d)
{
    int g = ((cur_genre(ui) + d) % GENRES + GENRES) % GENRES;
    if (g == G_USER) {
        s.user_view = 1;
        int first = drum_user_step(-1, 1);
        if (first < 0)
            no_user_patterns();
        else
            drum_user_select(first);
    } else {
        bank_pattern(ui, drum_genre_first(g));
    }
    ui->rev++;
}

static void tempo(struct ui_ctx *ui, int d)
{
    struct globals *g = ui->globals;
    if (g->drum_sync) {                     /* the tempo the effects follow */
        if (ui->rig->global_bpm) {
            g->bpm = (uint16_t)(g->bpm + d < 40 ? 40 : g->bpm + d > 300 ? 300 : g->bpm + d);
            rig_set_global_tempo(ui->rig, g->bpm);
        } else {
            rig_set_bpm(ui->rig, ui->rig->preset.bpm + d);
        }
    } else {
        int b = g->drum_bpm + d;
        g->drum_bpm = (uint16_t)(b < 40 ? 40 : b > 300 ? 300 : b);
        drums_set_bpm(g->drum_bpm);
    }
    ui->rev++;
}

static void volume(struct ui_ctx *ui, int d)
{
    struct globals *g = ui->globals;
    int v = g->drum_volume + d;
    g->drum_volume = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    drums_set_volume(g->drum_volume);
    ui->rev++;
}

static int bpm_of(const struct ui_ctx *ui)
{
    return ui->globals->drum_sync ? rig_tempo(ui->rig) : ui->globals->drum_bpm;
}

/* the lists: how many rows, which is chosen */
static int list_count(const struct ui_ctx *ui, int l)
{
    if (l == L_GENRE)
        return GENRES;
    return user_genre() ? DRUM_USER_SLOTS : drum_genre_count[ui->globals->drum_genre % DRUM_GENRES];
}

static int list_sel(const struct ui_ctx *ui, int l)
{
    if (l == L_GENRE)
        return cur_genre(ui);
    if (user_genre())
        return drum_user_selected();
    return globals_drum_pattern(ui->globals) - drum_genre_first(ui->globals->drum_genre % DRUM_GENRES);
}

static int clamp_top(const struct ui_ctx *ui, int l, int top)
{
    int max = list_count(ui, l) - ROWS;
    return top > max ? (max > 0 ? max : 0) : top < 0 ? 0 : top;
}

/* keep the chosen row in view when the choice changes (a list scrolled by hand stays where it is) */
static void follow(const struct ui_ctx *ui, int l)
{
    struct list_obj *o = &s.list[l];
    int sel = list_sel(ui, l);
    if (sel == o->shown_sel)
        return;
    o->shown_sel = sel;
    if (sel < o->top + 1 || sel > o->top + ROWS - 2)
        o->top = clamp_top(ui, l, sel - ROWS / 2);
}

/* ---------------------------------------------------------------- touch */

static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    int l = g->tag / ROWS, r = g->tag % ROWS;
    struct list_obj *o = &s.list[l];
    if (g->kind == GESTURE_DRAG && g->vertical) {           /* scroll */
        if (g->first)
            o->drag_top = o->top;
        o->top = clamp_top(ui, l, o->drag_top - g->dy / ROW_H);
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        int i = o->top + r;
        if (i >= list_count(ui, l))
            return;
        if (l == L_GENRE)
            genre(ui, i - list_sel(ui, L_GENRE));
        else if (user_genre() && drum_user_select(i) != 0)
            ui_toast(T(MSG_DRUM_EMPTY_SLOT));
        else if (!user_genre())
            bank_pattern(ui, drum_genre_first(ui->globals->drum_genre % DRUM_GENRES) + i);
        ui->rev++;
    }
}

static void on_button(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        play(ui, !drums_playing());
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && g->tag == 1) {
        ui->globals->drum_sync = !ui->globals->drum_sync;
        globals_apply(ui->globals);
        ui->rev++;
        return;
    }
    if (g->kind != GESTURE_DRAG || g->vertical)
        return;
    if (g->first)
        s.drag_start = 0;
    int steps = g->dx / DRAG_PX;
    if (steps != s.drag_start) {
        int d = steps - s.drag_start;
        if (g->tag == 0)
            pattern(ui, d);
        else if (g->tag == 1)
            tempo(ui, d);
        else
            volume(ui, d);
        s.drag_start = steps;
    }
}

/* ---------------------------------------------------------------- screen */

static void list_create(lv_obj_t *root, int l, int x, int w, const char *heading)
{
    struct list_obj *o = &s.list[l];
    lv_obj_t *h = theme_label(root, &font_tag_12, C_LABEL, 2);
    lv_label_set_text_static(h, heading);
    lv_obj_set_pos(h, x + 2, LIST_Y);
    o->box = theme_box(root, x, LIST_Y + HEAD_H, w, ROWS * ROW_H + 2, C_BG);
    lv_obj_set_style_border_width(o->box, 1, 0);
    lv_obj_set_style_border_color(o->box, rgb(C_FRAME), 0);
    lv_obj_set_style_radius(o->box, 3, 0);
    for (int r = 0; r < ROWS; r++) {
        o->row[r] = theme_box(o->box, 0, r * ROW_H, w - 2, ROW_H, C_BG);
        gesture_bind(o->row[r], on_row, l * ROWS + r);
        o->edge[r] = theme_box(o->row[r], 0, 0, 3, ROW_H, C_ACCENT);
        o->text[r] = theme_label(o->row[r], &font_body_14, C_NEUTRAL_ON, 0);
        lv_obj_set_pos(o->text[r], 10, (ROW_H - 18) / 2);
        lv_obj_set_width(o->text[r], w - 16);
        lv_label_set_long_mode(o->text[r], LV_LABEL_LONG_DOT);
    }
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    list_create(root, L_GENRE, GENRE_X, GENRE_W, T(MSG_DRUM_STYLE));
    list_create(root, L_PATTERN, PAT_X, PAT_W, T(MSG_DRUM_RHYTHM));

    /* play / stop, its state, the bar's beats */
    int bx = SIDE_X + (SIDE_W - BUTTON) / 2;
    s.button = theme_box(root, bx, LIST_Y + HEAD_H, BUTTON, BUTTON, C_BG);
    lv_obj_set_style_radius(s.button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s.button, 2, 0);
    gesture_bind(s.button, on_button, 0);
    s.icon = theme_icon(s.button, &icon_play_28, C_TEXT);
    lv_obj_center(s.icon);
    s.state = theme_label(root, &font_label_12, C_LABEL, 2);
    lv_obj_set_width(s.state, SIDE_W);
    lv_obj_set_style_text_align(s.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s.state, SIDE_X, LIST_Y + HEAD_H + BUTTON + 8);
    int dx = (SIDE_W - 4 * DOT) / 5;
    for (int i = 0; i < 4; i++) {
        s.dots[i] = theme_box(root, SIDE_X + dx + i * (DOT + dx), LIST_Y + HEAD_H + BUTTON + 34, DOT, DOT, C_TRACK);
        lv_obj_set_style_radius(s.dots[i], LV_RADIUS_CIRCLE, 0);
    }

    theme_box(root, 0, CELLS_Y, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.cells[i], root, i * UI_W / 3, CELLS_Y + 1, UI_W / 3, UI_H - CELLS_Y - 1, &font_value_26,
                          i > 0);
        gesture_bind(s.cells[i].box, on_cell, i);
    }
}

static void enter(struct ui_ctx *ui)
{
    s.user_view = 0;
    for (int l = 0; l < 2; l++) {
        s.list[l].top = 0;
        s.list[l].shown_sel = -1;
    }
    follow(ui, L_GENRE);
    follow(ui, L_PATTERN);
}

static void leave(struct ui_ctx *ui)
{
    if (globals_save(ui->globals, &ui->rig->src))
        ui_toast(T(MSG_DRUM_SAVE_FAILED));
}

static void list_refresh(const struct ui_ctx *ui, int l, int playing)
{
    struct list_obj *o = &s.list[l];
    int n = list_count(ui, l), sel = list_sel(ui, l);
    o->top = clamp_top(ui, l, o->top);
    for (int r = 0; r < ROWS; r++) {
        int i = o->top + r, cur = i == sel;
        if (i >= n) {
            lv_obj_add_flag(o->row[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(o->row[r], LV_OBJ_FLAG_HIDDEN);
        int empty = 0;
        if (l == L_GENRE) {
            theme_set_text(o->text[r], i < DRUM_GENRES ? drum_genre_names[i] : T(MSG_DRUM_MY_RHYTHMS));
        } else if (user_genre()) {
            struct drum_user_info info;
            char text[DRUM_USER_NAME + 8];
            empty = drum_user_info(i, &info) != 0;
            lv_snprintf(text, sizeof text, "%d  %s", i + 1, empty ? T(MSG_DRUM_SLOT_EMPTY) : info.name);
            theme_set_text(o->text[r], text);
        } else {
            theme_set_text(o->text[r], drum_pattern_names[drum_genre_first(ui->globals->drum_genre % DRUM_GENRES) + i]);
        }
        theme_set_bg(o->row[r], cur ? C_SEL_BG : C_BG);
        theme_set_bg(o->edge[r], cur && playing ? C_TUNED : C_ACCENT);
        if (cur)
            lv_obj_clear_flag(o->edge[r], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(o->edge[r], LV_OBJ_FLAG_HIDDEN);
        theme_set_color(o->text[r], cur ? C_TEXT : empty ? C_OFF : C_LABEL);
    }
}

static void refresh(struct ui_ctx *ui)
{
    const struct globals *g = ui->globals;
    int p = globals_drum_pattern(g), on = drums_playing(), bpm = bpm_of(ui);
    top_bar_sub(T(MSG_SHARED_FN_DRUM));
    follow(ui, L_GENRE);
    follow(ui, L_PATTERN);
    list_refresh(ui, L_GENRE, on);
    list_refresh(ui, L_PATTERN, on);

    uint32_t col = !drums_available() ? C_DIM : on ? C_TUNED : C_ACCENT;
    theme_set_bg(s.button, on ? 0x12241A : C_SEL_BG);
    theme_set_border(s.button, col);
    theme_set_icon(s.icon, on ? &icon_stop_28 : &icon_play_28, col);
    lv_obj_center(s.icon);
    theme_set_text(s.state, T(!drums_available() ? MSG_DRUM_NO_RHYTHMS : on ? MSG_DRUM_PLAYING : MSG_DRUM_STOPPED));
    theme_set_color(s.state, on ? C_TUNED : C_LABEL);
    uint32_t beat = on ? time_ms() * (uint32_t)bpm / 60000u : 0;
    for (int i = 0; i < 4; i++)
        theme_set_bg(s.dots[i], on && (int)(beat % 4) == i ? (i ? C_ACCENT : C_TUNED) : C_TRACK);

    char v[16];
    int user = drum_user_selected();
    if (user >= 0) {
        struct drum_user_info info;
        int once = drum_user_info(user, &info) == 0 && (info.flags & DRUM_USER_ONCE);
        lv_snprintf(v, sizeof v, "M%d", user + 1);
        param_cell_set(&s.cells[0], T(MSG_DRUM_MY_RHYTHM), once ? T(MSG_DRUM_ONCE) : "", v,
                       (float)user / (DRUM_USER_SLOTS - 1), C_LABEL, C_TEXT, C_ACCENT);
    } else {
        lv_snprintf(v, sizeof v, "%d", p + 1);
        param_cell_set(&s.cells[0], T(MSG_DRUM_RHYTHM), "", v, (float)p / (DRUM_PATTERNS - 1), C_LABEL, C_TEXT,
                       C_ACCENT);
    }
    lv_snprintf(v, sizeof v, "%d BPM", bpm);
    param_cell_set(&s.cells[1], T(g->drum_sync ? MSG_DRUM_TEMPO_SYNC : MSG_DRUM_TEMPO), "", v,
                   (float)(bpm - 40) / 260.0f, C_LABEL, C_TEXT, g->drum_sync ? C_OFF : C_ACCENT);
    lv_snprintf(v, sizeof v, "%d", g->drum_volume);
    param_cell_set(&s.cells[2], "VOLUME", "", v, g->drum_volume / 100.0f, C_LABEL, C_TEXT, C_ACCENT);
}

static void tick(struct ui_ctx *ui)
{
    static uint32_t shown;
    int playing = drums_playing();
    if (playing != s.was_playing) {             /* started, stopped, or a pattern that plays once ended */
        s.was_playing = playing;
        ui->rev++;
    }
    if (!playing)
        return;
    uint32_t beat = time_ms() * (uint32_t)bpm_of(ui) / 60000u;
    if (beat != shown) {
        shown = beat;
        ui->rev++;
    }
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        pattern(ui, d);
        return 1;
    }
    if ((d = ui_turned(ev, ENC_KNOB1))) {
        genre(ui, d);
        return 1;
    }
    if (ev->type == IN_TURN && ev->id == ENC_KNOB2) {
        tempo(ui, ev->fast);
        return 1;
    }
    if (ev->type == IN_TURN && ev->id == ENC_KNOB3) {
        volume(ui, ev->delta);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU) || ui_pressed(ev, BTN_EFX) || ui_pressed(ev, BTN_DRUM)) {
        play(ui, !drums_playing());
        return 1;
    }
    if (ui_pressed(ev, BTN_CTRL)) {
        ui->globals->drum_sync = !ui->globals->drum_sync;
        globals_apply(ui->globals);
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_drum = { create, enter, refresh, input, leave, tick };
