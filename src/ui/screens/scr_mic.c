/*
 * MIC (from the edit screen of a Cab or User IR block, waveod's engine): where the virtual mic stands in front
 * of the speaker (engine/cabmic.h). Not in the design; built from its parts.
 *
 *   left:   the speaker from the front (surround, cone, dust cap) and the mic on it: its distance from the
 *           centre is POSITION (the dust cap's centre .. the edge of the cone);
 *   right:  from the side (the grille, the cone behind it): the mic at its DISTANCE from the grille, at the
 *           height of its position, tilted by its ANGLE;
 *           Its corner names the mic type (ORIGINAL = the mic the IR was captured with), drawn as that mic;
 *   below:  the three values, and the change in the response (+-12 dB, 50 Hz .. 16 kHz) against the IR as
 *           captured, which is where the mic stands at 0 / 0 / 0 (the dust cap's edge, close, on axis) with
 *           the type ORIGINAL.
 *
 * Controls: knobs 1-3 = position / distance / angle, MENU turns = mic type, EFX = the mic back to where the IR
 * was captured (the type stays), MENU press or EXIT = back.
 * Touch: tap or drag on the front view = position (how far from the centre); drag the mic in the side view =
 * distance (sideways) and position (up / down); tap the type = the next one; hold either view = back to where
 * the IR was captured; ‹ = back.
 */
#include "dsp/fmath.h"
#include "engine/cabmic.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define VIEW_Y      (UI_TOPBAR_H + 4)
#define VIEW_H      144
#define FRONT_X     UI_PAD
#define FRONT_W     180
#define SIDE_X      (FRONT_X + FRONT_W + 10)
#define SIDE_W      (UI_W - UI_PAD - SIDE_X)
#define TEXT_Y      (VIEW_Y + VIEW_H + 2)
#define CURVE_Y     (TEXT_Y + 24)
#define CURVE_H     (UI_H - CURVE_Y - 4)
#define R_SURROUND  70
#define R_CONE      62
#define R_CAP       20
#define GRILLE_DX   44                          /* the grille's x in the side view */
#define DIST_PX0    8                           /* mic tip from the grille at the first distance step */
#define DIST_PX     (SIDE_W - GRILLE_DX - 64)   /* ... and the extra at the last */
#define MIC_LEN     52
#define POINTS      64
#define DB_RANGE    12.0f

static struct {
    lv_obj_t *front, *side, *curve, *type;
    lv_obj_t *name[3], *value[3];
    float knob[CABMIC_PARAMS];                  /* position, distance, angle, type */
    int16_t y[POINTS];
} s SDRAM_BSS;

static const char *const names[3] = { "POSI\xC3\x87\xC3\x83O", "DIST\xC3\x82NCIA", "\xC3\x82NGULO" };

static int slot_of(const struct ui_ctx *ui)
{
    return ui->sel < ui->topo.count ? ui->topo.node[ui->sel].slot : -1;
}

/* index of the first mic parameter of the slot, or -1 */
static int first_param(const struct ui_ctx *ui, int slot)
{
    return slot < 0 ? -1 : rig_mic_first(ui->rig, slot);
}

static int type_of(void)
{
    float knobs[16] = { 0 };
    knobs[CABMIC_KNOB_TYPE] = s.knob[3];
    return cabmic_type(knobs);
}

/* radius on the front view (and height on the side view) of a position step: the cap's edge at step 1 */
static float step_radius(float st)
{
    return st <= 1.0f ? st * R_CAP : R_CAP + (st - 1.0f) * 0.5f * (R_CONE - R_CAP);
}

static float radius_step(float r)
{
    float st = r <= R_CAP ? r / R_CAP : 1.0f + (r - R_CAP) / (0.5f * (R_CONE - R_CAP));
    return st < 0 ? 0 : st > CABMIC_POSITIONS - 1 ? (float)(CABMIC_POSITIONS - 1) : st;
}

static float dist_px(float sd)
{
    return DIST_PX0 + sd / (CABMIC_DISTANCES - 1) * DIST_PX;
}

static void set_param(struct ui_ctx *ui, int i, float v)
{
    int slot = slot_of(ui), k = first_param(ui, slot);
    if (k < 0)
        return;
    rig_param_set(ui->rig, slot, k + i, v);
    ui_changed();
}

/* the mic back to where the IR was captured; its type stays */
static void neutral(struct ui_ctx *ui)
{
    for (int i = 0; i < 3; i++)
        set_param(ui, i, 0.0f);
}

static void on_type(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        set_param(ui, 3, (float)((type_of() + 1) % (CABMIC_TYPES + 1)));
}

/* ---------------------------------------------------------------- touch */

static void on_front(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_LONG) {
        neutral(ui);
        return;
    }
    if (g->kind != GESTURE_TAP && g->kind != GESTURE_DRAG)
        return;
    float dx = (float)(g->x - (FRONT_X + FRONT_W / 2)), dy = (float)(g->y - (VIEW_Y + VIEW_H / 2));
    float r = (float)sqrt((double)(dx * dx + dy * dy));
    set_param(ui, 0, cabmic_knob(radius_step(r), CABMIC_POSITIONS));
}

static void on_side(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_LONG) {
        neutral(ui);
        return;
    }
    if (g->kind != GESTURE_TAP && g->kind != GESTURE_DRAG)
        return;
    float sd = ((float)(g->x - (SIDE_X + GRILLE_DX)) - DIST_PX0) / DIST_PX * (CABMIC_DISTANCES - 1);
    sd = sd < 0 ? 0 : sd > CABMIC_DISTANCES - 1 ? (float)(CABMIC_DISTANCES - 1) : sd;
    float dy = (float)(g->y - (VIEW_Y + VIEW_H / 2));
    set_param(ui, 1, cabmic_knob(sd, CABMIC_DISTANCES));
    set_param(ui, 0, cabmic_knob(radius_step(dy < 0 ? -dy : dy), CABMIC_POSITIONS));
}

/* ---------------------------------------------------------------- drawing */

static void disc(lv_layer_t *layer, int cx, int cy, int r, uint32_t fill, uint32_t border, int bw)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = rgb(fill);
    d.border_color = rgb(border);
    d.border_width = bw;
    lv_area_t a = { cx - r, cy - r, cx + r, cy + r };
    lv_draw_rect(layer, &d, &a);
}

static void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, uint32_t colour)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = w;
    d.color = rgb(colour);
    d.round_start = d.round_end = 1;
    d.p1.x = x1, d.p1.y = y1, d.p2.x = x2, d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void front_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.front, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    disc(layer, cx, cy, R_SURROUND, C_LINE, C_WIRE, 2);
    disc(layer, cx, cy, R_CONE, C_TRACK, C_BORDER_OFF, 1);
    for (int r = R_CONE - 14; r > R_CAP; r -= 14)          /* the cone's rings, outer first */
        disc(layer, cx, cy, r, C_TRACK, C_LINE, 1);
    disc(layer, cx, cy, R_CAP, C_BORDER_OFF, C_WIRE, 1);
    float st = cabmic_step(s.knob[0], CABMIC_POSITIONS);
    int r = (int)(step_radius(st) + 0.5f);
    line(layer, cx, cy, cx + r, cy, 1, C_OFF);
    disc(layer, cx + r, cy, 9, C_ACCENT, C_BG, 2);
    disc(layer, cx + r, cy, 3, C_BG, C_BG, 0);
}

static void side_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.side, &a);
    int gx = a.x1 + GRILLE_DX, cy = (a.y1 + a.y2) / 2;
    /* the cone behind the grille, the dust cap, the grille */
    line(layer, gx - 4, cy - R_SURROUND, gx - 30, cy - R_CAP, 2, C_WIRE);
    line(layer, gx - 4, cy + R_SURROUND, gx - 30, cy + R_CAP, 2, C_WIRE);
    line(layer, gx - 30, cy - R_CAP, gx - 30, cy + R_CAP, 2, C_WIRE);
    line(layer, gx - 30, cy - R_CAP, gx - 22, cy, 2, C_BORDER_OFF);
    line(layer, gx - 22, cy, gx - 30, cy + R_CAP, 2, C_BORDER_OFF);
    for (int y = a.y1 + 4; y < a.y2 - 4; y += 6)
        line(layer, gx, y, gx, y + 3, 2, C_OFF);
    /* the mic: its tip at (distance, height), the body away from the speaker, tilted towards the axis */
    float st = cabmic_step(s.knob[0], CABMIC_POSITIONS), sd = cabmic_step(s.knob[1], CABMIC_DISTANCES);
    int tx = gx + (int)(dist_px(sd) + 0.5f), ty = cy - (int)(step_radius(st) + 0.5f);
    float an = s.knob[2] * 3.14159265f / 180.0f;
    int bx = tx + (int)(MIC_LEN * cosf(an) + 0.5f), by = ty - (int)(MIC_LEN * sinf(an) + 0.5f);
    line(layer, tx, cy, gx, cy, 1, C_LINE);                    /* the speaker's axis */
    float ux = cosf(an), uy = -sinf(an);
    if (type_of() == 1) {
        /* the 57: a long slim body, the grille a little wider, a dark band where it screws on */
        int ex = tx + (int)((MIC_LEN + 8) * ux), ey = ty + (int)((MIC_LEN + 8) * uy);
        int gx2 = tx + (int)(18 * ux + 0.5f), gy2 = ty + (int)(18 * uy + 0.5f);
        line(layer, tx, ty, ex, ey, 9, C_LABEL);
        line(layer, tx, ty, gx2, gy2, 12, C_TEXT);
        line(layer, gx2, gy2, gx2 + (int)(3 * ux), gy2 + (int)(3 * uy), 10, C_BG);
        line(layer, tx, ty, tx + (int)(4 * ux), ty + (int)(4 * uy), 12, C_ACCENT);
        return;
    }
    line(layer, tx, ty, bx, by, 12, C_LABEL);
    line(layer, tx, ty, tx + (int)(10 * ux), ty + (int)(10 * uy), 14, C_ACCENT);
}

static void curve_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.curve, &a);
    int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    line(layer, a.x1, a.y1 + (h - 1) / 2, a.x2, a.y1 + (h - 1) / 2, 1, C_WIRE);   /* 0 dB */
    for (int dec = 1; dec <= 2; dec++) {                                          /* 500 Hz, 5 kHz */
        int x = a.x1 + (w - 1) * dec * 2 / 5;
        line(layer, x, a.y1, x, a.y2, 1, C_LINE);
    }
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = 2;
    d.round_start = d.round_end = 1;
    d.color = rgb(C_ACCENT);
    for (int i = 1; i < POINTS; i++) {
        d.p1.x = a.x1 + (w - 1) * (i - 1) / (POINTS - 1);
        d.p2.x = a.x1 + (w - 1) * i / (POINTS - 1);
        d.p1.y = a.y1 + s.y[i - 1];
        d.p2.y = a.y1 + s.y[i];
        lv_draw_line(layer, &d);
    }
}

/* the response change on POINTS frequencies, 50 Hz .. 16 kHz (2.5 decades) */
static void curve_update(void)
{
    float knobs[16] = { 0 }, db[CABMIC_BANDS];
    knobs[CABMIC_KNOB_POSITION] = s.knob[0];
    knobs[CABMIC_KNOB_DISTANCE] = s.knob[1];
    knobs[CABMIC_KNOB_ANGLE] = s.knob[2];
    knobs[CABMIC_KNOB_TYPE] = s.knob[3];
    cabmic_curve(knobs, db);
    for (int i = 0; i < POINTS; i++) {
        float hz = 50.0f * fm_pow10(2.5f * (float)i / (float)(POINTS - 1));
        float v = cabmic_db_at(db, hz);
        v = v > DB_RANGE ? DB_RANGE : v < -DB_RANGE ? -DB_RANGE : v;
        s.y[i] = (int16_t)((CURVE_H - 1) * 0.5f * (1.0f - v / DB_RANGE) + 0.5f);
    }
}

static const char *position_word(float st)
{
    return st < 0.5f ? "CENTRO" : st < 1.5f ? "CALOTA" : st < 2.5f ? "CONE" : "BORDA";
}

static const char *distance_word(float sd)
{
    return sd < 0.5f ? "ENCOSTADO" : sd < 1.5f ? "PERTO" : sd < 3.0f ? "M\xC3\x89" "DIO" : "LONGE";
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    s.front = theme_box(root, FRONT_X, VIEW_Y, FRONT_W, VIEW_H, C_BG);
    lv_obj_add_event_cb(s.front, front_draw, LV_EVENT_DRAW_MAIN, ui);
    gesture_bind(s.front, on_front, 0);
    s.side = theme_box(root, SIDE_X, VIEW_Y, SIDE_W, VIEW_H, C_BG);
    lv_obj_add_event_cb(s.side, side_draw, LV_EVENT_DRAW_MAIN, ui);
    gesture_bind(s.side, on_side, 0);
    s.type = theme_label(root, &font_tag_12, C_LABEL, 2);
    lv_obj_set_pos(s.type, SIDE_X + SIDE_W - 110, VIEW_Y + 4);
    lv_obj_set_size(s.type, 106, 18);
    lv_obj_set_style_text_align(s.type, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(s.type, LV_OBJ_FLAG_CLICKABLE);
    gesture_bind(s.type, on_type, 0);
    theme_box(root, 0, TEXT_Y - 1, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++) {
        s.name[i] = theme_label(root, &font_label_12, C_LABEL, 1);
        theme_set_text(s.name[i], names[i]);
        lv_obj_set_pos(s.name[i], UI_PAD + i * (UI_W - 2 * UI_PAD) / 3, TEXT_Y + 5);
        s.value[i] = theme_label(root, &font_body_14, C_TEXT, 0);
    }
    s.curve = theme_box(root, UI_PAD, CURVE_Y, UI_W - 2 * UI_PAD, CURVE_H, C_BG);
    lv_obj_add_event_cb(s.curve, curve_draw, LV_EVENT_DRAW_MAIN, ui);
}

static void refresh(struct ui_ctx *ui)
{
    int slot = slot_of(ui), k = first_param(ui, slot);
    char title[48], name[32], text[32];
    ui_slot_name(ui, slot, name, sizeof name);
    lv_snprintf(title, sizeof title, "MIC \xC2\xB7 %s", name);
    top_bar_sub(title);
    top_bar_scene();
    for (int i = 0; i < CABMIC_PARAMS; i++) {
        struct rig_param p;
        s.knob[i] = k >= 0 && rig_param(ui->rig, slot, k + i, &p) == 0 ? p.value : 0.0f;
    }
    int t = type_of();
    theme_set_text(s.type, t ? cabmic_type_names[t - 1] : "Original");
    theme_set_color(s.type, t ? C_ACCENT : C_LABEL);
    float st = cabmic_step(s.knob[0], CABMIC_POSITIONS), sd = cabmic_step(s.knob[1], CABMIC_DISTANCES);
    int pv = (int)(s.knob[0] + (s.knob[0] < 0 ? -0.5f : 0.5f)), dv = (int)(s.knob[1] + (s.knob[1] < 0 ? -0.5f : 0.5f));
    lv_snprintf(text, sizeof text, "%s %s%d", position_word(st), pv > 0 ? "+" : "", pv);
    theme_set_text(s.value[0], text);
    lv_snprintf(text, sizeof text, "%s %s%d", distance_word(sd), dv > 0 ? "+" : "", dv);
    theme_set_text(s.value[1], text);
    lv_snprintf(text, sizeof text, "%d\xC2\xB0", (int)(s.knob[2] + 0.5f));
    theme_set_text(s.value[2], text);
    for (int i = 0; i < 3; i++) {
        lv_obj_update_layout(s.name[i]);
        lv_obj_set_pos(s.value[i], UI_PAD + i * (UI_W - 2 * UI_PAD) / 3 + lv_obj_get_width(s.name[i]) + 8, TEXT_Y + 2);
    }
    curve_update();
    lv_obj_invalidate(s.front);
    lv_obj_invalidate(s.side);
    lv_obj_invalidate(s.curve);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int slot = slot_of(ui), k = first_param(ui, slot);
    for (int i = 0; i < 3; i++)
        if (ev->type == IN_TURN && ev->id == ENC_KNOB1 + i) {
            if (k >= 0) {
                rig_param_nudge(ui->rig, slot, k + i, ev->fast);
                ui_changed();
            }
            return 1;
        }
    if (ev->type == IN_TURN && ev->id == ENC_MENU) {
        if (k >= 0) {
            rig_param_nudge(ui->rig, slot, k + 3, ev->delta);
            ui_changed();
        }
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {
        neutral(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU) || ui_pressed(ev, BTN_EXIT)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_mic = { create, 0, refresh, input, 0, 0 };
