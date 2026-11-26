#include "ui/widgets/mod_graph.h"
#include "engine/modulators.h"
#include "ui/theme.h"
#include "ui/sdram_code.h"           /* last: drawn from SDRAM like the screens */

#define CYCLES 2                    /* LFO cycles drawn */

struct area { int l, r, t, b; };

static const struct preset_mod *mod_of(const struct mod_graph *g)
{
    return &g->ui->rig->preset.mods.m[g->mod & 3];
}

/* a shape whose drawing is the shape itself (not what it did: the random ones) */
static int drawn_shape(const struct preset_mod *m)
{
    return m->type == MOD_LFO && m->shape != MS_RANDOM && m->shape != MS_SMOOTH;
}

/* an LFO shape at p (0..1 of a cycle), as engine/modulators.c plays it */
static float shape_at(int shape, float p)
{
    switch (shape) {
    case MS_TRIANGLE: return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
    case MS_SQUARE:   return p < 0.5f ? 1.0f : 0.0f;
    case MS_SAW_UP:   return p;
    case MS_SAW_DOWN: return 1.0f - p;
    default:                                    /* a sine from its bottom */
        return 0.5f - 0.5f * (float)lv_trigo_cos((int16_t)(p * 360.0f)) / (float)LV_TRIGO_SIN_MAX;
    }
}

/* the corners of one cycle of the straight shapes, (p, value) pairs; 0 = the shape is curved (sampled) */
static int corners(int shape, const float **out)
{
    static const float tri[] = { 0, 0, 0.5f, 1, 1, 0 };
    static const float square[] = { 0, 1, 0.5f, 1, 0.5f, 0, 1, 0 };
    static const float up[] = { 0, 0, 1, 1 };
    static const float down[] = { 0, 1, 1, 0 };
    switch (shape) {
    case MS_TRIANGLE: *out = tri; return 3;
    case MS_SQUARE:   *out = square; return 4;
    case MS_SAW_UP:   *out = up; return 2;
    case MS_SAW_DOWN: *out = down; return 2;
    default:          return 0;
    }
}

static int ypos(const struct area *a, float v)
{
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    return a->b - (int)(v * (float)(a->b - a->t) + 0.5f);
}

static void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, uint32_t colour, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.width = w;
    d.color = rgb(colour);
    d.opa = opa;
    d.round_start = d.round_end = 1;
    d.p1.x = x1, d.p1.y = y1, d.p2.x = x2, d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void rect(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t fill, uint32_t border, int radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = rgb(fill);
    d.bg_opa = fill == 0 ? LV_OPA_TRANSP : LV_OPA_COVER;
    d.border_color = rgb(border);
    d.border_width = border ? 1 : 0;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static void dot(const struct mod_graph *g, lv_layer_t *layer, int x, int y)
{
    int r = g->small ? 3 : 4;
    rect(layer, x - r, y - r, x + r, y + r, C_ACCENT, C_TEXT, LV_RADIUS_CIRCLE);
}

static void draw_lfo(const struct mod_graph *g, lv_layer_t *layer, const struct area *a, const struct preset_mod *m)
{
    int w = a->r - a->l, px = 0, py = 0, first = 1, lw = g->small ? 1 : 2;
    const float *c;
    int nc = corners(m->shape, &c);
    if (nc) {                                   /* the corners, cycle after cycle (the jumps drawn upright) */
        for (int cy = 0; cy < CYCLES; cy++)
            for (int k = 0; k < nc; k++) {
                int x = a->l + (int)(((float)cy + c[2 * k]) * (float)w / (float)CYCLES + 0.5f);
                int y = ypos(a, c[2 * k + 1]);
                if (!first)
                    line(layer, px, py, x, y, lw, C_ACCENT, LV_OPA_COVER);
                px = x, py = y, first = 0;
            }
    } else {                                    /* the sine, a point every 3 px */
        int n = w / 3;
        px = a->l, py = ypos(a, shape_at(m->shape, 0.0f));
        for (int i = 1; i <= n; i++) {
            float t = (float)i * (float)CYCLES / (float)n, p = t - (float)(int)t;
            int x = a->l + i * w / n, y = ypos(a, shape_at(m->shape, p));
            line(layer, px, py, x, y, lw, C_ACCENT, LV_OPA_COVER);
            px = x, py = y;
        }
    }
    if (m->phase) {                             /* where it starts when the preset loads */
        int x = a->l + (int)((float)m->phase * 0.01f * (float)w / (float)CYCLES);
        line(layer, x, a->b - 6, x, a->b + 3, 2, C_LABEL, LV_OPA_COVER);
    }
    float pos = ((float)(mods_cycle(g->mod) % CYCLES) + mods_phase(g->mod)) / (float)CYCLES;
    int x = a->l + (int)(pos * (float)w + 0.5f);
    line(layer, x, a->t, x, a->b, 1, C_TEXT, LV_OPA_40);
    dot(g, layer, x, ypos(a, mods_value(g->mod)));
}

/* what the modulator did over the last seconds, the newest on the right */
static void draw_history(const struct mod_graph *g, lv_layer_t *layer, const struct area *a)
{
    if (g->hist_n < 1)
        return;
    int w = a->r - a->l, px = 0, py = 0, lw = g->small ? 1 : 2;
    for (int k = 0; k < g->hist_n; k++) {
        float v = g->hist[(g->hist_at + MOD_GRAPH_HIST - g->hist_n + k) % MOD_GRAPH_HIST];
        int x = a->r - (g->hist_n - 1 - k) * w / (MOD_GRAPH_HIST - 1), y = ypos(a, v);
        if (k)
            line(layer, px, py, x, y, lw, C_ACCENT, LV_OPA_COVER);
        px = x, py = y;
    }
    dot(g, layer, px, py);
}

static void draw_seq(const struct mod_graph *g, lv_layer_t *layer, const struct area *a, const struct preset_mod *m)
{
    int n = m->steps >= 1 && m->steps <= MOD_STEPS ? m->steps : 1, w = a->r - a->l;
    int cur = (int)(mods_cycle(g->mod) % (uint32_t)n), gap = g->small && n > 8 ? 1 : 2;
    for (int i = 0; i < n; i++) {
        int x1 = a->l + i * w / n + gap, x2 = a->l + (i + 1) * w / n - gap;
        int y = ypos(a, (float)(m->step[i] > 100 ? 100 : m->step[i]) * 0.01f);
        rect(layer, x1, a->t, x2, a->b, C_LINE, i == g->outline_step ? C_TEXT : 0, 2);
        rect(layer, x1, y < a->b - 2 ? y : a->b - 2, x2, a->b, i == cur ? C_ACCENT : C_WIRE, 0, 2);
    }
    if (m->glide) {                             /* the ramp into each step */
        float gl = (float)(m->glide > 100 ? 100 : m->glide) * 0.01f;
        for (int i = 0; i < n; i++) {
            int x0 = a->l + i * w / n, x1 = a->l + (i + 1) * w / n;
            int xg = x0 + (int)(gl * (float)(x1 - x0));
            int yu = ypos(a, (float)m->step[(i + n - 1) % n] * 0.01f), yv = ypos(a, (float)m->step[i] * 0.01f);
            line(layer, x0, yu, xg, yv, 1, C_TEXT, LV_OPA_70);
            line(layer, xg, yv, x1, yv, 1, C_TEXT, LV_OPA_70);
        }
    }
    int x = a->l + (int)(((float)cur + mods_phase(g->mod)) * (float)w / (float)n + 0.5f);
    line(layer, x, a->t, x, a->b, 1, C_TEXT, LV_OPA_60);
    dot(g, layer, x, ypos(a, mods_value(g->mod)));
}

static void area_of(const struct mod_graph *g, struct area *out)
{
    lv_area_t a;
    lv_obj_get_coords(g->box, &a);
    out->l = a.x1 + g->pad;
    out->r = a.x2 - g->pad;
    out->t = a.y1 + g->top;
    out->b = a.y2 - g->pad;
}

static void draw(lv_event_t *e)
{
    const struct mod_graph *g = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const struct preset_mod *m = mod_of(g);
    if (m->type == MOD_OFF)
        return;
    struct area a;
    area_of(g, &a);
    for (int k = 0; k <= 2; k++) {              /* 0, 50 and 100 % */
        int y = ypos(&a, (float)k * 0.5f);
        line(layer, a.l, y, a.r, y, 1, C_LINE, LV_OPA_COVER);
    }
    if (m->type == MOD_SEQ)
        draw_seq(g, layer, &a, m);
    else if (drawn_shape(m))
        draw_lfo(g, layer, &a, m);
    else
        draw_history(g, layer, &a);
}

void mod_graph_create(struct mod_graph *g, lv_obj_t *parent, int x, int y, int w, int h, struct ui_ctx *ui, int mod,
                      int top, int small)
{
    g->box = theme_box(parent, x, y, w, h, C_BG);
    lv_obj_set_style_bg_opa(g->box, LV_OPA_TRANSP, 0);
    g->ui = ui;
    g->mod = mod & 3;
    g->top = top;
    g->pad = small ? 2 : 6;
    g->small = small;
    g->outline_step = -1;
    g->hist_n = g->hist_at = 0;
    lv_obj_add_event_cb(g->box, draw, LV_EVENT_DRAW_MAIN, g);
}

void mod_graph_set_mod(struct mod_graph *g, int mod)
{
    if ((mod & 3) != g->mod)
        g->hist_n = 0;
    g->mod = mod & 3;
    lv_obj_invalidate(g->box);
}

void mod_graph_tick(struct mod_graph *g)
{
    const struct preset_mod *m = mod_of(g);
    if (m->type != g->hist_type || m->shape != g->hist_shape) {
        g->hist_type = m->type;
        g->hist_shape = m->shape;
        g->hist_n = 0;
    }
    if (m->type == MOD_OFF)
        return;
    g->hist[g->hist_at] = mods_value(g->mod);
    g->hist_at = (g->hist_at + 1) % MOD_GRAPH_HIST;
    if (g->hist_n < MOD_GRAPH_HIST)
        g->hist_n++;
    lv_obj_invalidate(g->box);
}

int mod_graph_step_at(const struct mod_graph *g, int x)
{
    const struct preset_mod *m = mod_of(g);
    struct area a;
    area_of(g, &a);
    if (m->type != MOD_SEQ || x < a.l || x > a.r)
        return -1;
    int n = m->steps >= 1 && m->steps <= MOD_STEPS ? m->steps : 1;
    int i = (x - a.l) * n / (a.r - a.l + 1);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

int mod_graph_value_at(const struct mod_graph *g, int y)
{
    struct area a;
    area_of(g, &a);
    float v = (float)(a.b - y) / (float)(a.b - a.t);
    int pc = (int)(v * 20.0f + 0.5f) * 5;
    return pc < 0 ? 0 : pc > 100 ? 100 : pc;
}
