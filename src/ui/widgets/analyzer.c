#include "ui/widgets/analyzer.h"
#include "dsp/spectrum.h"
#include "engine/scope.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "ui/gesture.h"
#include "ui/i18n.h"
#include "ui/theme.h"
#include <string.h>
#include "ui/sdram_code.h"           /* last: main-loop code, SDRAM */

#define MAX_W    UI_W
#define MAX_H    136
#define TOP_DB   0.0f
#define BOT_DB   (-90.0f)
#define FALL_DB_S 30.0f              /* the curves' fall per second */
#define RISE     0.7f                /* the part of a rise taken per frame */
#define QUIET_MS 150                 /* no samples this long: the curves fall to the floor */
#define CHUNK    512
#define F_LO     20.0f
#define F_HI     20000.0f

static const float grid_db[] = {-20.0f, -40.0f, -60.0f, -80.0f};
static const struct { float hz; const char *text; int major; } grid_hz[] = {
    {50, "50", 0}, {100, "100", 1}, {200, "200", 0}, {500, "500", 0}, {1000, "1k", 1}, {2000, "2k", 0},
    {5000, "5k", 0}, {10000, "10k", 1},
};
#define GRID_DB (int)(sizeof grid_db / sizeof grid_db[0])
#define GRID_HZ (int)(sizeof grid_hz / sizeof grid_hz[0])

static struct {
    lv_obj_t *box, *canvas, *legend_b, *legend_a, *readout;
    lv_obj_t *db_lbl[GRID_DB], *hz_lbl[GRID_HZ];
    int w, h, shown;
    const struct chain *chain;
    int slot;
    enum analyzer_mode mode;
    uint32_t color;
    int cursor;                                 /* column, -1 = none */
    int frozen;
    uint32_t last_frame, last_data;
    int fresh;                                  /* samples since the last analysis */
    uint32_t head;                              /* the history rings' next sample */
    uint8_t row_grid[MAX_H], col_grid[MAX_W];   /* 0 none, 1 minor, 2 major */
    float hist_b[SPEC_N], hist_a[SPEC_N];       /* rings of the last SPEC_N samples */
    float lin[SPEC_N], in_b[CHUNK], in_a[CHUNK];
    float new_b[SPEC_MAX_COLS], new_a[SPEC_MAX_COLS];
    float show_b[SPEC_MAX_COLS], show_a[SPEC_MAX_COLS];
    struct spectrum sp;
    uint32_t px[MAX_W * MAX_H] __attribute__((aligned(8)));
} a SDRAM_BSS;

static uint32_t mix(uint32_t c1, uint32_t c2, float t)     /* t of c1 over c2 */
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        float v = (float)((c1 >> sh) & 0xff) * t + (float)((c2 >> sh) & 0xff) * (1.0f - t);
        out |= (uint32_t)(v + 0.5f) << sh;
    }
    return out;
}

static inline uint32_t px32(uint32_t rgb)   /* XRGB8888 in memory: B, G, R, X */
{
    return 0xff000000u | rgb;
}

static int y_of(float db)
{
    float t = (TOP_DB - db) / (TOP_DB - BOT_DB);
    int y = (int)(t * (float)(a.h - 1) + 0.5f);
    return y < 0 ? 0 : y > a.h - 1 ? a.h - 1 : y;
}

/* ---------------------------------------------------------------- drawing */

/* Row by row the background and the dB lines (contiguous stores), then the frequency lines; column by column the
   "after" area (copied from a column of fill colours), the cursor and both curves on top. */
static void draw(void)
{
    const uint32_t bg = px32(C_BG), minor = px32(C_LINE), major = px32(C_BORDER_OFF), cur = px32(C_LABEL);
    const uint32_t fill = px32(mix(a.color, C_BG, 0.22f)), fill_grid = px32(mix(a.color, C_BG, 0.32f));
    const uint32_t line = px32(a.color), before = px32(C_LABEL);
    uint32_t fill_col[MAX_H];
    for (int y = 0; y < a.h; y++) {
        uint32_t v = a.row_grid[y] ? minor : bg, *row = a.px + y * a.w;
        for (int x = 0; x < a.w; x++)
            row[x] = v;
        fill_col[y] = a.row_grid[y] ? fill_grid : fill;
    }
    for (int x = 0; x < a.w; x++)
        if (a.col_grid[x])
            for (int y = 0; y < a.h; y++)
                a.px[y * a.w + x] = a.col_grid[x] == 2 ? major : minor;
    int pa = y_of(a.show_a[0]), pb = y_of(a.show_b[0]);
    for (int x = 0; x < a.w; x++) {
        int ya = y_of(a.show_a[x]), yb = y_of(a.show_b[x]);
        uint32_t *col = a.px + x;
        if (a.show_a[x] > BOT_DB) {
            if (a.col_grid[x])
                for (int y = ya; y < a.h; y++)
                    col[y * a.w] = fill_grid;
            else
                for (int y = ya; y < a.h; y++)
                    col[y * a.w] = fill_col[y];
        }
        if (x == a.cursor)
            for (int y = 0; y < a.h; y += 2)
                col[y * a.w] = cur;
        /* "before": a line from the last column's height to this one's */
        if (a.show_b[x] > BOT_DB || a.show_b[x > 0 ? x - 1 : 0] > BOT_DB) {
            int y0 = pb < yb ? pb : yb, y1 = pb < yb ? yb : pb;
            for (int y = y0; y <= y1; y++)
                col[y * a.w] = before;
        }
        /* "after": its top edge, two pixels thick */
        if (a.show_a[x] > BOT_DB || a.show_a[x > 0 ? x - 1 : 0] > BOT_DB) {
            int y0 = pa < ya ? pa : ya, y1 = pa < ya ? ya : pa;
            for (int y = y0; y <= y1 + 1 && y < a.h; y++)
                col[y * a.w] = line;
        }
        pa = ya;
        pb = yb;
    }
    lv_obj_invalidate(a.canvas);
}

static void legend(void)
{
    theme_set_text(a.legend_b, T(a.mode == ANALYZER_PRESET ? MSG_WIDGET_INPUT : MSG_WIDGET_BEFORE));
    theme_set_text(a.legend_a, T(a.mode == ANALYZER_PRESET ? MSG_WIDGET_OUTPUT : MSG_WIDGET_AFTER));
    theme_set_color(a.legend_a, a.color);
    lv_obj_update_layout(a.legend_a);
    lv_obj_align(a.legend_a, LV_ALIGN_TOP_RIGHT, -6, 3);
    lv_obj_align_to(a.legend_b, a.legend_a, LV_ALIGN_OUT_LEFT_MID, -10, 0);
}

static void readout(void)
{
    if (a.cursor < 0 && !a.frozen) {
        lv_obj_add_flag(a.readout, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char text[64], hz[16] = "";
    if (a.cursor >= 0) {
        float f = spectrum_hz(&a.sp, (float)a.cursor + 0.5f);
        if (f >= 1000.0f)
            lv_snprintf(hz, sizeof hz, "%d.%02d kHz", (int)(f / 1000.0f), (int)(f / 10.0f) % 100);
        else
            lv_snprintf(hz, sizeof hz, "%d Hz", (int)(f + 0.5f));
        int lb = (int)(a.show_b[a.cursor] - 0.5f), la = (int)(a.show_a[a.cursor] - 0.5f);
        lv_snprintf(text, sizeof text, "%s%s%s  %d / %d dB", a.frozen ? T(MSG_WIDGET_PAUSED) : "",
                    a.frozen ? " \xC2\xB7 " : "", hz,
                    lb < (int)BOT_DB ? (int)BOT_DB : lb, la < (int)BOT_DB ? (int)BOT_DB : la);
    } else {
        lv_snprintf(text, sizeof text, "%s", T(MSG_WIDGET_PAUSED));
    }
    theme_set_text(a.readout, text);
    lv_obj_clear_flag(a.readout, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------- set-up */

lv_obj_t *analyzer_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    a.w = w > MAX_W ? MAX_W : w;
    a.h = h > MAX_H ? MAX_H : h;
    a.slot = -100;
    a.cursor = -1;
    spectrum_init(&a.sp, a.w, F_LO, F_HI, 44100.0f);
    a.box = theme_box(parent, x, y, a.w, a.h, C_BG);
    a.canvas = lv_canvas_create(a.box);
    lv_canvas_set_buffer(a.canvas, a.px, a.w, a.h, LV_COLOR_FORMAT_XRGB8888);
    lv_obj_set_pos(a.canvas, 0, 0);
    lv_obj_remove_flag(a.canvas, LV_OBJ_FLAG_CLICKABLE);
    memset(a.row_grid, 0, sizeof a.row_grid);
    memset(a.col_grid, 0, sizeof a.col_grid);
    for (int i = 0; i < GRID_DB; i++) {
        int gy = y_of(grid_db[i]);
        a.row_grid[gy] = 1;
        char t[8];
        lv_snprintf(t, sizeof t, "%d", (int)grid_db[i]);
        a.db_lbl[i] = theme_label(a.box, &font_label_12, C_OFF, 0);
        theme_set_text(a.db_lbl[i], t);
        lv_obj_set_pos(a.db_lbl[i], 4, gy - 15);
    }
    for (int i = 0; i < GRID_HZ; i++) {
        int gx = (int)(spectrum_x(&a.sp, grid_hz[i].hz) + 0.5f);
        if (gx > 0 && gx < a.w)
            a.col_grid[gx] = (uint8_t)(grid_hz[i].major ? 2 : 1);
        a.hz_lbl[i] = theme_label(a.box, &font_label_12, C_OFF, 0);
        theme_set_text(a.hz_lbl[i], grid_hz[i].text);
        lv_obj_update_layout(a.hz_lbl[i]);
        lv_obj_set_pos(a.hz_lbl[i], gx + 3, a.h - 15);
    }
    a.legend_b = theme_label(a.box, &font_tag_12, C_LABEL, 1);
    a.legend_a = theme_label(a.box, &font_tag_12, C_TEXT, 1);
    a.readout = theme_label(a.box, &font_tag_12, C_TEXT, 0);
    lv_obj_set_pos(a.readout, 40, 3);
    for (int c = 0; c < a.w; c++)
        a.show_a[c] = a.show_b[c] = SPEC_FLOOR_DB;
    a.color = C_ACCENT;
    legend();
    readout();
    draw();
    analyzer_show(0);
    return a.box;
}

void analyzer_show(int on)
{
    a.shown = on;
    if (on)
        lv_obj_clear_flag(a.box, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(a.box, LV_OBJ_FLAG_HIDDEN);
}

static void clear(void)
{
    memset(a.hist_b, 0, sizeof a.hist_b);
    memset(a.hist_a, 0, sizeof a.hist_a);
    for (int c = 0; c < a.w; c++)
        a.show_a[c] = a.show_b[c] = SPEC_FLOOR_DB;
    a.fresh = 0;
    a.frozen = 0;
}

void analyzer_watch(const struct chain *c, int slot, enum analyzer_mode mode, uint32_t color)
{
    int moved = c != a.chain || mode != a.mode || (mode == ANALYZER_BLOCK && slot != a.slot);
    a.color = color;
    if (moved) {
        a.chain = c;
        a.slot = slot;
        a.mode = mode;
        scope_watch(c, mode == ANALYZER_PRESET ? SCOPE_PRESET : slot);
        clear();
        a.last_data = time_ms();
        readout();
    }
    legend();
    draw();
}

void analyzer_stop(void)
{
    scope_watch(0, SCOPE_OFF);
    a.chain = 0;
    a.slot = -100;
}

/* ---------------------------------------------------------------- running */

static void analyse(void)
{
    for (int pass = 0; pass < 2; pass++) {
        const float *ring = pass ? a.hist_a : a.hist_b;
        for (int i = 0; i < SPEC_N; i++)            /* oldest first */
            a.lin[i] = ring[(a.head + (uint32_t)i) & (SPEC_N - 1)];
        spectrum_run(&a.sp, a.lin, pass ? a.new_a : a.new_b);
    }
}

static void settle(const float *nb, const float *na, float dt)
{
    float fall = FALL_DB_S * dt;
    for (int c = 0; c < a.w; c++) {
        float *s[2] = {&a.show_b[c], &a.show_a[c]};
        float n[2] = {nb ? nb[c] : SPEC_FLOOR_DB, na ? na[c] : SPEC_FLOOR_DB};
        for (int k = 0; k < 2; k++) {
            if (n[k] > *s[k])
                *s[k] += RISE * (n[k] - *s[k]);
            else
                *s[k] = *s[k] - fall > n[k] ? *s[k] - fall : n[k];
        }
    }
}

static int above_floor(void)
{
    for (int c = 0; c < a.w; c++)
        if (a.show_a[c] > BOT_DB || a.show_b[c] > BOT_DB)
            return 1;
    return 0;
}

void analyzer_tick(void)
{
    if (!a.shown || !a.chain)
        return;
    uint32_t now = time_ms();
    int n;
    while ((n = scope_read(a.in_b, a.in_a, CHUNK)) > 0) {
        for (int i = 0; i < n; i++, a.head++) {
            a.hist_b[a.head & (SPEC_N - 1)] = a.in_b[i];
            a.hist_a[a.head & (SPEC_N - 1)] = a.in_a[i];
        }
        a.fresh += n;
        a.last_data = now;
    }
    if (a.frozen || now - a.last_frame < ANALYZER_FRAME_MS)
        return;
    float dt = (float)(now - a.last_frame) * 0.001f;
    if (dt > 0.1f)
        dt = 0.1f;
    if (a.fresh >= ANALYZER_HOP) {
        analyse();
        settle(a.new_b, a.new_a, dt);
        a.fresh = 0;
    } else if (now - a.last_data > QUIET_MS && above_floor()) {
        settle(0, 0, dt);                           /* nothing comes: down to the floor */
    } else {
        return;
    }
    a.last_frame = now;
    draw();
    if (a.cursor >= 0)
        readout();
}

int analyzer_touch(const struct gesture *g)
{
    int x = g->x - lv_obj_get_x(a.box), y = g->y - lv_obj_get_y(a.box);
    if (g->kind == GESTURE_LONG) {
        a.frozen = !a.frozen;
        readout();
        return 0;
    }
    if (g->kind != GESTURE_TAP)
        return 0;
    lv_area_t r;
    lv_obj_get_coords(a.legend_b, &r);
    if (y < 24 && g->x >= r.x1 - 8)                 /* the legend: the other mode */
        return 1;
    if (x < 0 || x >= a.w)
        return 0;
    a.cursor = a.cursor >= 0 && (x - a.cursor < 12 && a.cursor - x < 12) ? -1 : x;
    readout();
    draw();
    return 0;
}
