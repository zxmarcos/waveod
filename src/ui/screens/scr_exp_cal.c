/*
 * GLOBAL › PEDAIS EXP › CALIBRAR: the expression pedals' range, in the original's steps (heel, toe, then the toe
 * switch for EXP 1) with the pedal drawn moving as it moves.
 *
 * Left, EXP 1 / EXP 2 and the pedal: its treadle at the angle the pedal is at, the toe switch lit while it is
 * pressed. Right, the steps with their values and what to do, and the button (COMEÇAR, PRÓXIMO, SALVAR). Below,
 * the pedal's whole travel (0..255) with the calibrated range, where it is now and the toe switch's threshold.
 *
 * Nothing changes until the last step: SALVAR writes the range (and the threshold), EXIT before that leaves the
 * calibration as it was. The toe switch threshold is three quarters of the way from the toe to the strongest
 * press; with no press past the toe the old threshold stays (or none, when it falls inside the range).
 *
 * Controls: MENU press = the button, MENU turns = EXP 1 / EXP 2, knobs 1-3 = heel, toe, toe switch (fine
 * adjustment, at once), EXIT = cancel the steps / back. Touch: the button, EXP 1 / EXP 2.
 */
#include "model/globals.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define TOP        (UI_TOPBAR_H + 8)
#define PILL_W     60
#define PILL_H     24
#define ART_X      UI_PAD
#define ART_Y      (TOP + PILL_H + 4)
#define ART_W      208
#define ART_H      120
#define PIVOT_X    22                       /* in the drawing */
#define PIVOT_Y    84
#define TREAD_L    170
#define TREAD_T    12
#define HEEL_DEG   (-21)                    /* the treadle at the heel (toe up) and at the toe (flat) */
#define TOE_DEG    (-1)
#define BASE_Y     92
#define PANEL_X    238
#define PANEL_W    (UI_W - UI_PAD - PANEL_X)
#define STEP_H     26
#define HINT_Y     (TOP + 3 * STEP_H + 6)
#define BUTTON_Y   (TOP + 122)
#define BUTTON_H   30
#define RANGE_Y    (TOP + 160)
#define RANGE_W    (UI_W - 2 * UI_PAD)
#define LABEL_Y    (RANGE_Y + 10)
#define MIN_SPAN   32                       /* a shorter range is a mistake */
#define TOE_MARGIN 8                        /* a press this far past the toe is the toe switch */
#define C_FRAME    0x2A2926
#define C_SEL_BG   0x161514
#define C_RANGE    0x5A4320                 /* the calibrated range on the travel: dim amber */

enum step { ST_IDLE, ST_HEEL, ST_TOE, ST_SWITCH };

static const uint16_t step_names[3] = { MSG_EXPCAL_HEEL, MSG_EXPCAL_TOE, MSG_EXPCAL_TOE_SWITCH };

static struct {
    lv_obj_t *pill[2], *pill_text[2];
    lv_obj_t *art, *base, *tread, *pad, *pivot, *toe_sw, *heel_tag, *toe_tag;
    lv_point_precise_t base_pts[5], tread_pts[5], pad_pts[5];
    lv_obj_t *step_box[3], *step_no[3], *step_name[3], *step_val[3];
    lv_obj_t *hint, *button, *button_text;
    lv_obj_t *track, *range, *now_mark, *toe_mark;
    lv_obj_t *label[4], *value[4];
    enum step step;
    int lo, hi, peak;                       /* the steps' values */
    int seen;                               /* the pedal's value drawn */
} s SDRAM_BSS;

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int pedal_now(const struct ui_ctx *ui)
{
    return expression_value(ui->pedals, ui->exp_cal);
}

static int has_switch(const struct ui_ctx *ui)
{
    return ui->exp_cal == 0;                /* EXP 1 only (the original's) */
}

/* ---------------------------------------------------------------- actions */

static void cancel(void)
{
    s.step = ST_IDLE;
}

static void next(struct ui_ctx *ui)
{
    struct globals *g = ui->globals;
    int n = ui->exp_cal, now = pedal_now(ui);
    char text[48];
    switch (s.step) {
    case ST_IDLE:
        s.step = ST_HEEL;
        break;
    case ST_HEEL:
        s.lo = now;
        s.step = ST_TOE;
        break;
    case ST_TOE:
        s.hi = now;
        if (s.hi - s.lo < MIN_SPAN) {
            ui_toast(T(MSG_EXPCAL_SHORT_RANGE));
            s.step = ST_HEEL;
            break;
        }
        if (has_switch(ui)) {
            s.peak = now;
            s.step = ST_SWITCH;
            break;
        }
        g->exp_min[n] = (int16_t)s.lo;
        g->exp_max[n] = (int16_t)s.hi;
        lv_snprintf(text, sizeof text, T(MSG_EXPCAL_CALIBRATED), n + 1);
        ui_toast(text);
        s.step = ST_IDLE;
        break;
    case ST_SWITCH:
        g->exp_min[n] = (int16_t)s.lo;
        g->exp_max[n] = (int16_t)s.hi;
        if (s.peak > s.hi + TOE_MARGIN) {
            g->exp_toe[n] = (int16_t)clamp((s.hi + 3 * s.peak) / 4, s.hi + 1, 255);
            lv_snprintf(text, sizeof text, T(MSG_EXPCAL_CALIBRATED), n + 1);
        } else {
            if (g->exp_toe[n] <= s.hi)
                g->exp_toe[n] = 255;        /* inside the range it would press itself: none */
            lv_snprintf(text, sizeof text, T(MSG_EXPCAL_CALIBRATED_NO_SWITCH), n + 1);
        }
        ui_toast(text);
        s.step = ST_IDLE;
        break;
    }
    ui->rev++;
}

static void pick(struct ui_ctx *ui, int n)
{
    if (s.step != ST_IDLE || n == ui->exp_cal)
        return;
    ui->exp_cal = n;
    s.seen = -1;
    ui->rev++;
}

static void fine(struct ui_ctx *ui, int knob, int d)
{
    struct globals *g = ui->globals;
    int n = ui->exp_cal;
    if (s.step != ST_IDLE)
        return;
    if (knob == 0)
        g->exp_min[n] = (int16_t)clamp(g->exp_min[n] + d, 0, g->exp_max[n] - 1);
    else if (knob == 1)
        g->exp_max[n] = (int16_t)clamp(g->exp_max[n] + d, g->exp_min[n] + 1, 255);
    else if (has_switch(ui))
        g->exp_toe[n] = (int16_t)clamp(g->exp_toe[n] + d, g->exp_max[n] + 1, 255);
    ui->rev++;
}

/* ---------------------------------------------------------------- touch */

static void on_pill(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        pick(ui, g->tag);
}

static void on_button(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        next(ui);
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *line(lv_obj_t *parent, uint32_t colour, int width)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, rgb(colour), 0);
    lv_obj_set_style_line_rounded(l, 1, 0);
    return l;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    static const char *const pills[2] = { "EXP 1", "EXP 2" };
    for (int n = 0; n < 2; n++) {
        s.pill[n] = theme_box(root, ART_X + n * (PILL_W + 6), TOP, PILL_W, PILL_H, C_BG);
        lv_obj_set_style_radius(s.pill[n], 3, 0);
        lv_obj_set_style_border_width(s.pill[n], 1, 0);
        gesture_bind(s.pill[n], on_pill, n);
        s.pill_text[n] = theme_label(s.pill[n], &font_label_12, C_OFF, 1);
        lv_label_set_text_static(s.pill_text[n], pills[n]);
        lv_obj_center(s.pill_text[n]);
    }

    /* the pedal: base, treadle on its pivot at the heel, the toe switch under the toe */
    s.art = theme_box(root, ART_X, ART_Y, ART_W, ART_H, C_BG);
    lv_obj_set_style_bg_opa(s.art, LV_OPA_TRANSP, 0);
    static const lv_point_precise_t base[5] = { { 6, BASE_Y }, { ART_W - 6, BASE_Y }, { ART_W - 6, BASE_Y + 14 },
                                                { 6, BASE_Y + 14 }, { 6, BASE_Y } };
    for (int i = 0; i < 5; i++)
        s.base_pts[i] = base[i];
    s.base = line(s.art, C_LABEL, 2);
    lv_line_set_points(s.base, s.base_pts, 5);
    s.toe_sw = theme_box(s.art, PIVOT_X + TREAD_L - 22, BASE_Y - 5, 16, 5, C_DIM);
    lv_obj_set_style_radius(s.toe_sw, 1, 0);
    s.tread = line(s.art, C_TEXT, 2);
    s.pad = line(s.art, C_DIM, 2);
    s.pivot = theme_box(s.art, PIVOT_X - 4, PIVOT_Y - 4, 9, 9, C_LABEL);
    lv_obj_set_style_radius(s.pivot, LV_RADIUS_CIRCLE, 0);
    s.heel_tag = theme_label(s.art, &font_tag_12, C_OFF, 1);
    lv_label_set_text_static(s.heel_tag, T(MSG_EXPCAL_HEEL));
    lv_obj_set_pos(s.heel_tag, 6, BASE_Y + 17);
    s.toe_tag = theme_label(s.art, &font_tag_12, C_OFF, 1);
    lv_label_set_text_static(s.toe_tag, T(MSG_EXPCAL_TOE));
    lv_obj_align(s.toe_tag, LV_ALIGN_TOP_RIGHT, -6, BASE_Y + 17);

    /* the steps */
    for (int i = 0; i < 3; i++) {
        int y = TOP + i * STEP_H;
        s.step_box[i] = theme_box(root, PANEL_X, y, PANEL_W, STEP_H - 4, C_BG);
        lv_obj_set_style_radius(s.step_box[i], 2, 0);
        s.step_no[i] = theme_label(s.step_box[i], &font_tag_12, C_OFF, 0);
        lv_label_set_text_fmt(s.step_no[i], "%d", i + 1);
        lv_obj_set_pos(s.step_no[i], 8, 4);
        s.step_name[i] = theme_label(s.step_box[i], &font_label_12, C_OFF, 1);
        lv_label_set_text_static(s.step_name[i], T(step_names[i]));
        lv_obj_set_pos(s.step_name[i], 24, 4);
        s.step_val[i] = theme_label(s.step_box[i], &font_body_14, C_OFF, 0);
        lv_obj_align(s.step_val[i], LV_ALIGN_TOP_RIGHT, -8, 2);
    }
    s.hint = theme_label(root, &font_body_14, C_NEUTRAL_ON, 0);
    lv_obj_set_pos(s.hint, PANEL_X, HINT_Y);
    lv_obj_set_width(s.hint, PANEL_W);
    lv_label_set_long_mode(s.hint, LV_LABEL_LONG_WRAP);
    s.button = theme_box(root, PANEL_X, BUTTON_Y, PANEL_W, BUTTON_H, C_SEL_BG);
    lv_obj_set_style_radius(s.button, 3, 0);
    lv_obj_set_style_border_width(s.button, 1, 0);
    lv_obj_set_style_border_color(s.button, rgb(C_ACCENT), 0);
    gesture_bind(s.button, on_button, 0);
    s.button_text = theme_label(s.button, &font_label_12, C_ACCENT, 2);
    lv_obj_center(s.button_text);

    /* the travel */
    s.track = theme_box(root, UI_PAD, RANGE_Y, RANGE_W, 6, C_TRACK);
    lv_obj_set_style_radius(s.track, 1, 0);
    s.range = theme_box(s.track, 0, 0, 0, 6, C_RANGE);
    s.toe_mark = theme_box(root, UI_PAD, RANGE_Y - 4, 2, 14, C_CLIP);
    s.now_mark = theme_box(root, UI_PAD, RANGE_Y - 5, 3, 16, C_TEXT);
    for (int i = 0; i < 4; i++) {
        int x = UI_PAD + i * (RANGE_W / 4);
        s.label[i] = theme_label(root, &font_tag_12, C_OFF, 1);
        lv_obj_set_pos(s.label[i], x, LABEL_Y + 4);
        s.value[i] = theme_label(root, &font_value_20, C_TEXT, 0);
        lv_obj_set_pos(s.value[i], x, LABEL_Y + 18);
    }
}

static void enter(struct ui_ctx *ui)
{
    (void)ui;
    s.step = ST_IDLE;
    s.seen = -1;
}

static void leave(struct ui_ctx *ui)
{
    cancel();
    if (globals_save(ui->globals, &ui->rig->src))
        ui_toast(T(MSG_EXPCAL_SAVE_FAILED));
}

static void tick(struct ui_ctx *ui)
{
    int now = pedal_now(ui);
    if (s.step == ST_SWITCH && now > s.peak)
        s.peak = now;
    if (now != s.seen)
        ui->rev++;
}

/* the treadle at pos (0 = heel, 1 = toe) */
static void draw_treadle(float pos, uint32_t colour)
{
    int deg = HEEL_DEG + (int)((float)(TOE_DEG - HEEL_DEG) * pos + 0.5f);
    int32_t c = lv_trigo_sin((int16_t)(deg + 90)), sn = lv_trigo_sin((int16_t)deg);  /* x 32767 */
    int32_t ux = TREAD_L * c / LV_TRIGO_SIN_MAX, uy = TREAD_L * sn / LV_TRIGO_SIN_MAX;
    int32_t nx = TREAD_T * sn / LV_TRIGO_SIN_MAX, ny = -TREAD_T * c / LV_TRIGO_SIN_MAX;   /* up from the surface */
    s.tread_pts[0] = (lv_point_precise_t){ PIVOT_X, PIVOT_Y };
    s.tread_pts[1] = (lv_point_precise_t){ PIVOT_X + ux, PIVOT_Y + uy };
    s.tread_pts[2] = (lv_point_precise_t){ PIVOT_X + ux + nx, PIVOT_Y + uy + ny };
    s.tread_pts[3] = (lv_point_precise_t){ PIVOT_X + nx, PIVOT_Y + ny };
    s.tread_pts[4] = s.tread_pts[0];
    lv_line_set_points(s.tread, s.tread_pts, 5);
    lv_obj_set_style_line_color(s.tread, rgb(colour), 0);

    /* the rubber pad on top, from 40 % of the treadle to the toe */
    int32_t ax = PIVOT_X + ux * 40 / 100 + nx * 3 / 12, ay = PIVOT_Y + uy * 40 / 100 + ny * 3 / 12;
    int32_t bx = PIVOT_X + ux * 94 / 100 + nx * 3 / 12, by = PIVOT_Y + uy * 94 / 100 + ny * 3 / 12;
    int32_t hx = nx * 6 / 12, hy = ny * 6 / 12;
    s.pad_pts[0] = (lv_point_precise_t){ ax, ay };
    s.pad_pts[1] = (lv_point_precise_t){ bx, by };
    s.pad_pts[2] = (lv_point_precise_t){ bx + hx, by + hy };
    s.pad_pts[3] = (lv_point_precise_t){ ax + hx, ay + hy };
    s.pad_pts[4] = s.pad_pts[0];
    lv_line_set_points(s.pad, s.pad_pts, 5);
}

static void place(lv_obj_t *mark, int v, int w)
{
    lv_obj_set_x(mark, UI_PAD + clamp(v, 0, 255) * (RANGE_W - w) / 255);
}

static void refresh(struct ui_ctx *ui)
{
    const struct globals *g = ui->globals;
    int n = ui->exp_cal, now = pedal_now(ui), sw = has_switch(ui);
    s.seen = now;
    top_bar_sub(T(MSG_EXPCAL_TITLE));

    for (int k = 0; k < 2; k++) {
        int sel = k == n;
        theme_set_bg(s.pill[k], sel ? C_SEL_BG : C_BG);
        theme_set_border(s.pill[k], sel ? C_ACCENT : C_FRAME);
        theme_set_color(s.pill_text[k], sel ? C_ACCENT : s.step == ST_IDLE ? C_LABEL : C_DIM);
    }

    /* the pedal: in the steps against its whole travel, else against the calibration */
    int lo = s.step == ST_IDLE ? g->exp_min[n] : 0, hi = s.step == ST_IDLE ? g->exp_max[n] : 255;
    float pos = hi > lo ? (float)(clamp(now, lo, hi) - lo) / (float)(hi - lo) : 0.0f;
    draw_treadle(pos, s.step == ST_HEEL || s.step == ST_TOE ? C_ACCENT : C_TEXT);
    int pressed = sw && (s.step == ST_SWITCH ? now > s.hi + TOE_MARGIN : s.step == ST_IDLE && now > g->exp_toe[n]);
    lv_obj_set_style_bg_color(s.toe_sw, rgb(pressed ? C_CLIP : s.step == ST_SWITCH ? C_ACCENT : C_DIM), 0);
    if (sw)
        lv_obj_clear_flag(s.toe_sw, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s.toe_sw, LV_OBJ_FLAG_HIDDEN);

    /* the steps: done (white), active (amber, the live value), to come (dim); the switch for EXP 1 only */
    int vals[3] = { s.step == ST_IDLE ? g->exp_min[n] : s.lo, s.step == ST_IDLE ? g->exp_max[n] : s.hi,
                    s.step == ST_IDLE ? g->exp_toe[n] : s.peak };
    for (int i = 0; i < 3; i++) {
        int active = s.step == ST_HEEL + i, done = s.step > ST_HEEL + i;
        if (i == 2 && !sw) {
            lv_obj_add_flag(s.step_box[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s.step_box[i], LV_OBJ_FLAG_HIDDEN);
        char v[12];
        if (active && i < 2)
            lv_snprintf(v, sizeof v, "%d", now);
        else if (i == 2 && s.step == ST_IDLE && vals[2] > 254)
            lv_snprintf(v, sizeof v, "%s", T(MSG_EXPCAL_NONE));
        else if (active || done || s.step == ST_IDLE)
            lv_snprintf(v, sizeof v, "%d", vals[i]);
        else
            lv_snprintf(v, sizeof v, "\xE2\x80\x93");
        uint32_t col = active ? C_ACCENT : done || s.step == ST_IDLE ? C_TEXT : C_DIM;
        theme_set_bg(s.step_box[i], active ? C_SEL_BG : C_BG);
        lv_obj_set_style_border_side(s.step_box[i], LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(s.step_box[i], active ? 2 : 0, 0);
        lv_obj_set_style_border_color(s.step_box[i], rgb(C_ACCENT), 0);
        theme_set_color(s.step_no[i], active ? C_ACCENT : C_OFF);
        theme_set_color(s.step_name[i], col == C_TEXT ? C_LABEL : col);
        theme_set_text(s.step_val[i], v);
        theme_set_color(s.step_val[i], col);
        lv_obj_align(s.step_val[i], LV_ALIGN_TOP_RIGHT, -8, 2);
    }

    const char *hint, *button;
    switch (s.step) {
    case ST_HEEL:
        hint = T(MSG_EXPCAL_HINT_HEEL);
        button = T(MSG_EXPCAL_NEXT);
        break;
    case ST_TOE:
        hint = T(sw ? MSG_EXPCAL_HINT_TOE_SWITCH : MSG_EXPCAL_HINT_TOE);
        button = T(sw ? MSG_EXPCAL_NEXT : MSG_SCREENS_SAVE);
        break;
    case ST_SWITCH:
        hint = T(MSG_EXPCAL_HINT_SWITCH);
        button = T(MSG_SCREENS_SAVE);
        break;
    default:
        hint = T(n == 1 && g->exp2_jack != EXP2_PEDAL ? MSG_EXPCAL_HINT_JACK : MSG_EXPCAL_HINT_IDLE);
        button = T(MSG_EXPCAL_CALIBRATE);
        break;
    }
    theme_set_text(s.hint, hint);
    theme_set_text(s.button_text, button);
    lv_obj_center(s.button_text);

    /* the travel: range, now, toe switch */
    int a = s.step == ST_IDLE ? g->exp_min[n] : s.step > ST_HEEL ? s.lo : -1;
    int b = s.step == ST_IDLE ? g->exp_max[n] : s.step > ST_TOE ? s.hi : -1;
    if (a >= 0 && b > a) {
        lv_obj_set_x(s.range, a * RANGE_W / 255);
        lv_obj_set_width(s.range, (b - a) * RANGE_W / 255);
    } else {
        lv_obj_set_width(s.range, 0);
    }
    place(s.now_mark, now, 3);
    int toe = s.step == ST_IDLE ? g->exp_toe[n] : -1;
    if (sw && toe >= 0 && toe < 255) {
        lv_obj_clear_flag(s.toe_mark, LV_OBJ_FLAG_HIDDEN);
        place(s.toe_mark, toe, 2);
    } else {
        lv_obj_add_flag(s.toe_mark, LV_OBJ_FLAG_HIDDEN);
    }

    static const uint16_t labels[4] = { MSG_EXPCAL_NOW, MSG_EXPCAL_K1_HEEL, MSG_EXPCAL_K2_TOE, MSG_EXPCAL_K3_SWITCH };
    int nums[4] = { now, g->exp_min[n], g->exp_max[n], g->exp_toe[n] };
    for (int i = 0; i < 4; i++) {
        int shown = i < 3 || sw;
        theme_set_text(s.label[i], shown ? T(labels[i]) : "");
        char v[12];
        if (!shown)
            v[0] = 0;
        else if (i == 3 && nums[3] > 254)
            lv_snprintf(v, sizeof v, "\xE2\x80\x93");
        else
            lv_snprintf(v, sizeof v, "%d", nums[i]);
        theme_set_text(s.value[i], v);
        theme_set_color(s.value[i], i == 0 ? C_TEXT : s.step == ST_IDLE ? C_NEUTRAL_ON : C_DIM);
        theme_set_color(s.label[i], i == 0 ? C_LABEL : C_OFF);
    }
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        pick(ui, ui->exp_cal ^ 1);
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        d = ui_turned(ev, k);
        if (d) {
            fine(ui, k - ENC_KNOB1, d);
            return 1;
        }
    }
    if (ui_pressed(ev, BTN_MENU)) {
        next(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_GLOBAL)) {
        if (s.step != ST_IDLE) {
            cancel();
            ui->rev++;
        } else {
            ui_back();
        }
        return 1;
    }
    return 0;
}

const struct screen screen_exp_cal = { create, enter, refresh, input, leave, tick };
