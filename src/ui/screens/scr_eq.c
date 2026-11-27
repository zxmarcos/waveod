/*
 * Global EQ (from the settings): the original's post-chain EQ, shared with the stock firmware (record 0).
 * A head with the stage's name and switches, a strip with the nine stages, the response on a graph (a grid of
 * frequencies and dB, the area under the curve filled, one handle per stage at its frequency and gain), and the
 * stage's parameters in cells like the edit screen.
 *
 * Behind the curve, the spectrum of what is playing (FFT, on by default): the EQ's input as a grey line, its
 * output as a grey area, in dB of full scale (0 at the top, -90 at the bottom), rising at once and falling
 * 30 dB per second like the edit screen's analyser.
 *
 * FLAT (hold it): every gain to 0 dB, both cuts off, the level to 50; the frequencies, Qs and switches stay. It
 * then reads VOLTAR, which puts the EQ back as it was (until the next change).
 *
 * Controls: MENU turns = stage, knobs 1-3 = frequency / Q / gain (the level on the last stage), MENU press =
 * stage on / off, EFX = whole EQ on / off, CTRL = FFT on / off, EXIT or GLOBAL = back.
 * Touch: tap a stage or its handle = select it, hold a stage = on / off; drag a handle = frequency sideways, gain
 * up / down (a stage that is off comes on); tap FFT / EQ / ON-OFF in the head, hold FLAT; drag a cell sideways =
 * turn its knob; ‹ = back. Changes are heard at once and written to the flash when the screen is left.
 */
#include "dsp/fmath.h"
#include "dsp/global_eq.h"
#include "dsp/pitch.h"
#include "dsp/spectrum.h"
#include "engine/scope.h"
#include "model/globals.h"
#include "platform/time.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define HEAD_Y    UI_TOPBAR_H
#define HEAD_H    30
#define STRIP_Y   (HEAD_Y + HEAD_H)
#define STRIP_H   22
#define GRAPH_Y   (STRIP_Y + STRIP_H + 2)
#define GRAPH_H   90
#define GRAPH_W   (UI_W - 2 * UI_PAD)
#define CELLS_Y   (GRAPH_Y + GRAPH_H + 2)
#define CELLS_H   (UI_H - CELLS_Y)
#define PILL_H    22
#define PILL_GAP  6
#define POINTS    114
#define DB_RANGE  18.0f                     /* the graph shows +-18 dB */
#define DRAG_PX   12                        /* finger travel per step (cells) */
#define HANDLE    18                        /* handle diameter */
#define MARGIN    (HANDLE / 2 + 1)          /* 20 Hz and 20 kHz this far inside, so their handles show whole */
#define AXIS_W    (GRAPH_W - 2 * MARGIN)
#define HIT_PX    28                        /* a tap this close to a handle selects it */
#define STAGES    9
#define BANDS     8                         /* stages with a handle (not the level) */

/* the spectrum */
#define SPEC_TOP    0.0f
#define SPEC_BOT    (-90.0f)
#define FRAME_MS    40
#define HOP         1024
#define CHUNK       512
#define FALL_DB_S   30.0f
#define RISE        0.7f
#define QUIET_MS    150

struct stage {
    const char *tag, *mark;                 /* tag 0: the name (the level) */
    uint16_t name;                          /* message id */
    int8_t on, f, q, g;                     /* parameter indexes (enum eq_param), -1 = none */
};

static const struct stage stages[STAGES] = {
    { "L.CUT", "L",  MSG_EQ_LOW_CUT,    EQ_LOCUT_ON,   EQ_LOCUT_F,   EQ_LOCUT_Q,   -1 },
    { "L.SHF", "LS", MSG_EQ_LOW_SHELF,  EQ_LOSHELF_ON, EQ_LOSHELF_F, EQ_LOSHELF_Q, EQ_LOSHELF_G },
    { "B1",    "1",  MSG_EQ_BAND_1,     EQ_B1_ON,      EQ_B1_F,      EQ_B1_Q,      EQ_B1_G },
    { "B2",    "2",  MSG_EQ_BAND_2,     EQ_B2_ON,      EQ_B2_F,      EQ_B2_Q,      EQ_B2_G },
    { "B3",    "3",  MSG_EQ_BAND_3,     EQ_B3_ON,      EQ_B3_F,      EQ_B3_Q,      EQ_B3_G },
    { "B4",    "4",  MSG_EQ_BAND_4,     EQ_B4_ON,      EQ_B4_F,      EQ_B4_Q,      EQ_B4_G },
    { "H.SHF", "HS", MSG_EQ_HIGH_SHELF, EQ_HISHELF_ON, EQ_HISHELF_F, EQ_HISHELF_Q, EQ_HISHELF_G },
    { "H.CUT", "H",  MSG_EQ_HIGH_CUT,   EQ_HICUT_ON,   EQ_HICUT_F,   EQ_HICUT_Q,   -1 },
    { 0,       "",   MSG_EQ_LEVEL,      -1,            EQ_LEVEL,     -1,           -1 },
};

/* the grid: frequencies (the labelled ones get text) and dB */
static const struct { uint16_t hz; const char *text; } grid_hz[] = {
    { 50, 0 }, { 100, "100" }, { 200, 0 }, { 500, 0 }, { 1000, "1k" }, { 2000, 0 }, { 5000, 0 }, { 10000, "10k" },
};
static const struct { int8_t db; const char *text; } grid_db[] = {
    { 12, "+12" }, { 6, 0 }, { -6, 0 }, { -12, "\xE2\x80\x93" "12" },
};
#define GRID_HZ (int)(sizeof grid_hz / sizeof grid_hz[0])
#define GRID_DB (int)(sizeof grid_db / sizeof grid_db[0])

static struct {
    int sel;
    lv_obj_t *name, *eq_pill, *eq_text, *on_pill, *on_text, *fft_pill, *fft_text, *flat_pill, *flat_text;
    lv_obj_t *chips[STAGES], *chip_text[STAGES], *chip_bar[STAGES];
    lv_obj_t *graph, *canvas;
    struct param_cell cells[3];
    int drag_start;
    int drag_stage, drag_x, drag_y;         /* the handle dragged, where it was when the drag began */
    int16_t y[POINTS];                      /* the curve, pixel rows in the graph */
    int16_t hx[BANDS], hy[BANDS];           /* the handles' centres in the graph */
    float undo[EQ_PARAMS];                  /* the EQ before FLAT */
    int undo_ok;
    int fft;                                /* the spectrum is shown */
    uint8_t row_grid[GRAPH_H], col_grid[GRAPH_W];
    /* the spectrum: rings of the last SPEC_N samples, the curves shown */
    uint32_t head, last_frame, last_data;
    int fresh;
    float hist_b[SPEC_N], hist_a[SPEC_N], lin[SPEC_N], in_b[CHUNK], in_a[CHUNK];
    float new_b[AXIS_W], new_a[AXIS_W], show_b[AXIS_W], show_a[AXIS_W];
    struct spectrum sp;
    uint32_t px[GRAPH_W * GRAPH_H] __attribute__((aligned(8)));
} s SDRAM_BSS;

static int knob_param(int stage, int knob)
{
    const struct stage *st = &stages[stage];
    return knob == 0 ? st->f : knob == 1 ? st->q : st->g;
}

static int stage_on(const struct globals *g, int stage)
{
    return stages[stage].on < 0 || g->eq[stages[stage].on] != 0.0f;
}

static void changed(struct ui_ctx *ui)
{
    globals_apply(ui->globals);
    ui->rev++;
}

static void toggle_stage(struct ui_ctx *ui, int stage)
{
    int k = stages[stage].on;
    if (k < 0)
        return;
    float *v = &ui->globals->eq[k];
    *v = *v != 0.0f ? 0.0f : 1.0f;
    s.undo_ok = 0;
    changed(ui);
}

static void toggle_eq(struct ui_ctx *ui)
{
    ui->globals->eq_on = !ui->globals->eq_on;
    changed(ui);
}

static void nudge(struct ui_ctx *ui, int knob, int steps)
{
    int k = knob_param(s.sel, knob);
    if (k < 0 || !steps)
        return;
    /* the high shelf / high cut frequencies have a negative step in the original's table: same direction here */
    globals_eq_nudge(ui->globals, k, steps);
    s.undo_ok = 0;
    changed(ui);
}

/* parameter k to v, within its range (whole Hz, whole dB, like the knobs' steps) */
static void set_param(struct ui_ctx *ui, int k, float v)
{
    const struct eq_param_range *r = &eq_param_ranges[k];
    v = (float)(int)(v + (v >= 0 ? 0.5f : -0.5f));
    v = v < r->min ? r->min : v > r->max ? r->max : v;
    if (ui->globals->eq[k] != v) {
        ui->globals->eq[k] = v;
        s.undo_ok = 0;
        changed(ui);
    }
}

/* FLAT: no gain, no cut, unity level; VOLTAR: as it was */
static void flatten(struct ui_ctx *ui)
{
    float *eq = ui->globals->eq;
    memcpy(s.undo, eq, sizeof s.undo);
    static const int8_t gains[] = { EQ_LOSHELF_G, EQ_B1_G, EQ_B2_G, EQ_B3_G, EQ_B4_G, EQ_HISHELF_G };
    for (unsigned i = 0; i < sizeof gains; i++)
        eq[gains[i]] = 0.0f;
    eq[EQ_LOCUT_F] = eq_param_ranges[EQ_LOCUT_F].min;      /* 19 Hz: OFF */
    eq[EQ_HICUT_F] = eq_param_ranges[EQ_HICUT_F].max;      /* 20001 Hz: OFF */
    eq[EQ_LEVEL] = 50.0f;
    s.undo_ok = 1;
    changed(ui);
    ui_toast(T(MSG_EQ_FLATTENED));
}

static void restore(struct ui_ctx *ui)
{
    memcpy(ui->globals->eq, s.undo, sizeof s.undo);
    s.undo_ok = 0;
    changed(ui);
}

/* ---------------------------------------------------------------- text */

static void param_text(const struct globals *g, int k, char *out, unsigned size)
{
    float v = g->eq[k];
    if (k == EQ_LEVEL) {
        lv_snprintf(out, size, "%d", (int)(v + 0.5f));
    } else if (k == EQ_LOCUT_F && v <= 19.0f) {
        lv_snprintf(out, size, "OFF");
    } else if (k == EQ_HICUT_F && v >= 20001.0f) {
        lv_snprintf(out, size, "OFF");
    } else if (k == stages[s.sel].f) {
        int hz = (int)(v + 0.5f);
        if (hz >= 1000)
            lv_snprintf(out, size, "%d.%d kHz", hz / 1000, hz % 1000 / 100);
        else
            lv_snprintf(out, size, "%d Hz", hz);
    } else if (k == stages[s.sel].q) {
        int t = (int)(v * 100.0f + 0.5f);
        lv_snprintf(out, size, "%d.%02d", t / 100, t % 100);
    } else {
        int db = (int)(v + (v >= 0 ? 0.5f : -0.5f));
        lv_snprintf(out, size, "%s%d dB", db > 0 ? "+" : "", db);
    }
}

static float frac_of(int k, float v)
{
    const struct eq_param_range *r = &eq_param_ranges[k];
    if (r->max - r->min > 1000.0f)          /* frequencies on a log scale: 20 Hz .. 20 kHz */
        return pitch_log2(v / 20.0f) / pitch_log2(1000.0f);
    return (v - r->min) / (r->max - r->min);
}

/* ---------------------------------------------------------------- graph geometry (20 Hz .. 20 kHz, +-18 dB) */

static int x_of_hz(float hz)
{
    float f = pitch_log2(hz / 20.0f) / pitch_log2(1000.0f);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return MARGIN + (int)((AXIS_W - 1) * f + 0.5f);
}

static float hz_of_x(int x)
{
    return 20.0f * fm_pow10(3.0f * (float)(x - MARGIN) / (float)(AXIS_W - 1));
}

static int y_of_db(float db)
{
    db = db > DB_RANGE ? DB_RANGE : db < -DB_RANGE ? -DB_RANGE : db;
    return (int)((GRAPH_H - 1) * 0.5f * (1.0f - db / DB_RANGE) + 0.5f);
}

static float db_of_y(int y)
{
    return DB_RANGE * (1.0f - 2.0f * (float)y / (float)(GRAPH_H - 1));
}

static int y_of_spec(float db)
{
    float t = (SPEC_TOP - db) / (SPEC_TOP - SPEC_BOT);
    int y = (int)(t * (float)(GRAPH_H - 1) + 0.5f);
    return y < 0 ? 0 : y > GRAPH_H - 1 ? GRAPH_H - 1 : y;
}

/* ---------------------------------------------------------------- the background: grid, spectrum, EQ area */

static uint32_t mix(uint32_t c1, uint32_t c2, float t)     /* t of c1 over c2 */
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float v = (float)((c1 >> sh) & 0xff) * t + (float)((c2 >> sh) & 0xff) * (1.0f - t);
        out |= (uint32_t)(v + 0.5f) << sh;
    }
    return 0xff000000u | out;
}

/* Column by column into the canvas's pixels: each pixel's colour from what it is (spectrum area, grid line, EQ
   area), then the EQ input's line and the 0 dB line on top. */
static void render(const struct globals *g)
{
    uint32_t eq_col = g->eq_on ? C_ACCENT : C_OFF;
    float eq_t = g->eq_on ? 0.20f : 0.10f;
    uint32_t base[2][2], col[2][2][2];          /* [spectrum][grid], then [eq area] */
    base[0][0] = 0xff000000u | C_BG;
    base[0][1] = 0xff000000u | C_LINE;
    base[1][0] = mix(C_LABEL, C_BG, 0.16f);
    base[1][1] = mix(C_LABEL, C_LINE, 0.20f);
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++) {
            col[a][b][0] = base[a][b];
            col[a][b][1] = mix(eq_col, base[a][b] & 0xffffff, eq_t);
        }
    uint32_t pre = mix(C_LABEL, C_BG, 0.55f), zero_c = 0xff000000u | C_WIRE;
    int zero = y_of_db(0), pb = -1;
    for (int x = 0; x < GRAPH_W; x++) {
        int c = x < MARGIN ? 0 : x - MARGIN >= AXIS_W ? AXIS_W - 1 : x - MARGIN;
        int ya = s.fft && s.show_a[c] > SPEC_BOT ? y_of_spec(s.show_a[c]) : GRAPH_H;
        int yb = s.fft && s.show_b[c] > SPEC_BOT ? y_of_spec(s.show_b[c]) : -1;
        int i = x * (POINTS - 1) / (GRAPH_W - 1), r = x * (POINTS - 1) % (GRAPH_W - 1);
        int ye = i + 1 < POINTS ? s.y[i] + (s.y[i + 1] - s.y[i]) * r / (GRAPH_W - 1) : s.y[i];
        int e0 = ye < zero ? ye : zero, e1 = ye < zero ? zero : ye;
        uint32_t *p = s.px + x;
        int cg = s.col_grid[x];
        for (int y = 0; y < GRAPH_H; y++)
            p[y * GRAPH_W] = col[y >= ya][cg || s.row_grid[y]][y >= e0 && y <= e1 && ye != zero];
        if (yb >= 0) {                          /* the EQ's input: a line from the last column's height */
            int y0 = pb >= 0 && pb < yb ? pb : yb, y1 = pb >= 0 && pb > yb ? pb : yb;
            for (int y = y0; y <= y1; y++)
                p[y * GRAPH_W] = pre;
        }
        pb = yb;
        p[zero * GRAPH_W] = zero_c;
    }
    lv_obj_invalidate(s.canvas);
}

static void spectrum_clear(void)
{
    memset(s.hist_b, 0, sizeof s.hist_b);
    memset(s.hist_a, 0, sizeof s.hist_a);
    for (int c = 0; c < AXIS_W; c++)
        s.show_a[c] = s.show_b[c] = SPEC_FLOOR_DB;
    s.fresh = 0;
    s.last_data = time_ms();
}

static void set_fft(struct ui_ctx *ui, int on)
{
    s.fft = on;
    scope_watch(0, on ? SCOPE_EQ : SCOPE_OFF);
    spectrum_clear();
    render(ui->globals);
    ui->rev++;
}

static void graph_update(const struct globals *g)
{
    static float drawn[EQ_PARAMS];
    static int valid, drawn_on;
    if (!valid || drawn_on != g->eq_on || memcmp(drawn, g->eq, sizeof drawn) != 0) {
        memcpy(drawn, g->eq, sizeof drawn);
        drawn_on = g->eq_on;
        valid = 1;
        for (int i = 0; i < POINTS; i++)
            s.y[i] = (int16_t)y_of_db(global_eq_response_db(g->eq, hz_of_x(MARGIN + (AXIS_W - 1) * i / (POINTS - 1))));
        for (int b = 0; b < BANDS; b++) {
            const struct stage *st = &stages[b];
            float hz = g->eq[st->f];
            s.hx[b] = (int16_t)x_of_hz(hz);
            /* a stage with gain sits at its gain; a cut on the curve, where its slope is */
            s.hy[b] = (int16_t)(st->g >= 0 ? y_of_db(g->eq[st->g]) : y_of_db(global_eq_response_db(g->eq, hz)));
        }
        render(g);
    }
    lv_obj_invalidate(s.graph);             /* the colours follow the switches and the selection */
}

static void text_at(lv_layer_t *layer, const char *text, int x, int y, uint32_t colour, lv_text_align_t align, int w)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &font_tag_12;
    d.color = rgb(colour);
    d.text = text;
    d.align = align;
    lv_area_t a = { x, y, x + w - 1, y + 14 };
    lv_draw_label(layer, &d, &a);
}

/* over the canvas: the grid's labels, the curve, the level, the handles */
static void graph_draw(lv_event_t *e)
{
    struct ui_ctx *ui = lv_event_get_user_data(e);
    const struct globals *g = ui->globals;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.graph, &a);
    int on = g->eq_on;

    for (int i = 0; i < GRID_HZ; i++)
        if (grid_hz[i].text)
            text_at(layer, grid_hz[i].text, a.x1 + x_of_hz(grid_hz[i].hz) + 3, a.y2 - 13, C_DIM, LV_TEXT_ALIGN_LEFT, 30);
    for (int i = 0; i < GRID_DB; i++)
        if (grid_db[i].text)
            text_at(layer, grid_db[i].text, a.x1 + 3, a.y1 + y_of_db(grid_db[i].db) - 13, C_DIM, LV_TEXT_ALIGN_LEFT, 30);

    /* the curve, from edge to edge */
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = rgb(on ? C_ACCENT : C_OFF);
    d.width = 2;
    d.round_start = d.round_end = 1;
    for (int i = 1; i < POINTS; i++) {
        d.p1.x = a.x1 + (GRAPH_W - 1) * (i - 1) / (POINTS - 1);
        d.p2.x = a.x1 + (GRAPH_W - 1) * i / (POINTS - 1);
        d.p1.y = a.y1 + s.y[i - 1];
        d.p2.y = a.y1 + s.y[i];
        lv_draw_line(layer, &d);
    }

    char level[24];
    lv_snprintf(level, sizeof level, T(MSG_EQ_LEVEL_VALUE), (int)(g->eq[EQ_LEVEL] + 0.5f));
    text_at(layer, level, a.x2 - 100, a.y1 + 2, s.sel == STAGES - 1 ? C_ACCENT : C_LABEL, LV_TEXT_ALIGN_RIGHT, 97);

    /* the handles: the selected one last, on top */
    for (int n = 0; n <= BANDS; n++) {
        int b = n < BANDS ? n : s.sel;
        if (b >= BANDS || (n < BANDS && b == s.sel))
            continue;
        int cur = b == s.sel, live = stage_on(g, b);
        lv_area_t h = { a.x1 + s.hx[b] - HANDLE / 2, a.y1 + s.hy[b] - HANDLE / 2, 0, 0 };
        h.x2 = h.x1 + HANDLE - 1;
        h.y2 = h.y1 + HANDLE - 1;
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.radius = LV_RADIUS_CIRCLE;
        r.bg_color = rgb(cur ? (live && on ? C_ACCENT : C_LABEL) : live ? 0x2A2926 : C_BG);
        r.border_width = cur ? 0 : 1;
        r.border_color = rgb(live ? (on ? C_ACCENT : C_LABEL) : C_DIM);
        lv_draw_rect(layer, &r, &h);
        text_at(layer, stages[b].mark, h.x1, h.y1 + 1, cur ? C_BG : live ? C_TEXT : C_DIM, LV_TEXT_ALIGN_CENTER,
                HANDLE);
    }
}

/* ---------------------------------------------------------------- touch */

static void on_chip(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && s.sel != g->tag) {
        s.sel = g->tag;
        ui->rev++;
    } else if (g->kind == GESTURE_LONG) {
        toggle_stage(ui, g->tag);
    }
}

enum { PILL_EQ, PILL_ON, PILL_FFT };

static void on_pill(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_TAP)
        return;
    if (g->tag == PILL_ON)
        toggle_stage(ui, s.sel);
    else if (g->tag == PILL_FFT)
        set_fft(ui, !s.fft);
    else
        toggle_eq(ui);
}

static void on_flat(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_LONG && !s.undo_ok)
        flatten(ui);
    else if (g->kind == GESTURE_TAP && s.undo_ok)
        restore(ui);
    else if (g->kind == GESTURE_TAP)
        ui_toast(T(MSG_EQ_HOLD_FLAT));
}

/* the handle nearest to (x, y) in the graph, -1 = none close enough */
static int handle_at(int x, int y)
{
    int best = -1, best_d = HIT_PX * HIT_PX;
    for (int b = 0; b < BANDS; b++) {
        int dx = x - s.hx[b], dy = y - s.hy[b], d2 = dx * dx + dy * dy;
        if (d2 < best_d || (d2 == best_d && b == s.sel)) {
            best = b;
            best_d = d2;
        }
    }
    return best;
}

static void on_graph(struct ui_ctx *ui, const struct gesture *g)
{
    lv_area_t a;
    lv_obj_get_coords(s.graph, &a);
    if (g->kind == GESTURE_TAP) {
        int b = handle_at(g->x - a.x1, g->y - a.y1);
        if (b >= 0 && b != s.sel) {
            s.sel = b;
            ui->rev++;
        }
        return;
    }
    if (g->kind != GESTURE_DRAG)
        return;
    if (g->first) {                         /* the handle under the finger where it went down */
        s.drag_stage = handle_at(g->x - g->dx - a.x1, g->y - g->dy - a.y1);
        if (s.drag_stage < 0)
            return;
        s.sel = s.drag_stage;
        s.drag_x = s.hx[s.sel];
        s.drag_y = s.hy[s.sel];
        if (!stage_on(ui->globals, s.sel))
            toggle_stage(ui, s.sel);
    }
    if (s.drag_stage < 0)
        return;
    const struct stage *st = &stages[s.drag_stage];
    int x = s.drag_x + g->dx, y = s.drag_y + g->dy;
    x = x < 0 ? 0 : x > GRAPH_W - 1 ? GRAPH_W - 1 : x;
    y = y < 0 ? 0 : y > GRAPH_H - 1 ? GRAPH_H - 1 : y;
    float hz = hz_of_x(x);
    if (st->f == EQ_LOCUT_F && x <= MARGIN)
        hz = 19.0f;                         /* the far left: the low cut off, as on the stock editor */
    if (st->f == EQ_HICUT_F && x >= GRAPH_W - 1 - MARGIN)
        hz = 20001.0f;
    set_param(ui, st->f, hz);
    if (st->g >= 0)
        set_param(ui, st->g, db_of_y(y));
    ui->rev++;
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind != GESTURE_DRAG || g->vertical)
        return;
    if (g->first)
        s.drag_start = 0;
    int steps = g->dx / DRAG_PX;
    if (steps != s.drag_start) {
        nudge(ui, g->tag, steps - s.drag_start);
        s.drag_start = steps;
    }
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *pill_create(lv_obj_t *root, int x, int w, lv_obj_t **text)
{
    lv_obj_t *p = theme_box(root, x, HEAD_Y + (HEAD_H - PILL_H) / 2, w, PILL_H, C_BG);
    lv_obj_set_style_radius(p, 2, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    *text = theme_label(p, &font_tag_12, C_OFF, 2);
    lv_obj_center(*text);
    return p;
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    s.name = theme_label(root, &font_value_20, C_TEXT, 0);
    lv_obj_set_pos(s.name, UI_PAD, HEAD_Y + 3);
    /* the head's switches from the right: ON (the stage), EQ, FFT, FLAT */
    int x = UI_W - UI_PAD - 44;
    s.on_pill = pill_create(root, x, 44, &s.on_text);
    gesture_bind(s.on_pill, on_pill, PILL_ON);
    x -= PILL_GAP + 110;
    s.eq_pill = pill_create(root, x, 110, &s.eq_text);
    gesture_bind(s.eq_pill, on_pill, PILL_EQ);
    x -= PILL_GAP + 44;
    s.fft_pill = pill_create(root, x, 44, &s.fft_text);
    gesture_bind(s.fft_pill, on_pill, PILL_FFT);
    x -= PILL_GAP + 64;
    s.flat_pill = pill_create(root, x, 64, &s.flat_text);
    gesture_bind_hold(s.flat_pill, on_flat, 0, 0);

    int w = (UI_W - 2 * UI_PAD) / STAGES;
    for (int i = 0; i < STAGES; i++) {
        s.chips[i] = theme_box(root, UI_PAD + i * w, STRIP_Y, w, STRIP_H, C_BG);
        s.chip_text[i] = theme_label(s.chips[i], &font_tag_12, C_OFF, 1);
        lv_label_set_text(s.chip_text[i], stages[i].tag ? stages[i].tag : T(stages[i].name));
        lv_obj_align(s.chip_text[i], LV_ALIGN_TOP_MID, 0, 1);
        s.chip_bar[i] = theme_box(s.chips[i], 3, STRIP_H - 5, w - 6, 3, C_BORDER_OFF);
        lv_obj_set_style_outline_color(s.chip_bar[i], rgb(C_TEXT), 0);
        lv_obj_set_style_outline_pad(s.chip_bar[i], 1, 0);
        gesture_bind_hold(s.chips[i], on_chip, i, s.chip_bar[i]);
    }

    /* the graph: the canvas (grid, spectrum, EQ area), the rest drawn over it */
    s.graph = theme_box(root, UI_PAD, GRAPH_Y, GRAPH_W, GRAPH_H, C_BG);
    s.canvas = lv_canvas_create(s.graph);
    lv_canvas_set_buffer(s.canvas, s.px, GRAPH_W, GRAPH_H, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_set_pos(s.canvas, 0, 0);
    lv_obj_remove_flag(s.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.graph, graph_draw, LV_EVENT_DRAW_POST, ui);
    gesture_bind(s.graph, on_graph, 0);
    s.drag_stage = -1;
    for (int i = 0; i < GRID_HZ; i++)
        s.col_grid[x_of_hz(grid_hz[i].hz)] = 1;
    for (int i = 0; i < GRID_DB; i++)
        s.row_grid[y_of_db(grid_db[i].db)] = 1;
    spectrum_init(&s.sp, AXIS_W, 20.0f, 20000.0f, 44100.0f);
    spectrum_clear();
    s.fft = 1;

    theme_box(root, 0, CELLS_Y, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.cells[i], root, i * UI_W / 3, CELLS_Y + 1, UI_W / 3, CELLS_H - 1, &font_value_26, i > 0);
        gesture_bind(s.cells[i].box, on_cell, i);
    }
}

static void enter(struct ui_ctx *ui)
{
    s.undo_ok = 0;
    set_fft(ui, s.fft);
    graph_update(ui->globals);
}

static void leave(struct ui_ctx *ui)
{
    scope_watch(0, SCOPE_OFF);
    if (globals_save(ui->globals, &ui->rig->src))
        ui_toast(T(MSG_EQ_SAVE_FAILED));
}

/* the spectrum: new samples into the rings; at most every FRAME_MS a new analysis, the curves settled */
static void tick(struct ui_ctx *ui)
{
    if (!s.fft)
        return;
    uint32_t now = time_ms();
    int n;
    while ((n = scope_read(s.in_b, s.in_a, CHUNK)) > 0) {
        for (int i = 0; i < n; i++, s.head++) {
            s.hist_b[s.head & (SPEC_N - 1)] = s.in_b[i];
            s.hist_a[s.head & (SPEC_N - 1)] = s.in_a[i];
        }
        s.fresh += n;
        s.last_data = now;
    }
    if (now - s.last_frame < FRAME_MS)
        return;
    float dt = (float)(now - s.last_frame) * 0.001f;
    dt = dt > 0.1f ? 0.1f : dt;
    int fresh = s.fresh >= HOP;
    if (fresh) {
        for (int pass = 0; pass < 2; pass++) {
            const float *ring = pass ? s.hist_a : s.hist_b;
            for (int i = 0; i < SPEC_N; i++)        /* oldest first */
                s.lin[i] = ring[(s.head + (uint32_t)i) & (SPEC_N - 1)];
            spectrum_run(&s.sp, s.lin, pass ? s.new_a : s.new_b);
        }
        s.fresh = 0;
    } else if (now - s.last_data <= QUIET_MS) {
        return;
    }
    float fall = FALL_DB_S * dt;
    int lit = 0;
    for (int c = 0; c < AXIS_W; c++) {
        float *v[2] = { &s.show_b[c], &s.show_a[c] };
        float to[2] = { fresh ? s.new_b[c] : SPEC_FLOOR_DB, fresh ? s.new_a[c] : SPEC_FLOOR_DB };
        for (int k = 0; k < 2; k++) {
            if (to[k] > *v[k])
                *v[k] += RISE * (to[k] - *v[k]);
            else
                *v[k] = *v[k] - fall > to[k] ? *v[k] - fall : to[k];
            lit |= *v[k] > SPEC_BOT;
        }
    }
    s.last_frame = now;
    if (fresh || lit)
        render(ui->globals);
}

static void set_pill(lv_obj_t *pill, lv_obj_t *text, const char *label, int on)
{
    theme_set_text(text, label);
    lv_obj_center(text);
    theme_set_color(text, on ? C_BG : C_OFF);
    theme_set_bg(pill, on ? C_ACCENT : C_BG);
    theme_set_border(pill, on ? C_ACCENT : C_BORDER_OFF);
}

static void refresh(struct ui_ctx *ui)
{
    const struct globals *g = ui->globals;
    const struct stage *st = &stages[s.sel];
    top_bar_sub(T(MSG_EQ_TITLE));
    theme_set_text(s.name, T(st->name));
    set_pill(s.eq_pill, s.eq_text, T(g->eq_on ? MSG_EQ_EQ_ON : MSG_EQ_EQ_OFF), g->eq_on);
    set_pill(s.fft_pill, s.fft_text, "FFT", 0);
    theme_set_color(s.fft_text, s.fft ? C_TEXT : C_OFF);
    theme_set_border(s.fft_pill, s.fft ? C_LABEL : C_BORDER_OFF);
    set_pill(s.flat_pill, s.flat_text, s.undo_ok ? T(MSG_EQ_UNDO) : "FLAT", 0);
    theme_set_color(s.flat_text, s.undo_ok ? C_ACCENT : C_LABEL);
    theme_set_border(s.flat_pill, s.undo_ok ? C_ACCENT : C_BORDER_OFF);
    if (st->on >= 0) {
        lv_obj_clear_flag(s.on_pill, LV_OBJ_FLAG_HIDDEN);
        set_pill(s.on_pill, s.on_text, g->eq[st->on] != 0.0f ? "ON" : "OFF", g->eq[st->on] != 0.0f);
    } else {
        lv_obj_add_flag(s.on_pill, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < STAGES; i++) {
        int on = stage_on(g, i), cur = i == s.sel;
        theme_set_color(s.chip_text[i], cur ? C_TEXT : on ? C_LABEL : C_DIM);
        theme_set_bg(s.chip_bar[i], on ? (g->eq_on ? C_ACCENT : C_OFF) : C_BORDER_OFF);
        lv_obj_set_style_outline_width(s.chip_bar[i], cur ? 2 : 0, 0);
    }

    for (int i = 0; i < 3; i++) {
        int k = knob_param(s.sel, i);
        char v[24];
        if (k < 0) {
            param_cell_set(&s.cells[i], "", "", "", -1.0f, C_LABEL, C_TEXT, C_ACCENT);
            continue;
        }
        param_text(g, k, v, sizeof v);
        int live = g->eq_on && stage_on(g, s.sel);
        const char *label = k == EQ_LEVEL ? T(MSG_EQ_LEVEL) : i == 1 ? "Q" : T(i ? MSG_EQ_GAIN : MSG_EQ_FREQUENCY);
        param_cell_set(&s.cells[i], label, "", v, frac_of(k, g->eq[k]),
                       C_LABEL, live ? C_TEXT : C_LABEL, live ? C_ACCENT : C_OFF);
    }
    graph_update(g);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        s.sel = ((s.sel + d) % STAGES + STAGES) % STAGES;
        ui->rev++;
        return 1;
    }
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        if (ev->type == IN_TURN && ev->id == k) {
            nudge(ui, k - ENC_KNOB1, k == ENC_KNOB1 ? ev->fast : ev->delta);   /* frequencies accelerate */
            return 1;
        }
    }
    if (ui_pressed(ev, BTN_MENU)) {
        toggle_stage(ui, s.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {
        toggle_eq(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_CTRL)) {
        set_fft(ui, !s.fft);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_GLOBAL)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_eq = { create, enter, refresh, input, leave, tick };
