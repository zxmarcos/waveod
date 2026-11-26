/*
 * Where a link goes (a link of the links screen, + NOVA LIGAÇÃO, or a link just made on the assign screen), laid
 * out like the models screen: the destinations (the chain's blocks with their category's icon, then the scenes and
 * the functions, then MIDI out; a pedal: blocks and MIDI; a modulator: blocks), what of it (a block: on / off, or for a pedal on when it
 * moves / with the group, then its parameters; the scenes; the functions; a control or program change) and when (a
 * footswitch: pressed, held, while down) or the curve (a pedal's or a MIDI CC's parameter or CC out). Picking one
 * sets the link at once, so it is heard; below, its values follow the knobs (a parameter: OFF / ON or heel / toe,
 * then when / the curve; MIDI out: number, channel, when / curve, then the CC's OFF / ON values by touch).
 *
 * Controls: MENU turns = the cursor through every choice, MENU press = take it, MENU hold = remove the link;
 * knobs: a parameter's values / when or curve, MIDI out's number / channel / when or curve, else destination /
 * choice / when; EXIT or ‹ = back (a new link left without a choice is dropped).
 * Touch: tap a destination, a choice (= take it) or a when / curve; drag a column up / down = scroll; drag a value
 * sideways = turn it.
 */
#include "model/control.h"
#include "model/param_format.h"
#include "platform/sdram.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/param_cell.h"
#include "ui/widgets/top_bar.h"
#include "ui/sdram_code.h"           /* last: this screen runs from SDRAM */

#define AREA_Y     (UI_TOPBAR_H + 4)
#define FOOT_H     52
#define FOOT_Y     (UI_H - FOOT_H)
#define AREA_H     (FOOT_Y - 4 - AREA_Y)
#define PITCH      33
#define ROWS       6                        /* objects per column (one more than fit) */
#define VIS        ((AREA_H - 6) / PITCH)
#define DEST_X     4
#define DEST_W     166
#define ITEM_X     (DEST_X + DEST_W + 4)
#define WHEN_W     108
#define WHEN_X     (UI_W - 4 - WHEN_W)
#define ITEM_W     (WHEN_X - 4 - ITEM_X)
#define ITEM_W_ALL (UI_W - 4 - ITEM_X)      /* without the when column */
#define MAX_ITEMS  48
#define DRAG_RANGE 200                      /* px for a value's whole range */
#define C_FRAME    0x2A2926
#define C_ITEM     0xC9C6BF
#define C_ACTIVE   0x161514
#define C_ACCENT_DIM 0x2E2414                /* scenes and functions picked */

enum { D_BLOCK, D_SCENE, D_FN, D_MIDI };
enum { M_NUMBER, M_CHANNEL, M_WHEN, M_OFF, M_ON, M_CELLS };    /* a MIDI out link's cells */

struct dest {
    uint8_t kind;
    int8_t slot;
};

struct item {
    uint8_t action, arg;
    int8_t k;                               /* a parameter (rig_param order) */
};

struct column {
    lv_obj_t *box, *rows[ROWS], *bar[ROWS], *icon[ROWS], *name[ROWS];
    int top;
};

static struct {
    struct column dest, item, when;
    struct param_cell cells[3];
    struct param_cell mcells[M_CELLS];
    lv_obj_t *hint;
    struct dest dests[PRESET_SLOTS + 3];
    int ndests, cur_dest;
    struct item items[MAX_ITEMS];
    int nitems, cur_item;
    int follow;                             /* the cursor moved: the next refresh scrolls both columns to it (a
                                               drag only scrolls: the cursor may leave the view) */
    int chosen;                             /* something was picked since the screen opened */
    float drag_from;
} s SDRAM_BSS;

static struct control *link(struct ui_ctx *ui)
{
    return &ui_ctl_table(ui)->c[ui->target_link];
}

static int pedal(struct ui_ctx *ui)
{
    return CK_IS_PEDAL(link(ui)->key);
}

/* a modulator: parameters of blocks only (the audio engine moves them, docs/modulators.md) */
static int modulator(struct ui_ctx *ui)
{
    return CK_IS_MOD(link(ui)->key);
}

/* a pedal or a MIDI CC: its links follow a position (heel / toe values, a curve) */
static int sweeps(struct ui_ctx *ui)
{
    return CK_SWEEPS(link(ui)->key);
}

static int midi_out(struct ui_ctx *ui)
{
    return link(ui)->action == CA_MIDI;
}

static int midi_pc(struct ui_ctx *ui)
{
    return midi_out(ui) && (link(ui)->flags & CF_PC);
}

static void edited(struct ui_ctx *ui)
{
    ui_ctl_edited(ui);
}

/* the when / curve column: a footswitch's press kinds, the curves of a pedal's or a MIDI CC's parameter or CC out;
   none for the toe switch and the rest of a pedal's or a MIDI CC's links */
static int when_count(struct ui_ctx *ui)
{
    const struct control *l = link(ui);
    if (sweeps(ui))
        return l->action == CA_PARAM || (midi_out(ui) && !midi_pc(ui)) ? CC_COUNT : 0;
    return l->key == CK_TOE ? 0 : 3;
}

static int when_value(struct ui_ctx *ui)
{
    return sweeps(ui) ? link(ui)->curve : link(ui)->when;
}

static const char *when_name(struct ui_ctx *ui, int i)
{
    static const char *const whens[3] = { "APERTAR", "SEGURAR", "PISANDO" };   /* while down */
    static const char *const curves[CC_COUNT] = { "LINEAR", "LENTA", "R\xC3\x81PIDA" };
    return sweeps(ui) ? curves[i % CC_COUNT] : whens[i % 3];
}

/* ---------------------------------------------------------------- the choices */

static void build_dests(struct ui_ctx *ui)
{
    const struct preset *p = &ui->rig->preset;
    s.ndests = 0;
    for (int pos = 0; pos < p->count && !ui->ctl_global; pos++)     /* the global table: no blocks */
        if (rig_slot_used(ui->rig, p->order[pos]) && !rig_is_routing(ui->rig, p->order[pos]))
            s.dests[s.ndests++] = (struct dest){ D_BLOCK, (int8_t)p->order[pos] };
    if (modulator(ui))
        return;
    if (!pedal(ui)) {
        s.dests[s.ndests++] = (struct dest){ D_SCENE, -1 };
        s.dests[s.ndests++] = (struct dest){ D_FN, -1 };
    }
    s.dests[s.ndests++] = (struct dest){ D_MIDI, -1 };
}

static void add_item(int action, int arg, int k)
{
    if (s.nitems < MAX_ITEMS)
        s.items[s.nitems++] = (struct item){ (uint8_t)action, (uint8_t)arg, (int8_t)k };
}

static void build_items(struct ui_ctx *ui)
{
    s.nitems = 0;
    if (s.cur_dest < 0 || s.cur_dest >= s.ndests)
        return;
    const struct dest *d = &s.dests[s.cur_dest];
    if (d->kind == D_SCENE) {
        for (int n = 0; n < SCENE_COUNT; n++)
            add_item(CA_SCENE, n, -1);
        add_item(CA_SCENE, SCENE_NEXT, -1);
        add_item(CA_SCENE, SCENE_PREV, -1);
    } else if (d->kind == D_FN) {
        for (int i = 0; i < ui_function_count; i++)
            add_item(CA_FN, ui_functions[i], -1);
    } else if (d->kind == D_MIDI) {
        add_item(CA_MIDI, 0, -1);                       /* control change */
        if (!pedal(ui))
            add_item(CA_MIDI, 1, -1);                   /* program change */
    } else {
        int key = link(ui)->key;
        if (modulator(ui)) {
            /* parameters only */
        } else if (pedal(ui)) {
            add_item(CA_BLOCK, 1, -1);                  /* on when it moves */
            if (key != CK_EXP2)
                add_item(CA_BLOCK, 0, -1);              /* with the group */
        } else {
            add_item(CA_BLOCK, 0, -1);
            add_item(CA_BLOCK, 1, -1);
        }
        for (int k = 0; k < rig_param_count(ui->rig, d->slot); k++) {
            struct rig_param p;
            if (rig_param(ui->rig, d->slot, k, &p) == 0 && p.def.idx < PRESET_KNOBS)
                add_item(CA_PARAM, 0, k);
        }
    }
}

/* the destination and choice the link has now, -1 when not among them */
static int applied_dest(struct ui_ctx *ui)
{
    const struct control *l = link(ui);
    for (int i = 0; i < s.ndests; i++) {
        const struct dest *d = &s.dests[i];
        if ((d->kind == D_BLOCK && (l->action == CA_BLOCK || l->action == CA_PARAM) && l->slot == d->slot) ||
            (d->kind == D_SCENE && l->action == CA_SCENE) || (d->kind == D_FN && l->action == CA_FN) ||
            (d->kind == D_MIDI && l->action == CA_MIDI))
            return i;
    }
    return -1;
}

static int is_applied(struct ui_ctx *ui, const struct item *it)
{
    const struct control *l = link(ui);
    if (applied_dest(ui) != s.cur_dest || it->action != l->action)
        return 0;
    if (it->action == CA_PARAM) {
        struct rig_param p;
        return control_param_index(ui->rig, l, &p) == it->k;
    }
    if (it->action == CA_MIDI)
        return it->arg == ((l->flags & CF_PC) != 0);
    return it->arg == l->arg;
}

static int applied_item(struct ui_ctx *ui)
{
    for (int i = 0; i < s.nitems; i++)
        if (is_applied(ui, &s.items[i]))
            return i;
    return -1;
}

static void set_dest(struct ui_ctx *ui, int i)
{
    s.cur_dest = i < 0 ? 0 : i >= s.ndests ? s.ndests - 1 : i;
    build_items(ui);
    int a = applied_item(ui);
    s.cur_item = a >= 0 ? a : 0;
    s.item.top = 0;
    s.follow = 1;
    ui->rev++;
}

/* the link takes the choice under the cursor */
static void take(struct ui_ctx *ui)
{
    if (s.cur_item < 0 || s.cur_item >= s.nitems)
        return;
    const struct item *it = &s.items[s.cur_item];
    struct control *l = link(ui);
    s.chosen = 1;
    if (is_applied(ui, it)) {                       /* again: keeps its values */
        ui->rev++;
        return;
    }
    int slot = s.dests[s.cur_dest].slot;
    if (it->action == CA_PARAM) {
        l->slot = (int8_t)slot;
        control_set_param(ui->rig, l, it->k);
    } else if (it->action == CA_MIDI) {
        control_set_midi(l, it->arg);
    } else {
        l->action = it->action;
        l->slot = (int8_t)(it->action == CA_BLOCK ? slot : -1);
        l->arg = it->arg;
        l->flags = 0;
    }
    if (when_count(ui) && when_value(ui) >= when_count(ui)) {
        l->when = CW_PRESS;
        l->curve = CC_LINEAR;
    }
    edited(ui);
}

/* the cursor over every choice, destination after destination */
static void step_cursor(struct ui_ctx *ui, int d)
{
    while (d) {
        int dir = d > 0 ? 1 : -1;
        int i = s.cur_item + dir;
        if (i >= 0 && i < s.nitems) {
            s.cur_item = i;
        } else {
            int next = s.cur_dest + dir;
            if (next < 0 || next >= s.ndests)
                break;
            s.cur_dest = next;
            build_items(ui);
            s.cur_item = dir > 0 ? 0 : s.nitems - 1;
            s.item.top = s.cur_item;
        }
        d -= dir;
    }
    s.follow = 1;
    ui->rev++;
}

static void set_when(struct ui_ctx *ui, int v)
{
    int n = when_count(ui);
    if (!n)
        return;
    v = (v % n + n) % n;
    if (sweeps(ui))
        link(ui)->curve = (uint8_t)v;
    else
        link(ui)->when = (uint8_t)v;
    edited(ui);
}

static int midi_field(struct ui_ctx *ui, int field)
{
    const struct control *l = link(ui);
    return field == M_NUMBER ? l->arg : field == M_CHANNEL ? l->slot & 15 :
           (int)((field == M_OFF ? l->off : l->on) + 0.5f);
}

/* a MIDI out link's number, channel, OFF / ON value */
static void set_midi(struct ui_ctx *ui, int field, int v)
{
    struct control *l = link(ui);
    int max = field == M_CHANNEL ? 15 : 127;
    v = v < 0 ? 0 : v > max ? max : v;
    if (field == M_WHEN || v == midi_field(ui, field))
        return;
    if (field == M_NUMBER)
        l->arg = (uint8_t)v;
    else if (field == M_CHANNEL)
        l->slot = (int8_t)v;
    else if (field == M_OFF)
        l->off = (float)v;
    else
        l->on = (float)v;
    edited(ui);
}

static void set_value(struct ui_ctx *ui, int which, float v)
{
    struct rig_param p;
    struct control *l = link(ui);
    if (control_param_index(ui->rig, l, &p) < 0)
        return;
    float step = p.def.step > 0 ? p.def.step : 1.0f;
    v = p.def.min + (float)(int)((v - p.def.min) / step + (v >= p.def.min ? 0.5f : -0.5f)) * step;
    v = v < p.def.min ? p.def.min : v > p.def.max ? p.def.max : v;
    float *to = which ? &l->on : &l->off;
    if (*to != v) {
        *to = v;
        edited(ui);
    }
}

static void step_value(struct ui_ctx *ui, int which, int d)
{
    struct rig_param p;
    struct control *l = link(ui);
    if (control_param_index(ui->rig, l, &p) < 0)
        return;
    set_value(ui, which, (which ? l->on : l->off) + (float)d * (p.def.step > 0 ? p.def.step : 1.0f));
}

static int valid(struct ui_ctx *ui)
{
    return ui->target_link >= 0 && ui->target_link < ui_ctl_table(ui)->count;
}

/* leaving by any way: a new link without a choice goes */
static void leave(struct ui_ctx *ui)
{
    if (ui->target_new && !s.chosen && valid(ui)) {
        preset_control_remove(ui_ctl_table(ui), ui->target_link);
        ui_changed();
    }
    ui->target_new = 0;
}

/* the link went (another preset loaded): back */
static void tick(struct ui_ctx *ui)
{
    if (!valid(ui)) {
        ui->target_new = 0;
        ui_back();
    }
}

/* ---------------------------------------------------------------- touch */

static int column_touch(const struct gesture *g, int *top, int total)
{
    static int base;
    if (g->kind == GESTURE_DRAG && g->vertical) {
        if (g->first)
            base = *top;
        int t = base - g->dy / PITCH;
        t = t > total - VIS ? total - VIS : t;
        *top = t < 0 ? 0 : t;
        return -2;
    }
    if (g->kind != GESTURE_TAP)
        return -1;
    int i = *top + (g->y - AREA_Y - 3) / PITCH;
    return i >= 0 && i < total ? i : -1;
}

static void on_dests(struct ui_ctx *ui, const struct gesture *g)
{
    int i = column_touch(g, &s.dest.top, s.ndests);
    if (i >= 0)
        set_dest(ui, i);
    else if (i == -2)
        ui->rev++;
}

static void on_items(struct ui_ctx *ui, const struct gesture *g)
{
    int i = column_touch(g, &s.item.top, s.nitems);
    if (i >= 0) {
        s.cur_item = i;
        take(ui);
    } else if (i == -2) {
        ui->rev++;
    }
}

static void on_whens(struct ui_ctx *ui, const struct gesture *g)
{
    int top = 0, i = column_touch(g, &top, when_count(ui));
    if (i >= 0)
        set_when(ui, i);
}

static void on_cell(struct ui_ctx *ui, const struct gesture *g)
{
    struct rig_param p;
    struct control *l = link(ui);
    if (g->tag > 1 || g->kind != GESTURE_DRAG || g->vertical || control_param_index(ui->rig, l, &p) < 0)
        return;
    if (g->first)
        s.drag_from = g->tag ? l->on : l->off;
    set_value(ui, g->tag, s.drag_from + (float)g->dx * (p.def.max - p.def.min) / DRAG_RANGE);
}

/* a MIDI out cell dragged sideways: its value over DRAG_RANGE px (when / curve: a step every 40 px) */
static void on_mcell(struct ui_ctx *ui, const struct gesture *g)
{
    static int from;
    if (!midi_out(ui) || g->kind != GESTURE_DRAG || g->vertical)
        return;
    if (g->tag == M_WHEN) {
        if (g->first)
            from = when_value(ui);
        if (when_value(ui) != from + g->dx / 40)
            set_when(ui, from + g->dx / 40);
        return;
    }
    int max = g->tag == M_CHANNEL ? 15 : 127;
    if (g->first)
        from = midi_field(ui, g->tag);
    set_midi(ui, g->tag, from + g->dx * (max + 1) / DRAG_RANGE);
}

/* ---------------------------------------------------------------- drawing */

static lv_obj_t *frame(lv_obj_t *root, int x, int w)
{
    lv_obj_t *o = theme_box(root, x, AREA_Y, w, AREA_H, C_BG);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, rgb(C_FRAME), 0);
    lv_obj_set_style_clip_corner(o, 1, 0);
    return o;
}

static void column_create(struct column *c, lv_obj_t *root, int x, int w, int icons, gesture_handler fn)
{
    c->box = frame(root, x, w);
    gesture_bind(c->box, fn, 0);
    for (int i = 0; i < ROWS; i++) {
        lv_obj_t *r = theme_box(c->box, 3, 3 + i * PITCH, w - 8, PITCH - 3, C_BG);
        lv_obj_set_style_radius(r, 3, 0);
        lv_obj_set_style_border_width(r, 2, 0);
        c->rows[i] = r;
        c->bar[i] = theme_box(r, 0, 4, 3, PITCH - 11, C_BG);
        c->icon[i] = icons ? theme_icon(r, 0, C_LABEL) : 0;
        if (c->icon[i])
            lv_obj_align(c->icon[i], LV_ALIGN_LEFT_MID, 9, 0);
        c->name[i] = theme_label(r, &font_value_20, C_ITEM, 0);
        lv_obj_set_width(c->name[i], w - 8 - (icons ? 40 : 14));
        lv_obj_set_height(c->name[i], lv_font_get_line_height(&font_value_20));
        lv_label_set_long_mode(c->name[i], LV_LABEL_LONG_DOT);
        lv_obj_align(c->name[i], LV_ALIGN_LEFT_MID, icons ? 36 : 9, 0);
    }
}

static void create(struct ui_ctx *ui, lv_obj_t *root)
{
    (void)ui;
    column_create(&s.dest, root, DEST_X, DEST_W, 1, on_dests);
    column_create(&s.item, root, ITEM_X, ITEM_W, 0, on_items);
    column_create(&s.when, root, WHEN_X, WHEN_W, 0, on_whens);
    theme_box(root, 0, FOOT_Y, UI_W, 1, C_LINE);
    for (int i = 0; i < 3; i++) {
        param_cell_create(&s.cells[i], root, i * UI_W / 3, FOOT_Y + 1, UI_W / 3, FOOT_H - 1, &font_value_20, i > 0);
        gesture_bind(s.cells[i].box, on_cell, i);
    }
    for (int i = 0; i < M_CELLS; i++) {
        param_cell_create(&s.mcells[i], root, i * UI_W / M_CELLS, FOOT_Y + 1, UI_W / M_CELLS, FOOT_H - 1,
                          &font_value_20, i > 0);
        lv_obj_set_width(s.mcells[i].label, UI_W / M_CELLS - 2 * 14);    /* no source tag: the whole width */
        gesture_bind(s.mcells[i].box, on_mcell, i);
    }
    s.hint = theme_label(root, &font_body_14, C_LABEL, 0);
    lv_obj_set_width(s.hint, UI_W - 2 * UI_PAD);
    lv_label_set_long_mode(s.hint, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s.hint, UI_PAD, FOOT_Y + (FOOT_H - 18) / 2);
}

static void enter(struct ui_ctx *ui)
{
    s.chosen = 0;
    build_dests(ui);
    int a = ui->target_new ? -1 : applied_dest(ui);     /* a new link starts at the first block */
    s.cur_dest = a >= 0 ? a : 0;
    build_items(ui);
    a = applied_item(ui);
    s.cur_item = a >= 0 ? a : 0;
    s.dest.top = s.cur_dest - 2;
    s.item.top = s.cur_item - 2;
    s.follow = 1;
}

/* the cursor in view when it moved (s.follow), else just the first row inside the list */
static void keep_visible(int *top, int cur, int total)
{
    if (s.follow && cur < *top)
        *top = cur;
    if (s.follow && cur > *top + VIS - 1)
        *top = cur - VIS + 1;
    if (*top > total - VIS)
        *top = total - VIS;
    if (*top < 0)
        *top = 0;
}

static uint32_t dest_colour(struct ui_ctx *ui, int i)
{
    const struct dest *d = &s.dests[i];
    return d->kind == D_BLOCK ? theme_menu_color(ui->rig->preset.slot[d->slot].menu) : C_ACCENT;
}

static void item_text(struct ui_ctx *ui, const struct item *it, char *out, unsigned size)
{
    char name[24] = "?";
    if (it->action == CA_PARAM) {
        struct rig_param p;
        if (rig_param(ui->rig, s.dests[s.cur_dest].slot, it->k, &p) == 0)
            ui_param_label(&p, name, sizeof name);
        lv_snprintf(out, size, "%s", name);
    } else if (it->action == CA_BLOCK) {
        lv_snprintf(out, size, "%s", pedal(ui) ? (it->arg ? "LIGA AO MOVER" : "COM O GRUPO") :
                                      it->arg ? "DESLIGA O BLOCO" : "LIGA O BLOCO");
    } else if (it->action == CA_SCENE) {
        if (it->arg < SCENE_COUNT) {
            rig_scene_name(ui->rig, it->arg, name);
            lv_snprintf(out, size, "%s", name);
        } else {
            lv_snprintf(out, size, "%s", it->arg == SCENE_NEXT ? "PR\xC3\x93XIMA CENA" : "CENA ANTERIOR");
        }
    } else if (it->action == CA_MIDI) {
        lv_snprintf(out, size, "%s", it->arg ? "ENVIAR PC" : "ENVIAR CC");
    } else {
        lv_snprintf(out, size, "%s", assign_fn_name(it->arg));
    }
}

static void row_show(struct column *c, int i, int show)
{
    if (show)
        lv_obj_clear_flag(c->rows[i], LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(c->rows[i], LV_OBJ_FLAG_HIDDEN);
}

/* a row: filled = picked (or the destination under the cursor), border = the cursor */
static void row_style(struct column *c, int i, int filled, int cursor, uint32_t col, uint32_t fill)
{
    lv_obj_set_style_bg_opa(c->rows[i], filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    theme_set_bg(c->rows[i], fill);
    lv_obj_set_style_border_opa(c->rows[i], cursor ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    theme_set_border(c->rows[i], filled ? col : C_OFF);
    lv_obj_set_style_bg_opa(c->bar[i], filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    theme_set_bg(c->bar[i], col);
    theme_set_color(c->name[i], filled ? col : C_ITEM);
}

static void refresh_columns(struct ui_ctx *ui)
{
    keep_visible(&s.dest.top, s.cur_dest, s.ndests);
    int applied = applied_dest(ui);
    for (int i = 0; i < ROWS; i++) {
        int k = s.dest.top + i;
        row_show(&s.dest, i, k < s.ndests);
        if (k >= s.ndests)
            continue;
        const struct dest *d = &s.dests[k];
        uint32_t col = dest_colour(ui, k);
        char name[24];
        if (d->kind == D_BLOCK) {
            int menu = ui->rig->preset.slot[d->slot].menu;
            ui_slot_name(ui, d->slot, name, sizeof name);
            theme_set_icon(s.dest.icon[i], ui_menu_icon(menu, 20), k == s.cur_dest ? col : C_LABEL);
            row_style(&s.dest, i, k == s.cur_dest, 0, col, theme_menu_dim(menu));
        } else {
            lv_snprintf(name, sizeof name, "%s", d->kind == D_SCENE ? "Cenas" : d->kind == D_FN ?
                        "Fun\xC3\xA7\xC3\xB5" "es" : "MIDI");
            theme_set_icon(s.dest.icon[i], d->kind == D_SCENE ? &icon_tab_scene : d->kind == D_FN ?
                           &icon_tab_stomp : &icon_next_16, k == s.cur_dest ? col : C_LABEL);
            row_style(&s.dest, i, k == s.cur_dest, 0, col, C_ACCENT_DIM);
        }
        theme_set_text(s.dest.name[i], name);
        if (k == applied && k != s.cur_dest)                /* where the link goes now */
            theme_set_color(s.dest.name[i], C_TEXT);
    }

    uint32_t col = s.ndests ? dest_colour(ui, s.cur_dest) : C_ACCENT;
    uint32_t dim = s.ndests && s.dests[s.cur_dest].kind == D_BLOCK ?
                   theme_menu_dim(ui->rig->preset.slot[s.dests[s.cur_dest].slot].menu) : C_ACCENT_DIM;
    lv_obj_set_style_border_color(s.item.box, rgb(col), 0);
    keep_visible(&s.item.top, s.cur_item, s.nitems);
    s.follow = 0;
    for (int i = 0; i < ROWS; i++) {
        int k = s.item.top + i;
        row_show(&s.item, i, k < s.nitems);
        if (k >= s.nitems)
            continue;
        char text[32];
        item_text(ui, &s.items[k], text, sizeof text);
        theme_set_text(s.item.name[i], text);
        int on = is_applied(ui, &s.items[k]);
        row_style(&s.item, i, on, k == s.cur_item, col, dim);
        if (on)
            theme_set_color(s.item.name[i], C_TEXT);
    }

    int nw = when_count(ui), wv = when_value(ui);
    int item_w = nw ? ITEM_W : ITEM_W_ALL;
    lv_obj_set_width(s.item.box, item_w);
    for (int i = 0; i < ROWS; i++) {
        lv_obj_set_width(s.item.rows[i], item_w - 8);
        lv_obj_set_width(s.item.name[i], item_w - 8 - 14);
    }
    if (nw)
        lv_obj_clear_flag(s.when.box, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s.when.box, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ROWS; i++) {
        row_show(&s.when, i, i < nw);
        if (i >= nw)
            continue;
        theme_set_text(s.when.name[i], when_name(ui, i));
        row_style(&s.when, i, i == wv, 0, C_ACCENT, C_ACTIVE);
        if (i == wv)
            theme_set_color(s.when.name[i], C_TEXT);
    }
}

static void show(lv_obj_t *o, int on)
{
    if (on)
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* the heel / toe labels: a pedal's, a modulator's (0 / 100 %), a MIDI CC's (its value 0 / 127), else OFF / ON */
static const char *end_label(struct ui_ctx *ui, int toe)
{
    if (pedal(ui))
        return toe ? "PONTA" : "CALCANHAR";
    if (modulator(ui))
        return toe ? "MOD 100%" : "MOD 0%";
    if (sweeps(ui))
        return toe ? "EM 127" : "EM 0";
    return toe ? "ON" : "OFF";
}

/* a MIDI out link: number, channel, when / curve, then the CC's two values */
static void refresh_midi(struct ui_ctx *ui)
{
    char text[M_CELLS][16];
    int pc = midi_pc(ui);
    lv_snprintf(text[M_NUMBER], 16, "%d", midi_field(ui, M_NUMBER));
    lv_snprintf(text[M_CHANNEL], 16, "%d", midi_field(ui, M_CHANNEL) + 1);
    lv_snprintf(text[M_OFF], 16, "%d", midi_field(ui, M_OFF));
    lv_snprintf(text[M_ON], 16, "%d", midi_field(ui, M_ON));
    for (int i = 0; i < M_CELLS; i++)
        show(s.mcells[i].box, (i < M_OFF || !pc) && (i != M_WHEN || when_count(ui)));
    param_cell_set(&s.mcells[M_NUMBER], pc ? "PC" : "CC", "", text[M_NUMBER],
                   (float)midi_field(ui, M_NUMBER) / 127.0f, C_LABEL, C_TEXT, C_ACCENT);
    param_cell_set(&s.mcells[M_CHANNEL], "CANAL", "", text[M_CHANNEL], -1, C_LABEL, C_TEXT, C_ACCENT);
    param_cell_set(&s.mcells[M_WHEN], sweeps(ui) ? "CURVA" : "QUANDO", "",
                   when_count(ui) ? when_name(ui, when_value(ui)) : "", -1, C_LABEL, C_TEXT, C_ACCENT);
    param_cell_set(&s.mcells[M_OFF], end_label(ui, 0), "", text[M_OFF], (float)midi_field(ui, M_OFF) / 127.0f,
                   C_LABEL, C_TEXT, C_ACCENT);
    param_cell_set(&s.mcells[M_ON], end_label(ui, 1), "", text[M_ON], (float)midi_field(ui, M_ON) / 127.0f,
                   C_LABEL, C_TEXT, C_ACCENT);
}

static void refresh_foot(struct ui_ctx *ui)
{
    const struct control *l = link(ui);
    struct rig_param p;
    int param = control_param_index(ui->rig, l, &p) >= 0, midi = midi_out(ui);
    for (int i = 0; i < 3; i++)
        show(s.cells[i].box, param);
    for (int i = 0; i < M_CELLS && !midi; i++)
        show(s.mcells[i].box, 0);
    if (midi) {
        lv_obj_add_flag(s.hint, LV_OBJ_FLAG_HIDDEN);
        refresh_midi(ui);
        return;
    }
    if (!param) {
        char text[64];
        lv_snprintf(text, sizeof text, "%s", ui->target_new && !s.chosen ?
                    "TOQUE ONDE A LIGA\xC3\x87\xC3\x83O VAI" : when_count(ui) ?
                    "K3 = QUANDO \xC2\xB7 MENU SEGURADO REMOVE A LIGA\xC3\x87\xC3\x83O" :
                    "MENU SEGURADO REMOVE A LIGA\xC3\x87\xC3\x83O");
        theme_set_text(s.hint, text);
        lv_obj_clear_flag(s.hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s.hint, LV_OBJ_FLAG_HIDDEN);
    uint32_t col = theme_menu_color(ui->rig->preset.slot[l->slot].menu);
    float span = p.def.max > p.def.min ? p.def.max - p.def.min : 1.0f;
    char lo[16], hi[16];
    param_format(&p.def, l->off, 0, lo, sizeof lo);
    param_format(&p.def, l->on, 0, hi, sizeof hi);
    param_cell_set(&s.cells[0], end_label(ui, 0), "", lo, (l->off - p.def.min) / span, C_LABEL, C_TEXT, col);
    param_cell_set(&s.cells[1], end_label(ui, 1), "", hi, (l->on - p.def.min) / span, C_LABEL, C_TEXT, col);
    if (when_count(ui))
        param_cell_set(&s.cells[2], sweeps(ui) ? "CURVA" : "QUANDO", "", when_name(ui, when_value(ui)), -1,
                       C_LABEL, C_TEXT, col);
    else
        param_cell_set(&s.cells[2], "", "", "", -1, C_LABEL, C_TEXT, col);
}

static void refresh(struct ui_ctx *ui)
{
    if (!valid(ui))
        return;                                     /* tick goes back */
    const struct control *l = link(ui);
    static const char *const modes[] = { "", "PRESET", "STOMP", "", "CENAS" };
    char title[48];
    const char *pre = ui->ctl_global ? "GLOBAL \xC2\xB7 " : "";
    if (l->key >= CK_TOE)
        lv_snprintf(title, sizeof title, "%s%s \xC2\xBB PARA ONDE", pre, ui_key_name(l->key));
    else
        lv_snprintf(title, sizeof title, "%s%s \xC2\xB7 %s \xC2\xBB PARA ONDE", pre, ui_key_name(l->key),
                    l->modes == CM_ALL ? "TODOS" : modes[l->modes & CM_STOMP ? 2 : l->modes & CM_SCENE ? 4 : 1]);
    top_bar_sub(title);
    build_dests(ui);
    if (s.cur_dest >= s.ndests)
        s.cur_dest = s.ndests ? s.ndests - 1 : 0;
    build_items(ui);
    if (s.cur_item >= s.nitems)
        s.cur_item = s.nitems ? s.nitems - 1 : 0;
    refresh_columns(ui);
    refresh_foot(ui);
}

static int input(struct ui_ctx *ui, const struct input_event *ev)
{
    if (!valid(ui))
        return 0;
    int d = ui_turned(ev, ENC_MENU);
    if (d) {
        step_cursor(ui, d);
        return 1;
    }
    if (ev->type == IN_HOLD && (1u << ev->id) == BTN_MENU) {
        preset_control_remove(ui_ctl_table(ui), ui->target_link);
        ui->target_new = 0;
        ui_toast("LIGA\xC3\x87\xC3\x83O REMOVIDA");
        edited(ui);
        ui_back();
        return 1;
    }
    if (ui_pressed(ev, BTN_MENU)) {
        take(ui);
        return 1;
    }
    struct rig_param p;
    int param = control_param_index(ui->rig, link(ui), &p) >= 0;
    for (int k = ENC_KNOB1; k <= ENC_KNOB3; k++) {
        d = ui_turned(ev, k);
        if (!d)
            continue;
        int i = k - ENC_KNOB1;
        if (i == 2)
            set_when(ui, when_value(ui) + d);
        else if (param)
            step_value(ui, i, ev->fast ? ev->fast : d);
        else if (midi_out(ui))
            set_midi(ui, i ? M_CHANNEL : M_NUMBER, midi_field(ui, i ? M_CHANNEL : M_NUMBER) +
                     (!i && ev->fast ? ev->fast : d));
        else if (i == 0)
            set_dest(ui, s.cur_dest + d);
        else
            step_cursor(ui, d);
        return 1;
    }
    if (ui_pressed(ev, BTN_EXIT)) {
        ui_back();
        return 1;
    }
    return 0;
}

const struct screen screen_target = { create, enter, refresh, input, leave, tick };
