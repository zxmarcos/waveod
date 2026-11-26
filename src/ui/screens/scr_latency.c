/*
 * GLOBAL › LATÊNCIA: the latency test (engine/latency.h). The four routes as cards on top (a tap, the knobs or
 * MENU turns pick one), what to plug under them, then the result in large figures beside the MEDIR / CANCELAR
 * button with the runs done, a bar with its parts (the buffers' 128 samples and the converters'; or, through
 * the preset, what the preset adds) and the runs' spread, the level and the match.
 *
 * Controls: MENU turns or knobs = route, MENU press = start / cancel, EXIT or GLOBAL = back. Touch: a route
 * card, the button. Leaving the screen cancels a test running. A test started from waveod Studio shows here too.
 */
#include "engine/latency.h"
#include "platform/audio.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define GAP       6
#define CARD_Y    (UI_TOPBAR_H + 6)
#define CARD_H    44
#define CARD_W    ((UI_W - 2 * UI_PAD - (LAT_ROUTES - 1) * GAP) / LAT_ROUTES)
#define HINT_Y    (CARD_Y + CARD_H + 6)
#define BIG_Y     (HINT_Y + 36)
#define BUTTON_W  140
#define BUTTON_H  34
#define BUTTON_X  (UI_W - UI_PAD - BUTTON_W)
#define BAR_Y     (BIG_Y + 70)
#define BAR_W     (UI_W - 2 * UI_PAD)
#define PARTS_Y   (BAR_Y + 10)
#define SPREAD_Y  (PARTS_Y + 18)
#define TEXT_W    (UI_W - 2 * UI_PAD)
#define C_FRAME   0x2A2926
#define C_SEL_BG  0x161514

static const char *const route_name[LAT_ROUTES] = { "SA\xC3\x8D" "DA L", "SA\xC3\x8D" "DA R", "FX SEND", "PRESET" };
static const char *const route_sub[LAT_ROUTES] = { "\xC2\xBB INPUT", "\xC2\xBB INPUT", "\xC2\xBB FX RETURN",
                                                   "SEM CABO" };

static const char *const hints[LAT_ROUTES] = {
    "Cabo da sa\xC3\xAD" "da L no INPUT. Desligue amplificador e fone: o teste d\xC3\xA1 um clique.",
    "Cabo da sa\xC3\xAD" "da R no INPUT. Desligue amplificador e fone: o teste d\xC3\xA1 um clique.",
    "Cabo do FX SEND no FX RETURN. Desligue amplificador e fone: o teste d\xC3\xA1 um clique.",
    "Sem cabo: o sinal de teste passa pelo preset ativo; as sa\xC3\xAD" "das ficam mudas.",
};

static struct {
    lv_obj_t *card[LAT_ROUTES], *card_name[LAT_ROUTES], *card_sub[LAT_ROUTES];
    lv_obj_t *hint, *big, *samples, *button, *button_text, *runs[LAT_RUNS];
    lv_obj_t *track, *part_a, *part_b, *label_a, *label_b, *spread;
    int route;
    uint32_t seen;
} s SDRAM_BSS;

/* v with one decimal ("135.0"); hundredths when cents */
static void decimal(char *out, unsigned size, float v, int cents)
{
    int scale = cents ? 100 : 10, neg = v < 0;
    int n = (int)((neg ? -v : v) * (float)scale + 0.5f);
    if (cents)
        lv_snprintf(out, size, "%s%d.%02d", neg ? "-" : "", n / 100, n % 100);
    else
        lv_snprintf(out, size, "%s%d.%d", neg ? "-" : "", n / 10, n % 10);
}

static float to_ms(float samples)
{
    return samples * (1000.0f / (float)AUDIO_RATE);
}

static int busy(void)
{
    struct latency_result r;
    latency_get(&r);
    return r.status == LAT_BUSY;
}

static void pick(struct ui_ctx *ui, int route)
{
    if (busy())
        return;
    s.route = (route % LAT_ROUTES + LAT_ROUTES) % LAT_ROUTES;
    ui->rev++;
}

static void run(struct ui_ctx *ui)
{
    if (busy())
        latency_cancel();
    else
        latency_start(s.route);
    ui->rev++;
}

/* ---------------------------------------------------------------- touch */

static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        pick(ui, g->tag);
}

static void on_button(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP)
        run(ui);
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    for (int i = 0; i < LAT_ROUTES; i++) {
        s.card[i] = theme_box(root, UI_PAD + i * (CARD_W + GAP), CARD_Y, CARD_W, CARD_H, C_BG);
        lv_obj_set_style_radius(s.card[i], 3, 0);
        gesture_bind(s.card[i], on_card, i);
        s.card_name[i] = theme_label(s.card[i], &font_body_14, C_TEXT, 0);
        lv_label_set_text_static(s.card_name[i], route_name[i]);
        lv_obj_set_pos(s.card_name[i], 10, 5);
        s.card_sub[i] = theme_label(s.card[i], &font_tag_12, C_OFF, 1);
        lv_label_set_text_static(s.card_sub[i], route_sub[i]);
        lv_obj_set_pos(s.card_sub[i], 10, 24);
    }
    s.hint = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_obj_set_width(s.hint, TEXT_W);
    lv_label_set_long_mode(s.hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s.hint, UI_PAD, HINT_Y);

    s.big = theme_label(root, &font_title_40, C_DIM, 0);
    lv_obj_set_pos(s.big, UI_PAD, BIG_Y);
    s.samples = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_obj_set_pos(s.samples, UI_PAD, BIG_Y + 46);

    s.button = theme_box(root, BUTTON_X, BIG_Y + 4, BUTTON_W, BUTTON_H, C_SEL_BG);
    lv_obj_set_style_radius(s.button, 3, 0);
    lv_obj_set_style_border_width(s.button, 1, 0);
    gesture_bind(s.button, on_button, 0);
    s.button_text = theme_label(s.button, &font_label_12, C_ACCENT, 2);
    int seg = (BUTTON_W - (LAT_RUNS - 1) * 3) / LAT_RUNS;
    for (int i = 0; i < LAT_RUNS; i++)
        s.runs[i] = theme_box(root, BUTTON_X + i * (seg + 3), BIG_Y + BUTTON_H + 14, seg, 4, C_TRACK);

    s.track = theme_box(root, UI_PAD, BAR_Y, BAR_W, 6, C_TRACK);
    lv_obj_set_style_radius(s.track, 1, 0);
    s.part_a = theme_box(s.track, 0, 0, 0, 6, C_LABEL);
    s.part_b = theme_box(s.track, 0, 0, 0, 6, C_ACCENT);
    s.label_a = theme_label(root, &font_tag_12, C_LABEL, 1);
    lv_obj_set_pos(s.label_a, UI_PAD, PARTS_Y);
    s.label_b = theme_label(root, &font_tag_12, C_ACCENT, 1);
    lv_obj_set_pos(s.label_b, UI_PAD, PARTS_Y);
    s.spread = theme_label(root, &font_tag_12, C_OFF, 1);
    lv_obj_set_width(s.spread, TEXT_W);
    lv_label_set_long_mode(s.spread, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s.spread, UI_PAD, SPREAD_Y);
}

static void enter(struct ui_ctx *ui)
{
    (void)ui;
    struct latency_result r;
    latency_get(&r);
    if (r.status != LAT_IDLE)
        s.route = r.route;
}

static void leave(struct ui_ctx *ui)
{
    (void)ui;
    latency_cancel();
}

static void tick(struct ui_ctx *ui)
{
    struct latency_result r;
    latency_get(&r);
    if (r.seq != s.seen) {
        s.seen = r.seq;
        ui->rev++;
    }
}

/* the bar: part a (grey) then part b (amber), out of their sum; a's label on the left, b's on the right */
static void parts(float a, float b, const char *ta, const char *tb)
{
    float total = a + b;
    int wa = total > 0 ? (int)(BAR_W * a / total + 0.5f) : 0, wb = total > 0 ? BAR_W - wa : 0;
    lv_obj_set_width(s.part_a, wa);
    lv_obj_set_x(s.part_b, wa);
    lv_obj_set_width(s.part_b, wb);
    theme_set_text(s.label_a, ta);
    theme_set_text(s.label_b, tb);
    lv_obj_update_layout(s.label_b);
    lv_obj_set_x(s.label_b, UI_W - UI_PAD - lv_obj_get_width(s.label_b));
}

static void show_result(const struct latency_result *r)
{
    char big[24], smp[64], a[16], b[16], c[16], ta[48], tb[48], sp[96];
    int cable = r->route != LAT_PRESET, measured = r->status == LAT_OK || r->status == LAT_UNSTABLE;
    uint32_t colour = C_ACCENT;
    smp[0] = ta[0] = tb[0] = sp[0] = 0;
    switch (r->status) {
    case LAT_BUSY:
        lv_snprintf(big, sizeof big, "MEDINDO");
        colour = C_LABEL;
        if (cable)
            lv_snprintf(smp, sizeof smp, "N\xC3\x8DVEL DO SINAL %d dBFS", r->level_db);
        break;
    case LAT_OK:
    case LAT_UNSTABLE:
        decimal(a, sizeof a, to_ms(r->mean), 1);
        lv_snprintf(big, sizeof big, "%s ms", a);
        colour = r->status == LAT_OK ? C_ACCENT : C_CLIP;
        decimal(b, sizeof b, r->mean, 0);
        lv_snprintf(smp, sizeof smp, "%s AMOSTRAS A 44.1 kHz%s", b,
                    r->status == LAT_UNSTABLE ? " \xC2\xB7 INST\xC3\x81VEL" : "");
        decimal(a, sizeof a, r->min, 0);
        decimal(b, sizeof b, r->max, 0);
        if (cable)
            lv_snprintf(c, sizeof c, " \xC2\xB7 %d dBFS", r->level_db);
        else
            c[0] = 0;
        lv_snprintf(sp, sizeof sp, "%d MEDIDAS DE %s A %s%s \xC2\xB7 SEMELHAN\xC3\x87" "A %d%%", r->runs, a, b, c,
                    (int)(r->match * 100.0f + 0.5f));
        break;
    case LAT_NO_SIGNAL:
        lv_snprintf(big, sizeof big, "SEM SINAL");
        colour = C_CLIP;
        lv_snprintf(smp, sizeof smp, cable ? "NADA VOLTOU, NEM A -10 dBFS: CONFIRA O CABO"
                                           : "O PRESET N\xC3\x83O DEIXOU PASSAR (GATE? VOLUME?)");
        break;
    case LAT_CLIPPED:
        lv_snprintf(big, sizeof big, "SATUROU");
        colour = C_CLIP;
        lv_snprintf(smp, sizeof smp, "USE A ENTRADA LINHA (GLOBAL \xC2\xBB IN / OUT)");
        break;
    case LAT_CANCELLED:
        lv_snprintf(big, sizeof big, "CANCELADO");
        colour = C_DIM;
        break;
    default:
        lv_snprintf(big, sizeof big, "\xE2\x80\x93 ms");
        colour = C_DIM;
        break;
    }
    theme_set_text(s.big, big);
    theme_set_color(s.big, colour);
    theme_set_text(s.samples, smp);
    theme_set_color(s.samples, r->status == LAT_NO_SIGNAL || r->status == LAT_CLIPPED ? C_CLIP : C_LABEL);
    theme_set_text(s.spread, sp);

    /* with a cable, the buffers and the rest (the converters); through the preset all of it is the preset's */
    if (measured && cable) {
        decimal(a, sizeof a, to_ms(LAT_BUFFERS), 1);
        lv_snprintf(ta, sizeof ta, "BUFFERS %d \xC2\xB7 %s ms", LAT_BUFFERS, a);
        decimal(b, sizeof b, r->mean - LAT_BUFFERS, 0);
        decimal(c, sizeof c, to_ms(r->mean - LAT_BUFFERS), 1);
        lv_snprintf(tb, sizeof tb, "CONVERSORES %s \xC2\xB7 %s ms", b, c);
        parts(LAT_BUFFERS, r->mean > LAT_BUFFERS ? r->mean - LAT_BUFFERS : 0, ta, tb);
    } else if (measured) {
        decimal(c, sizeof c, to_ms(r->mean), 1);
        lv_snprintf(tb, sizeof tb, "O PRESET SOMA %s ms", c);
        parts(0, r->mean, "", tb);
    } else if (cable) {
        decimal(a, sizeof a, to_ms(LAT_BUFFERS), 1);
        lv_snprintf(ta, sizeof ta, "S\xC3\x93 OS BUFFERS J\xC3\x81 D\xC3\x83O %d AMOSTRAS \xC2\xB7 %s ms", LAT_BUFFERS, a);
        parts(0, 0, ta, "");
    } else {
        parts(0, 0, "", "");
    }
}

static void refresh(struct ui_ctx *ui)
{
    (void)ui;
    struct latency_result r;
    latency_get(&r);
    int on = r.status == LAT_BUSY, route = on ? r.route : s.route;
    top_bar_sub("LAT\xC3\x8ANCIA");
    for (int i = 0; i < LAT_ROUTES; i++) {
        int sel = i == route;
        theme_set_bg(s.card[i], sel ? C_SEL_BG : C_BG);
        lv_obj_set_style_border_width(s.card[i], sel ? 2 : 1, 0);
        theme_set_border(s.card[i], sel ? C_ACCENT : C_FRAME);
        theme_set_color(s.card_name[i], sel ? C_TEXT : on ? C_DIM : C_NEUTRAL_ON);
        theme_set_color(s.card_sub[i], sel ? C_ACCENT : C_OFF);
    }
    theme_set_text(s.hint, hints[route]);
    theme_set_text(s.button_text, on ? "CANCELAR" : "MEDIR");
    lv_obj_center(s.button_text);
    theme_set_border(s.button, on ? C_LABEL : C_ACCENT);
    theme_set_color(s.button_text, on ? C_LABEL : C_ACCENT);
    for (int i = 0; i < LAT_RUNS; i++) {
        int done = (on || r.status == LAT_OK || r.status == LAT_UNSTABLE) && i < r.runs;
        theme_set_bg(s.runs[i], done ? (on ? C_LABEL : C_ACCENT) : on && i == r.runs ? C_DIM : C_TRACK);
    }
    show_result(&r);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    for (int k = ENC_KNOB1; !d && k <= ENC_KNOB3; k++)
        d = ui_turned(ev, k);
    if (d) {
        pick(ui, s.route + d);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        run(ui);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_GLOBAL)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_latency = { create, enter, refresh, input, leave, tick };
