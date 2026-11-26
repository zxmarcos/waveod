/*
 * Modulators (CONTROLES › MOD, or a link of a modulator anywhere): the preset's four modulators (fw/preset_format.h
 * struct preset_mods; docs/modulators.md), one at a time, with a drawing of what it does now and, below, its
 * settings and the parameters it moves. Not in the design; a list like the settings under a drawing.
 *
 *   tabs       MOD 1-4, each with its type and a bar of its value now
 *   drawing    LFO: two cycles of its shape, the point where it is now running along them (the random shapes:
 *              what it did over the last seconds); ENVELOPE: its value over the last seconds; SEQUÊNCIA: a bar per
 *              step, the one playing lit, the glides as ramps, the step of the selected row outlined
 *   rows       TIPO; by type FORMA, SINCRONIA, VELOCIDADE, FASE INICIAL / SENSIBILIDADE, ATAQUE, RETORNO /
 *              SINCRONIA, VELOCIDADE, PASSOS, DESLIZE, a row per step; then a row per link (PARA: the parameter and
 *              its values at MOD 0 % and 100 %) and + NOVA LIGAÇÃO (the destination screen, scr_target.c)
 *
 * Controls: MENU turns = a row, MENU press = the next value / the link's destination / a new link, MENU hold = remove
 * the link; knob 1 = the row's value; knob 2 or EFX = the modulator; EXIT or CTRL = back.
 * Touch: tap a tab = that modulator; rows as the links screen (tap = select, again = MENU press, drag sideways =
 * knob 1, hold a link = remove it); on the drawing, a sequence's steps are set where the finger is (tap or drag), an
 * LFO's tap = the next shape.
 * Changes are part of the preset (EDITADO, saved with it) and play at once (ui_ctl_edited: model/modulation.c).
 */
#include "engine/modulators.h"
#include "model/control.h"
#include "model/param_format.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/mod_graph.h"
#include "ui/widgets/row_list.h"
#include "ui/widgets/top_bar.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define TABS_Y    (UI_TOPBAR_H + 4)
#define TAB_H     28
#define TAB_GAP   7
#define TAB_W     ((UI_W - 2 * UI_PAD - 3 * TAB_GAP) / 4)
#define GRAPH_Y   (TABS_Y + TAB_H + 5)
#define GRAPH_H   78
#define GRAPH_W   (UI_W - 2 * UI_PAD)
#define LIST_Y    (GRAPH_Y + GRAPH_H + 3)
#define ROW_H     27
#define DRAG_PX   24
#define CAP_H     20                        /* the caption above the curve */
#define IN_PAD    6

enum mod_field { MF_TYPE, MF_SHAPE, MF_SYNC, MF_RATE, MF_PHASE, MF_SENS, MF_ATTACK, MF_RELEASE, MF_STEPS, MF_GLIDE,
                 MF_STEP };                 /* MF_STEP + i: step i */

static struct {
    struct row_list list;
    char titles[ROW_LIST_MAX][24];
    lv_obj_t *tabs[MOD_COUNT], *tab_text[MOD_COUNT], *tab_bar[MOD_COUNT];
    lv_obj_t *frame, *caption, *now, *empty;
    struct mod_graph graph;
    int mod;                                /* 0..MOD_COUNT-1 */
    int fields[ROW_LIST_MAX], nfields;      /* the settings rows, from row 0 (enum mod_field) */
    int links[CONTROL_MAX], nlinks;         /* table entries of the modulator, after the settings rows */
    uint32_t last_tick;
    int drag_start;
} s SDRAM_BSS;

static const char *const type_names[MOD_TYPES] = { "DESLIGADO", "LFO", "ENVELOPE", ("SEQU\xC3\x8A" "NCIA") };
static const char *const type_short[MOD_TYPES] = { "\xE2\x80\x93", "LFO", "ENV", "SEQ" };
static const char *const shape_names[MS_SHAPES] = {
    "SENOIDE", "TRI\xC3\x82NGULO", "QUADRADA", "DENTE SUBINDO", "DENTE DESCENDO", "ALEAT\xC3\x93RIA",
    "ALEAT\xC3\x93RIA SUAVE",
};

static struct preset_mod *mod_of(struct ui_ctx *ui, int i)
{
    return &ui->rig->preset.mods.m[i & 3];
}

static struct preset_mod *the_mod(struct ui_ctx *ui)
{
    return mod_of(ui, s.mod);
}

static struct preset_controls *table(struct ui_ctx *ui)
{
    return ui_ctl_table(ui);
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---------------------------------------------------------------- rows */

static void collect(struct ui_ctx *ui)
{
    const struct preset_mod *m = the_mod(ui);
    int *f = s.fields, n = 0;
    f[n++] = MF_TYPE;
    if (m->type == MOD_LFO) {
        f[n++] = MF_SHAPE;
        f[n++] = MF_SYNC;
        if (!m->sync)
            f[n++] = MF_RATE;
        f[n++] = MF_PHASE;
    } else if (m->type == MOD_ENV) {
        f[n++] = MF_SENS;
        f[n++] = MF_ATTACK;
        f[n++] = MF_RELEASE;
    } else if (m->type == MOD_SEQ) {
        f[n++] = MF_SYNC;
        if (!m->sync)
            f[n++] = MF_RATE;
        f[n++] = MF_STEPS;
        f[n++] = MF_GLIDE;
        for (int i = 0; i < m->steps && i < MOD_STEPS; i++)
            f[n++] = MF_STEP + i;
    }
    s.nfields = n;
    const struct preset_controls *c = table(ui);
    int max = ROW_LIST_MAX - s.nfields - 1;
    s.nlinks = 0;
    for (int i = 0; i < c->count && s.nlinks < max; i++)
        if (c->c[i].key == CK_MOD1 + s.mod)
            s.links[s.nlinks++] = i;
}

static int field_of(int row)
{
    return row >= 0 && row < s.nfields ? s.fields[row] : -1;
}

static int row_link(int row)
{
    return row >= s.nfields && row < s.nfields + s.nlinks ? s.links[row - s.nfields] : -1;
}

static int row_add(void)
{
    return s.nfields + s.nlinks;
}

/* the step the selected row is, -1 = none */
static int selected_step(void)
{
    int f = field_of(s.list.sel);
    return f >= MF_STEP ? f - MF_STEP : -1;
}

/* ---------------------------------------------------------------- changes */

static void edited(struct ui_ctx *ui)
{
    ui_ctl_edited(ui);
}

/* a time in ms d steps on (finer when short) */
static int step_ms(int ms, int d, int lo, int hi)
{
    for (; d > 0; d--)
        ms += ms < 20 ? 1 : ms < 100 ? 5 : ms < 500 ? 10 : 50;
    for (; d < 0; d++)
        ms -= ms <= 20 ? 1 : ms <= 100 ? 5 : ms <= 500 ? 10 : 50;
    return clampi(ms, lo, hi);
}

static void mod_knob(struct ui_ctx *ui, int field, int d)
{
    struct preset_mod *m = the_mod(ui);
    if (field == MF_TYPE) {
        int t = clampi(m->type + d, MOD_OFF, MOD_TYPES - 1);
        if (t != m->type && m->type == MOD_OFF)
            preset_mod_default(m, t);
        m->type = (uint8_t)t;
    } else if (field == MF_SHAPE) {
        m->shape = (uint8_t)(((m->shape + d) % MS_SHAPES + MS_SHAPES) % MS_SHAPES);
    } else if (field == MF_SYNC) {
        m->sync = (uint8_t)clampi(m->sync + d, 0, 11);
    } else if (field == MF_RATE) {
        int c = (int)(m->rate * 100.0f + 0.5f);     /* hundredths of a Hz */
        for (; d > 0; d--)
            c += c < 100 ? 5 : c < 500 ? 10 : c < 1000 ? 50 : 100;
        for (; d < 0; d++)
            c -= c <= 100 ? 5 : c <= 500 ? 10 : c <= 1000 ? 50 : 100;
        m->rate = (float)clampi(c, 5, 2000) * 0.01f;
    } else if (field == MF_PHASE) {
        m->phase = (uint8_t)clampi(m->phase + 5 * d, 0, 95);
    } else if (field == MF_SENS) {
        m->sens = (int8_t)clampi(m->sens + d, -20, 40);
    } else if (field == MF_ATTACK) {
        m->attack = (uint16_t)step_ms(m->attack, d, 1, 2000);
    } else if (field == MF_RELEASE) {
        m->release = (uint16_t)step_ms(m->release, d, 10, 5000);
    } else if (field == MF_STEPS) {
        m->steps = (uint8_t)clampi(m->steps + d, 1, MOD_STEPS);
    } else if (field == MF_GLIDE) {
        m->glide = (uint8_t)clampi(m->glide + 5 * d, 0, 100);
    } else if (field >= MF_STEP && field < MF_STEP + MOD_STEPS) {
        uint8_t *v = &m->step[field - MF_STEP];
        *v = (uint8_t)clampi(*v + 5 * d, 0, 100);
    }
}

static void choose(struct ui_ctx *ui, int mod)
{
    s.mod = ((mod % MOD_COUNT) + MOD_COUNT) % MOD_COUNT;
    s.list.sel = 0;
    s.list.top = 0;
    mod_graph_set_mod(&s.graph, s.mod);
    collect(ui);
    ui->rev++;
}

static void knob(struct ui_ctx *ui, int row, int d)
{
    int f = field_of(row);
    if (f < 0)
        return;
    mod_knob(ui, f, d);
    collect(ui);                                    /* a type or sync change shows other rows */
    if (s.list.sel >= row_add() + 1)
        s.list.sel = row_add();
    edited(ui);
}

/* a new link that does nothing yet: the destination screen gives it a parameter, or drops it */
static void add_link(struct ui_ctx *ui)
{
    struct control l = { .key = (uint8_t)(CK_MOD1 + s.mod), .modes = CM_ALL, .when = CW_PRESS, .action = CA_FN,
                         .slot = -1, .arg = FSF_NONE };
    if (preset_control_add(table(ui), &l) != 0) {
        ui_toast("Tabela de controles cheia");
        return;
    }
    collect(ui);
    s.list.sel = row_add() - 1;
    ui_target(table(ui)->count - 1, 1);
}

static void remove_link(struct ui_ctx *ui, int row)
{
    int i = row_link(row);
    if (i < 0)
        return;
    preset_control_remove(table(ui), i);
    collect(ui);
    ui_toast("LIGA\xC3\x87\xC3\x83O REMOVIDA");
    edited(ui);
}

static void activate(struct ui_ctx *ui, int row)
{
    if (field_of(row) >= 0)
        knob(ui, row, 1);
    else if (row_link(row) >= 0)
        ui_target(row_link(row), 0);
    else if (row == row_add())
        add_link(ui);
}

/* ---------------------------------------------------------------- text */

static void field_title(int field, char *buf, unsigned size)
{
    static const char *const titles[MF_STEP] = {
        "TIPO", "FORMA", "SINCRONIA", "VELOCIDADE", "FASE INICIAL", "SENSIBILIDADE", "ATAQUE", "RETORNO", "PASSOS",
        "DESLIZE",
    };
    if (field >= MF_STEP)
        lv_snprintf(buf, size, "PASSO %d", field - MF_STEP + 1);
    else
        lv_snprintf(buf, size, "%s", field >= 0 ? titles[field] : "");
}

static void rate_text(const struct preset_mod *m, char *out, unsigned size)
{
    int c = (int)(m->rate * 100.0f + 0.5f);
    if (m->sync)
        lv_snprintf(out, size, "%s", param_sync_divisions[(m->sync - 1) % 11]);
    else
        lv_snprintf(out, size, "%d.%02d Hz", c / 100, c % 100);
}

static void field_text(struct ui_ctx *ui, int field, char *out, unsigned size)
{
    const struct preset_mod *m = the_mod(ui);
    switch (field) {
    case MF_TYPE:    lv_snprintf(out, size, "%s", type_names[m->type % MOD_TYPES]); break;
    case MF_SHAPE:   lv_snprintf(out, size, "%s", shape_names[m->shape % MS_SHAPES]); break;
    case MF_SYNC:    lv_snprintf(out, size, "%s", m->sync ? param_sync_divisions[(m->sync - 1) % 11] : "LIVRE"); break;
    case MF_RATE:    rate_text(m, out, size); break;
    case MF_PHASE:   lv_snprintf(out, size, "%d%%", m->phase); break;
    case MF_SENS:    lv_snprintf(out, size, "%+d dB", m->sens); break;
    case MF_ATTACK:  lv_snprintf(out, size, "%d ms", m->attack); break;
    case MF_RELEASE: lv_snprintf(out, size, "%d ms", m->release); break;
    case MF_STEPS:   lv_snprintf(out, size, "%d", m->steps); break;
    case MF_GLIDE:   lv_snprintf(out, size, "%d%%", m->glide); break;
    default:         lv_snprintf(out, size, "%d%%", m->step[(field - MF_STEP) % MOD_STEPS]); break;
    }
}

static void value_text(void *ctx, int row, char *out, unsigned size)
{
    struct ui_ctx *ui = ctx;
    if (field_of(row) >= 0)
        field_text(ui, field_of(row), out, size);
    else if (row_link(row) >= 0)
        ui_link_text(ui, &table(ui)->c[row_link(row)], out, size);
    else if (row == row_add())
        lv_snprintf(out, size, "MENU \xC2\xBB");
    else
        out[0] = 0;
}

/* the caption over the drawing: what it is and how fast; the value now on the right */
static void caption(struct ui_ctx *ui)
{
    const struct preset_mod *m = the_mod(ui);
    char text[64], rate[16];
    rate_text(m, rate, sizeof rate);
    if (m->type == MOD_LFO)
        lv_snprintf(text, sizeof text, "%s \xC2\xB7 %s", shape_names[m->shape % MS_SHAPES], rate);
    else if (m->type == MOD_ENV)
        lv_snprintf(text, sizeof text, "ENVELOPE \xC2\xB7 %+d dB \xC2\xB7 %d / %d ms", m->sens, m->attack, m->release);
    else if (m->type == MOD_SEQ)
        lv_snprintf(text, sizeof text, "PASSO %d / %d \xC2\xB7 %s",
                    (int)(mods_cycle(s.mod) % (m->steps ? m->steps : 1)) + 1, m->steps, rate);
    else
        text[0] = 0;
    theme_set_text(s.caption, text);
    if (m->type == MOD_OFF) {
        theme_set_text(s.now, "");
    } else {
        lv_snprintf(text, sizeof text, "%d%%", (int)(mods_value(s.mod) * 100.0f + 0.5f));
        theme_set_text(s.now, text);
    }
    lv_obj_align(s.now, LV_ALIGN_TOP_RIGHT, -IN_PAD, 3);
}

static void tabs(struct ui_ctx *ui)
{
    for (int i = 0; i < MOD_COUNT; i++) {
        const struct preset_mod *m = mod_of(ui, i);
        char text[24];
        lv_snprintf(text, sizeof text, "MOD %d  %s", i + 1, type_short[m->type % MOD_TYPES]);
        theme_set_text(s.tab_text[i], text);
        theme_set_color(s.tab_text[i], m->type == MOD_OFF ? C_OFF : C_TEXT);
        theme_set_border(s.tabs[i], i == s.mod ? C_ACCENT : C_BORDER_OFF);
        int w = m->type == MOD_OFF ? 0 : (int)(mods_value(i) * (float)(TAB_W - 4) + 0.5f);
        lv_obj_set_width(s.tab_bar[i], w > 0 ? w : 1);
        if (m->type == MOD_OFF)
            lv_obj_add_flag(s.tab_bar[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(s.tab_bar[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------- the drawing */

/* the drawing's step under the finger set to its height */
static void graph_step(struct ui_ctx *ui, const struct gesture *g)
{
    struct preset_mod *m = the_mod(ui);
    int i = mod_graph_step_at(&s.graph, g->x), pc = mod_graph_value_at(&s.graph, g->y);
    if (i < 0 || (m->step[i] == pc && g->kind != GESTURE_TAP))
        return;
    m->step[i] = (uint8_t)pc;
    for (int r = 0; r < s.nfields; r++)             /* its row selected, in view */
        if (s.fields[r] == MF_STEP + i)
            s.list.sel = r;
    edited(ui);
}

/* ---------------------------------------------------------------- touch */

static void on_tab(struct ui_ctx *ui, const struct gesture *g)
{
    if (g->kind == GESTURE_TAP && g->tag != s.mod)
        choose(ui, g->tag);
}

static void on_graph(struct ui_ctx *ui, const struct gesture *g)
{
    const struct preset_mod *m = the_mod(ui);
    if (m->type == MOD_SEQ && (g->kind == GESTURE_TAP || g->kind == GESTURE_DRAG))
        graph_step(ui, g);
    else if (m->type == MOD_LFO && g->kind == GESTURE_TAP)
        knob(ui, 1, 1);                             /* row 1 is FORMA */
}

static void on_row(struct ui_ctx *ui, const struct gesture *g)
{
    struct row_list *l = &s.list;
    if (g->tag >= l->count)
        return;
    if (row_list_scroll(l, g)) {
        ui->rev++;
    } else if (g->kind == GESTURE_TAP) {
        if (l->sel == g->tag)
            activate(ui, g->tag);
        l->sel = g->tag;
        ui->rev++;
    } else if (g->kind == GESTURE_LONG && row_link(g->tag) >= 0) {
        l->sel = g->tag;
        remove_link(ui, g->tag);
    } else if (g->kind == GESTURE_DRAG) {
        if (g->first)
            s.drag_start = 0;
        l->sel = g->tag;
        int steps = g->dx / DRAG_PX;
        if (steps != s.drag_start) {
            knob(ui, g->tag, steps - s.drag_start);
            s.drag_start = steps;
        }
    }
}

/* ---------------------------------------------------------------- screen */

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    for (int i = 0; i < MOD_COUNT; i++) {
        lv_obj_t *t = theme_box(root, UI_PAD + i * (TAB_W + TAB_GAP), TABS_Y, TAB_W, TAB_H, C_BG);
        lv_obj_set_style_radius(t, 3, 0);
        lv_obj_set_style_border_width(t, 2, 0);
        gesture_bind(t, on_tab, i);
        s.tabs[i] = t;
        s.tab_text[i] = theme_label(t, &font_body_14, C_TEXT, 0);
        lv_obj_align(s.tab_text[i], LV_ALIGN_LEFT_MID, 8, -1);
        s.tab_bar[i] = theme_box(t, 0, TAB_H - 7, 1, 3, C_ACCENT);
    }
    s.frame = theme_box(root, UI_PAD, GRAPH_Y, GRAPH_W, GRAPH_H, C_BG);
    lv_obj_set_style_radius(s.frame, 3, 0);
    lv_obj_set_style_border_width(s.frame, 1, 0);
    lv_obj_set_style_border_color(s.frame, rgb(C_BORDER_OFF), 0);
    gesture_bind(s.frame, on_graph, 0);
    mod_graph_create(&s.graph, s.frame, 0, 0, GRAPH_W, GRAPH_H, ui, 0, CAP_H, 0);
    s.caption = theme_label(s.frame, &font_tag_12, C_LABEL, 1);
    lv_obj_set_pos(s.caption, IN_PAD, 3);
    s.now = theme_label(s.frame, &font_tag_12, C_TEXT, 1);
    s.empty = theme_label(s.frame, &font_body_14, C_OFF, 0);
    lv_obj_center(s.empty);
    row_list_create(&s.list, root, ROW_LIST_MAX, LIST_Y, ROW_H, on_row);
}

/* opened on ui_mods' modulator and link when given; back from the destination screen as it was */
static void enter(struct ui_ctx *ui)
{
    if (ui->mods_mod >= 0) {
        s.mod = ui->mods_mod & 3;
        s.list.sel = 0;
        s.list.top = 0;
        mod_graph_set_mod(&s.graph, s.mod);
    }
    collect(ui);
    for (int k = 0; k < s.nlinks; k++)
        if (s.links[k] == ui->mods_link)
            s.list.sel = s.nfields + k;
    ui->mods_mod = -1;
    ui->mods_link = -1;
}

static void refresh(struct ui_ctx *ui)
{
    const struct preset_mod *m = the_mod(ui);
    top_bar_sub("MODULADORES \xC2\xB7 DESTE PRESET");
    collect(ui);
    struct row_list *l = &s.list;
    int n = row_add() + 1;
    for (int r = n; r < ROW_LIST_MAX; r++)
        lv_obj_add_flag(l->rows[r], LV_OBJ_FLAG_HIDDEN);
    l->count = n;
    l->last_stop = n - 1;
    if (l->sel >= n)
        l->sel = n - 1;
    for (int r = 0; r < s.nfields; r++)
        field_title(s.fields[r], s.titles[r], sizeof s.titles[0]);
    for (int k = 0; k < s.nlinks; k++)
        lv_snprintf(s.titles[s.nfields + k], sizeof s.titles[0], "PARA");
    lv_snprintf(s.titles[row_add()], sizeof s.titles[0], "+ NOVA LIGA\xC3\x87\xC3\x83O");
    for (int r = 0; r < n; r++)
        row_list_title(l, r, s.titles[r]);
    row_list_refresh(l, value_text, ui);

    tabs(ui);
    caption(ui);
    theme_set_text(s.empty, m->type == MOD_OFF ? "DESLIGADO \xC2\xB7 GIRE O KNOB 1 EM TIPO" : "");
    lv_obj_center(s.empty);
    s.graph.outline_step = selected_step();
    lv_obj_invalidate(s.graph.box);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        row_list_step(&s.list, d);
        ui->rev++;
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB1);
    if (d) {
        knob(ui, s.list.sel, d);
        return 1;
    }
    d = ui_turned(ev, ENC_KNOB2);
    if (d) {
        choose(ui, s.mod + (d > 0 ? 1 : -1));
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU) {
        remove_link(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        activate(ui, s.list.sel);
        return 1;
    }
    if (ui_pressed(ev, BTN_EFX)) {
        choose(ui, s.mod + 1);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT) || ui_pressed(ev, BTN_CTRL)) {
        ui_back();
        return 1;
    }
    return 0;
}

/* the drawing, the value and the tabs' bars move 25 times a second while a modulator runs */
static void tick(struct ui_ctx *ui)
{
    if (time_ms() - s.last_tick < MOD_GRAPH_TICK_MS)
        return;
    s.last_tick = time_ms();
    int any = 0;
    for (int i = 0; i < MOD_COUNT; i++)
        any |= mod_of(ui, i)->type != MOD_OFF;
    mod_graph_tick(&s.graph);
    if (!any)
        return;
    if (the_mod(ui)->type != MOD_OFF)
        caption(ui);
    tabs(ui);
}

const struct screen screen_mods = { create, enter, refresh, input, 0, tick };
