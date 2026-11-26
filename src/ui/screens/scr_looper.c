/*
 * Looper (the footswitch function LOOPER; factory presets: C+D). Not in the design; the original's looper
 * screen (screen 0x0A, event table 0x800C0FB4) gives the controls:
 *
 *   FS A (press)     record -> play -> overdub -> play ...; with AUTO REC it waits for the guitar instead
 *   FS B (press)     stop; held: erase the loop
 *   FS C (press)     undo / redo the last overdub
 *   FS D (release)   AUTO REC on / off
 *   C + D            leave (also EXIT)
 *   knob 1 / knob 3  record / playback level, knob 2  PRE / POST (while the looper is empty)
 *   MENU press       erase the loop (stopped)
 * The screen: the state and the time, the loop's waveform with the playback position (red while recording or
 * overdubbing, green while playing), the levels, and a card per footswitch.
 * Touch: tap a footswitch card = that footswitch, drag a level sideways = its knob.
 * The levels, place and undo are global settings, written when the screen is left.
 */
#include "dsp/pitch.h"
#include "engine/looper.h"
#include "model/globals.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "platform/sdram.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define STATE_Y   (UI_TOPBAR_H + 4)
#define WAVE_Y    (UI_TOPBAR_H + 40)
#define WAVE_H    58
#define WAVE_W    (UI_W - 2 * UI_PAD)
#define CELLS_Y   (WAVE_Y + WAVE_H + 4)
#define CELLS_H   58
#define REC_SPAN  (8u * 44100u)             /* while recording the waveform spans at least 8 s, then grows */
#define CARDS_Y   (CELLS_Y + CELLS_H + 6)
#define CARD_GAP  7
#define CARD_W    ((UI_W - 2 * UI_PAD - 3 * CARD_GAP) / 4)
#define CARD_H    (UI_H - CARDS_Y - 8)
#define DRAG_PX   8

static struct {
    lv_obj_t *state, *time, *wave, *empty;
    uint8_t peaks[WAVE_W];
    uint32_t span, pos;                     /* frames the waveform spans, where playback is */
    uint32_t colour;
    uint8_t height[256];                    /* a peak's height in the waveform, on a dB scale */
    struct param_cell cells[3];
    lv_obj_t *cards[4], *keys[4], *labels[4];
    uint8_t down;                           /* footswitches held (bit per A..D) */
    int leaving;                            /* C + D pressed together */
    int drag_start;
} s SDRAM_BSS;

static int key(const struct input_event *ev)
{
    static const uint32_t b[4] = { BTN_FS_A, BTN_FS_B, BTN_FS_C, BTN_FS_D };
    if (ev->type == IN_TURN)
        return -1;
    for (int k = 0; k < 4; k++)
        if (b[k] == 1u << ev->id)
            return k;
    return -1;
}

static void press_a(void)                  /* FUN_80009360 */
{
    struct looper_status st;
    looper_status(&st);
    switch (st.state) {
    case LOOP_EMPTY:
        if (looper_auto())
            looper_arm(1);
        else
            looper_command(LOOP_CMD_REC);
        break;
    case LOOP_PLAY:
        if (looper_auto())
            looper_arm(1);
        else
            looper_command(LOOP_CMD_DUB);
        break;
    default:
        looper_command(LOOP_CMD_PLAY);
        break;
    }
}

static void level(struct ui_ctx *ui, int which, int d)
{
    struct globals *g = ui->globals;
    if (which == 1) {
        struct looper_status st;
        looper_status(&st);
        if (st.state != LOOP_EMPTY) {
            ui_toast("PR\xC3\x89 / P\xC3\x93S: apague o loop antes");
            return;
        }
        if (d)
            g->loop_post = !g->loop_post;
    } else {
        uint8_t *v = which == 0 ? &g->loop_rec : &g->loop_play;
        int n = *v + d;
        *v = (uint8_t)(n < 0 ? 0 : n > 100 ? 100 : n);
    }
    globals_apply(g);
    ui->rev++;
}

static void footswitch(struct ui_ctx *ui, int k, int type)
{
    if (type == IN_PRESS) {
        s.down |= (uint8_t)(1u << k);
        if ((s.down & 0xc) == 0xc) {
            s.leaving = 1;
            return;
        }
        if (k == 0)
            press_a();
        else if (k == 1)
            looper_command(LOOP_CMD_STOP);
        else if (k == 2)
            looper_undo();
    } else if (type == IN_HOLD && k == 1) {
        looper_command(LOOP_CMD_CLEAR);
    } else if (type == IN_RELEASE) {
        s.down &= (uint8_t)~(1u << k);
        if (s.leaving) {
            if (!(s.down & 0xc)) {
                s.leaving = 0;
                ui_back();
            }
            return;
        }
        if (k == 3) {
            looper_set_auto(!looper_auto());
            if (!looper_auto())
                looper_arm(0);
        }
    }
    ui->rev++;
}

/* ---------------------------------------------------------------- touch */

static void on_card(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP) {
        footswitch(ui, g->tag, IN_PRESS);
        footswitch(ui, g->tag, IN_RELEASE);
    } else if (g->kind == GESTURE_LONG && g->tag == 1) {
        looper_command(LOOP_CMD_CLEAR);
    }
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && g->tag == 1) {
        level(ui, 1, 1);
        return;
    }
    if (g->kind != GESTURE_DRAG || g->vertical || g->tag == 1)
        return;
    if (g->first)
        s.drag_start = 0;
    int steps = g->dx / DRAG_PX;
    if (steps != s.drag_start) {
        level(ui, g->tag, steps - s.drag_start);
        s.drag_start = steps;
    }
}

/* ---------------------------------------------------------------- screen */

/* the waveform, mirrored about the middle; brighter up to the playback position; the position as a line */
static void wave_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s.wave, &a);
    int mid = (a.y1 + a.y2) / 2, half = WAVE_H / 2 - 4;
    int head = s.span ? (int)((uint64_t)s.pos * WAVE_W / s.span) : -1;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = 1;
    d.color = rgb(C_LINE);
    d.p1.x = a.x1 + 1;
    d.p2.x = a.x2 - 1;
    d.p1.y = d.p2.y = mid;
    lv_draw_line(layer, &d);
    d.color = rgb(s.colour);
    for (int x = 1; x < WAVE_W - 1; x++) {
        int h = s.height[s.peaks[x]] * half / 255;
        if (!h)
            continue;
        d.opa = x <= head ? LV_OPA_COVER : LV_OPA_50;
        d.p1.x = d.p2.x = a.x1 + x;
        d.p1.y = mid - h;
        d.p2.y = mid + h;
        lv_draw_line(layer, &d);
    }
    if (head >= 0) {
        d.opa = LV_OPA_COVER;
        d.width = 2;
        d.color = rgb(C_TEXT);
        d.p1.x = d.p2.x = a.x1 + (head < WAVE_W - 2 ? head : WAVE_W - 2);
        d.p1.y = a.y1 + 2;
        d.p2.y = a.y2 - 2;
        lv_draw_line(layer, &d);
    }
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    s.state = theme_label(root, &font_value_26, C_TEXT, 0);
    lv_obj_set_pos(s.state, UI_PAD, STATE_Y);
    s.time = theme_label(root, &font_value_20, C_LABEL, 0);
    lv_obj_set_pos(s.time, UI_PAD, STATE_Y + 4);
    s.wave = theme_box(root, UI_PAD, WAVE_Y, WAVE_W, WAVE_H, C_BG);
    lv_obj_set_style_radius(s.wave, 3, 0);
    lv_obj_set_style_border_width(s.wave, 1, 0);
    lv_obj_set_style_border_color(s.wave, rgb(C_BORDER_OFF), 0);
    lv_obj_add_event_cb(s.wave, wave_draw, LV_EVENT_DRAW_MAIN, 0);
    for (int v = 1; v < 256; v++) {         /* -42..0 dB of the loop's full scale */
        float db = 6.0206f * pitch_log2((float)v / 255.0f), h = (db + 42.0f) / 42.0f;
        s.height[v] = (uint8_t)(h < 0 ? 0 : h > 1 ? 255 : (int)(h * 255.0f + 0.5f));
    }
    s.empty = theme_label(s.wave, &font_label_12, C_DIM, 2);
    lv_obj_center(s.empty);
    theme_box(root, 0, CELLS_Y, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.cells[i], root, i * UI_W / 3, CELLS_Y + 1, UI_W / 3, CELLS_H, &font_value_20, i > 0);
        gesture_bind(s.cells[i].box, on_cell, i);
    }
    for (int k = 0; k < 4; k++) {
        s.cards[k] = theme_box(root, UI_PAD + k * (CARD_W + CARD_GAP), CARDS_Y, CARD_W, CARD_H, C_BG);
        lv_obj_set_style_radius(s.cards[k], 3, 0);
        lv_obj_set_style_border_width(s.cards[k], 2, 0);
        lv_obj_set_style_border_color(s.cards[k], rgb(C_BORDER_OFF), 0);
        s.keys[k] = theme_label(s.cards[k], &font_tag_12, C_OFF, 1);
        lv_label_set_text(s.keys[k], (const char[]){ (char)('A' + k), 0 });
        lv_obj_set_pos(s.keys[k], 8, 4);
        s.labels[k] = theme_label(s.cards[k], &font_tag_12, C_TEXT, 1);
        lv_obj_align(s.labels[k], LV_ALIGN_BOTTOM_LEFT, 8, -4);
        gesture_bind(s.cards[k], on_card, k);
    }
}

static void enter(struct ui_ctx *ui)
{
    (void)ui;
    s.down = 0;
    s.leaving = 0;
}

static void leave(struct ui_ctx *ui)
{
    if (globals_save(ui->globals, &ui->rig->src))
        ui_toast("Falha ao gravar o looper");
}

static void seconds(uint32_t frames, char *out, unsigned size)
{
    uint32_t tenths = frames / 4410;
    lv_snprintf(out, size, "%u.%u s", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

static void refresh(struct ui_ctx *ui)
{
    const struct globals *g = ui->globals;
    struct looper_status st;
    looper_status(&st);
    top_bar_sub(st.state == LOOP_EMPTY ? (g->loop_post ? "LOOPER \xC2\xB7 P\xC3\x93S" : "LOOPER \xC2\xB7 PR\xC3\x89") :
                st.place == LOOP_POST ? "LOOPER \xC2\xB7 P\xC3\x93S" : "LOOPER \xC2\xB7 PR\xC3\x89");

    static const char *const names[] = { [LOOP_EMPTY] = "VAZIO", [LOOP_STOPPED] = "PARADO", [2] = "PARADO",
                                         [LOOP_PLAY] = "TOCANDO", [LOOP_REC] = "GRAVANDO", [LOOP_DUB] = "SOBREPONDO" };
    const char *name = st.armed ? "ESPERANDO O SINAL" : names[st.state];
    uint32_t color = st.state == LOOP_REC || st.state == LOOP_DUB ? C_CLIP : st.state == LOOP_PLAY ? C_TUNED : C_TEXT;
    theme_set_text(s.state, name);
    theme_set_color(s.state, st.armed ? C_ACCENT : color);

    char a[16], b[16], line[48];
    if (st.state == LOOP_EMPTY) {
        seconds(st.max, b, sizeof b);
        lv_snprintf(line, sizeof line, "at\xC3\xA9 %s", b);
    } else if (st.state == LOOP_REC) {
        seconds(st.pos, a, sizeof a);
        lv_snprintf(line, sizeof line, "%s", a);
    } else {
        seconds(st.pos, a, sizeof a);
        seconds(st.len, b, sizeof b);
        lv_snprintf(line, sizeof line, "%s / %s", a, b);
    }
    theme_set_text(s.time, line);
    lv_obj_update_layout(s.time);
    lv_obj_set_x(s.time, UI_W - UI_PAD - lv_obj_get_width(s.time));

    /* the waveform: the loop's length; while recording at least REC_SPAN, growing ahead of the recording */
    if (st.state == LOOP_EMPTY) {
        s.span = 0;
    } else if (st.state == LOOP_REC) {
        uint32_t want = st.pos + st.pos / 4;
        s.span = want < REC_SPAN ? REC_SPAN : want > st.max ? st.max : want;
    } else {
        s.span = st.len;
    }
    s.pos = st.pos;
    s.colour = color == C_TEXT ? C_LABEL : color;
    if (s.span)
        looper_wave(s.peaks, WAVE_W, s.span);
    theme_set_text(s.empty, st.state == LOOP_EMPTY ? (st.armed ? "COMECE A TOCAR" : "PISE A PARA GRAVAR") : "");
    lv_obj_center(s.empty);
    theme_set_border(s.wave, st.state == LOOP_REC || st.state == LOOP_DUB ? C_CLIP : C_BORDER_OFF);
    lv_obj_invalidate(s.wave);

    char v[16];
    lv_snprintf(v, sizeof v, "%d", g->loop_rec);
    param_cell_set(&s.cells[0], "GRAVA\xC3\x87\xC3\x83O", "", v, g->loop_rec / 100.0f, C_LABEL, C_TEXT, C_ACCENT);
    param_cell_set(&s.cells[1], "POSI\xC3\x87\xC3\x83O", "", g->loop_post ? "P\xC3\x93S" : "PR\xC3\x89", -1, C_LABEL,
                   st.state == LOOP_EMPTY ? C_TEXT : C_LABEL, C_ACCENT);
    lv_snprintf(v, sizeof v, "%d", g->loop_play);
    param_cell_set(&s.cells[2], "REPRODU\xC3\x87\xC3\x83O", "", v, g->loop_play / 100.0f, C_LABEL, C_TEXT, C_ACCENT);

    const char *la = st.state == LOOP_EMPTY ? "GRAVAR" : st.state == LOOP_PLAY ? "SOBREPOR" : "TOCAR";
    const char *lc = st.layer == 1 ? "DESFAZER" : st.layer == 2 ? "REFAZER" : "-";
    const char *labels[4] = { la, "PARAR", lc, looper_auto() ? "AUTO REC ON" : "AUTO REC OFF" };
    for (int k = 0; k < 4; k++) {
        theme_set_text(s.labels[k], labels[k]);
        lv_obj_align(s.labels[k], LV_ALIGN_BOTTOM_LEFT, 8, -4);
        int lit = (k == 3 && looper_auto()) || (k == 0 && (st.state == LOOP_REC || st.state == LOOP_DUB));
        theme_set_border(s.cards[k], lit ? C_ACCENT : C_BORDER_OFF);
        theme_set_color(s.labels[k], k == 2 && !st.layer ? C_OFF : C_TEXT);
    }
}

static void tick(struct ui_ctx *ui)
{
    static uint32_t shown;
    struct looper_status st;
    looper_status(&st);
    uint32_t sig = st.pos / 2205 ^ (uint32_t)st.state << 24 ^ (uint32_t)st.armed << 28;
    if (sig != shown) {                     /* the time moves every 50 ms */
        shown = sig;
        ui->rev++;
    }
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int k = key(ev);
    if (k >= 0) {
        footswitch(ui, k, ev->type);
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        int d = ui_turned(ev, ENC_KNOB1 + i);
        if (d) {
            level(ui, i, i == 1 ? d : ev->fast);
            return 1;
        }
    }
    if (ui_pressed(ev, BTN_MENU)) {
        struct looper_status st;
        looper_status(&st);
        if (st.state == LOOP_STOPPED)
            looper_command(LOOP_CMD_CLEAR);
        ui->rev++;
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT)) {
        ui_back();
        return 1;
    }
    return ev->type == IN_PRESS && (1u << ev->id == BTN_BANK_UP || 1u << ev->id == BTN_BANK_DOWN);
}

const struct screen screen_looper = { create, enter, refresh, input, leave, tick };
