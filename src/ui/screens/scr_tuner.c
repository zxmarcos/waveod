/*
 * 04 Tuner (tab TUNER): the note in very large letters with its octave, a scale of 41 ticks (-50..+50 cents,
 * the centre one tall and white) with a sliding bar, the frequency and the deviation. Green within 3 cents,
 * amber otherwise, as in the design. The output is muted unless the settings say otherwise.
 *
 * STROBE (GLOBAL › AFINADOR › VISOR, or a tap on the scale): two bands of stripes in place of the scale, running
 * right when the note is sharp and left when it is flat, faster the further off (the lower band at half the
 * speed, for the last cents); they stand still in tune.
 *
 * Opened by its tab, by holding a footswitch or from the settings. Any footswitch, EXIT or MENU goes back.
 */
#include "model/tuner.h"
#include "platform/time.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

LV_FONT_DECLARE(font_note_140)

#define NOTE_Y0     UI_TOPBAR_H
#define NOTE_Y1     176
#define SCALE_Y     (NOTE_Y1 + 2)
#define SCALE_H     43
#define SCALE_X     38
#define SCALE_W     (UI_W - 2 * SCALE_X)
#define TICKS       41
#define BAR_W       8
#define FOOT_Y      (SCALE_Y + SCALE_H)
#define IN_TUNE     3.0f
#define C_SILENT    C_DIM
#define STROBE_P    32                      /* the upper band's period (px); the lower band's is twice that */
#define STROBE_K    8.0f                    /* px per second per cent */
#define STROBE_GAP  5

static struct {
    lv_obj_t *body, *note, *octave;
    lv_obj_t *ticks[TICKS], *indicator;
    lv_obj_t *hz, *cents, *ref;
    lv_obj_t *scale_hit, *strobe;
    float phase[2];                         /* the bands' offsets (px) */
    float speed;                            /* px per second of the upper band */
    uint32_t colour, moved_at;
} s SDRAM_BSS;

/* the strobe: two bands of stripes, each period half lit, offset by its phase */
static void strobe_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.strobe, &a);
    int band = (SCALE_H - STROBE_GAP) / 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = rgb(s.colour);
    d.radius = 1;
    for (int r = 0; r < 2; r++) {
        int p = STROBE_P << r, w = p / 2, y = a.y1 + r * (band + STROBE_GAP);
        int off = (int)s.phase[r];
        d.bg_opa = r ? LV_OPA_60 : LV_OPA_COVER;
        for (int x = off - p; x < SCALE_W; x += p) {
            lv_area_t b = { a.x1 + (x < 0 ? 0 : x), y, a.x1 + (x + w > SCALE_W ? SCALE_W : x + w) - 1, y + band - 1 };
            if (b.x2 >= b.x1)
                lv_draw_rect(layer, &d, &b);
        }
    }
}

static void on_scale(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    ui->settings->tuner_strobe = !ui->settings->tuner_strobe;
    settings_touch();                       /* written after a pause */
    ui->rev++;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    s.note = theme_label(root, &font_note_140, C_SILENT, 0);
    s.octave = theme_label(root, &font_title_40, C_OFF, 0);
    for (int i = 0; i < TICKS; i++) {
        int center = i == TICKS / 2, major = i % 5 == 0;
        int h = center ? SCALE_H : major ? 24 : 12;
        int x = SCALE_X + i * SCALE_W / (TICKS - 1) - 1;
        s.ticks[i] = theme_box(root, x, SCALE_Y + (SCALE_H - h) / 2, 2, h,
                               center ? C_TEXT : major ? C_OFF : C_BORDER_OFF);
    }
    s.indicator = theme_box(root, 0, SCALE_Y, BAR_W, SCALE_H, C_ACCENT);
    s.strobe = theme_box(root, SCALE_X, SCALE_Y, SCALE_W, SCALE_H, C_BG);
    lv_obj_add_event_cb(s.strobe, strobe_draw, LV_EVENT_DRAW_MAIN, 0);
    s.scale_hit = theme_box(root, 0, SCALE_Y - 6, UI_W, SCALE_H + 12, C_BG);
    lv_obj_set_style_bg_opa(s.scale_hit, LV_OPA_TRANSP, 0);
    gesture_bind(s.scale_hit, on_scale, 0);
    s.colour = C_SILENT;
    s.hz = theme_label(root, &font_value_20, C_LABEL, 0);
    s.cents = theme_label(root, &font_value_20, C_ACCENT, 0);
    s.ref = theme_label(root, &font_label_12, C_OFF, 1);
}

static void enter(struct ui_ctx *ui)
{
    tuner_start(ui->settings->a4, ui->settings->tuner_mute);
}

static void leave(struct ui_ctx *ui)
{
    (void)ui;
    tuner_stop();
}

static void tick(struct ui_ctx *ui)
{
    if (tuner_poll())
        ui->rev++;
    uint32_t now = time_ms();
    if (!ui->settings->tuner_strobe || now - s.moved_at < 33)  /* about 30 frames per second */
        return;
    float dt = (float)(now - s.moved_at) * 0.001f;
    s.moved_at = now;
    if (s.speed == 0.0f || dt > 0.2f)
        return;
    for (int r = 0; r < 2; r++) {
        float p = (float)(STROBE_P << r);
        s.phase[r] += s.speed * dt / (float)(1 << r);
        while (s.phase[r] >= p)
            s.phase[r] -= p;
        while (s.phase[r] < 0)
            s.phase[r] += p;
    }
    lv_obj_invalidate(s.strobe);
}

/* place the note and its octave as one group, centred, sharing a baseline */
static void place_note(void)
{
    lv_obj_update_layout(s.note);
    lv_obj_update_layout(s.octave);
    int wn = lv_obj_get_width(s.note), wo = lv_label_get_text(s.octave)[0] ? lv_obj_get_width(s.octave) : 0;
    int x = (UI_W - wn - (wo ? wo + 4 : 0)) / 2;
    /* cap height of the note is about 0.7 of the font size: centre the capitals in the area */
    int cap = 98, baseline = NOTE_Y0 + (NOTE_Y1 - NOTE_Y0 + cap) / 2;
    const lv_font_t *fn = &font_note_140, *fo = &font_title_40;
    lv_obj_set_pos(s.note, x, baseline - (lv_font_get_line_height(fn) - fn->base_line));
    lv_obj_set_pos(s.octave, x + wn + 4, baseline - (lv_font_get_line_height(fo) - fo->base_line));
}

static void refresh(struct ui_ctx *ui)
{
    char ref[32], hz[24], cents[24], oct[4];
    struct tuner_reading r;
    tuner_get(&r);
    top_bar_tabs(SCR_TUNER);
    int strobe = ui->settings->tuner_strobe;
    for (int i = 0; i < TICKS; i++)
        if (strobe)
            lv_obj_add_flag(s.ticks[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(s.ticks[i], LV_OBJ_FLAG_HIDDEN);
    if (strobe)
        lv_obj_clear_flag(s.strobe, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s.strobe, LV_OBJ_FLAG_HIDDEN);
    lv_snprintf(ref, sizeof ref, "A4 = %d Hz%s%s", ui->settings->a4, ui->settings->tuner_mute ? " \xC2\xB7 MUTE" : "",
                strobe ? " \xC2\xB7 STROBE" : "");
    theme_set_text(s.ref, ref);
    lv_obj_align(s.ref, LV_ALIGN_TOP_MID, 0, FOOT_Y + (UI_H - FOOT_Y - 14) / 2);

    if (!r.valid) {
        theme_set_text(s.note, "-");
        theme_set_text(s.octave, "");
        theme_set_color(s.note, C_SILENT);
        lv_obj_add_flag(s.indicator, LV_OBJ_FLAG_HIDDEN);
        theme_set_text(s.hz, "");
        theme_set_text(s.cents, "");
        place_note();
        s.speed = 0.0f;
        s.colour = C_TRACK;
        lv_obj_invalidate(s.strobe);
        return;
    }
    float c = r.cents < -50 ? -50 : r.cents > 50 ? 50 : r.cents;
    uint32_t color = (c < 0 ? -c : c) < IN_TUNE ? C_TUNED : C_ACCENT;
    theme_set_text(s.note, tuner_note_name(r.note));
    theme_set_color(s.note, color);
    lv_snprintf(oct, sizeof oct, "%d", r.octave);
    theme_set_text(s.octave, oct);
    place_note();

    s.speed = r.cents * STROBE_K;           /* sharp: to the right */
    s.colour = color;
    if (strobe)
        lv_obj_add_flag(s.indicator, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(s.indicator, LV_OBJ_FLAG_HIDDEN);
    theme_set_bg(s.indicator, color);
    lv_obj_set_x(s.indicator, SCALE_X + (int)((c + 50.0f) * SCALE_W / 100.0f) - BAR_W / 2);

    int tenths = (int)(r.hz * 10.0f + 0.5f);
    lv_snprintf(hz, sizeof hz, "%d.%d Hz", tenths / 10, tenths % 10);
    int ci = (int)(c >= 0 ? c + 0.5f : c - 0.5f);
    lv_snprintf(cents, sizeof cents, "%s%d \xC2\xA2", ci > 0 ? "+" : "", ci);
    theme_set_text(s.hz, hz);
    theme_set_text(s.cents, cents);
    theme_set_color(s.cents, color);
    lv_obj_set_pos(s.hz, SCALE_X, FOOT_Y + (UI_H - FOOT_Y - 24) / 2);
    lv_obj_align(s.cents, LV_ALIGN_TOP_RIGHT, -SCALE_X, FOOT_Y + (UI_H - FOOT_Y - 24) / 2);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    if (ev->type != IN_PRESS)
        return 1;
    uint32_t b = 1u << ev->id;
    (void)ui;
    if (b & (BTN_FS_A | BTN_FS_B | BTN_FS_C | BTN_FS_D | BTN_EXIT | BTN_MENU | BTN_GLOBAL))
        ui_back();
    return 1;
}

const struct screen screen_tuner = { create, enter, refresh, input, leave, tick };
