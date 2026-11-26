#include "ui/ui.h"
#include "model/modulation.h"
#include "platform/display.h"
#include "platform/time.h"
#include "ui/alert.h"
#include "ui/boot.h"
#include "engine/drums.h"
#include "ui/diag_panel.h"
#include "ui/footswitch.h"
#include "ui/leds.h"
#include "model/expression.h"
#include "ui/gesture.h"
#include "ui/images/icons.h"
#include "ui/theme.h"
#include "ui/ui_internal.h"
#include "ui/widgets/top_bar.h"
#include <string.h>

#define TOAST_MS      2500
#define METER_MS      50
#define STATS_MS      250
#define DRAG_RANGE_PX 200           /* a drag this long sweeps a whole parameter range */

static struct ui_ctx ctx;
static struct expression pedals;
static lv_obj_t *roots[SCR_COUNT];
static uint32_t seen_rev[SCR_COUNT];
static lv_obj_t *toast_box, *toast_text;
static uint32_t toast_until, meter_at, stats_at;
static uint32_t loads_seen;                 /* rig->loads the UI has caught up with */

static const struct screen *const screens[SCR_COUNT] = {
    [SCR_PRESET] = &screen_preset,
    [SCR_STOMP] = &screen_stomp,
    [SCR_CHAIN] = &screen_chain,
    [SCR_EDIT] = &screen_edit,
    [SCR_BROWSE] = &screen_browse,
    [SCR_SAVE] = &screen_save,
    [SCR_NAME] = &screen_name,
    [SCR_TUNER] = &screen_tuner,
    [SCR_SETTINGS] = &screen_settings,
    [SCR_EQ] = &screen_eq,
    [SCR_ASSIGN] = &screen_assign,
    [SCR_LOOPER] = &screen_looper,
    [SCR_DRUM] = &screen_drum,
    [SCR_SCENE] = &screen_scene,
    [SCR_CONTROLS] = &screen_controls,
    [SCR_LINK] = &screen_link,
    [SCR_CONTROL_MAP] = &screen_control_map,
    [SCR_TARGET] = &screen_target,
    [SCR_LATENCY] = &screen_latency,
    [SCR_MIC] = &screen_mic,
    [SCR_EXP_CAL] = &screen_exp_cal,
    [SCR_PRESETS] = &screen_presets,
    [SCR_MENU] = &screen_menu,
};

/* ---------------------------------------------------------------- display driver */

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    if (lv_display_flush_is_last(disp))
        display_show(px == (uint8_t *)display_fb(1) ? 1 : 0);
    lv_display_flush_ready(disp);
}

static void display_init(void)
{
    lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(disp, display_fb(0), display_fb(1), DISPLAY_W * DISPLAY_H * 4,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush);
}

/* ---------------------------------------------------------------- helpers for the screens */

const char *ui_slot_tag(const struct ui_ctx *ui, int slot)
{
    const struct rig *r = ui->rig;
    if (!rig_slot_used(r, slot))
        return "+";
    if (rig_is_routing(r, slot)) {
        uint32_t id = r->preset.slot[slot].id;
        return id == ID_SPLIT ? "SPLIT" : id == ID_MERGE ? "MERGE" : "B";
    }
    return lib_menu_tag(r->preset.slot[slot].menu);
}

void ui_slot_name(const struct ui_ctx *ui, int slot, char *out, unsigned size)
{
    struct catalog_item item;
    if (rig_slot_item(ui->rig, slot, &item) == 0)
        lv_snprintf(out, size, "%s", item.name);
    else
        lv_snprintf(out, size, "Slot vazio");
}

/* the design's icon of each library menu (FX = filter, IR = cabinet, Clone = amp, loop / send / return) */
static const uint8_t menu_icon[LIB_MAX_MENUS] = {
    [MENU_DYN] = ICON_DYN, [MENU_FX] = ICON_FLT,  [MENU_WAH] = ICON_WAH,   [MENU_DRV] = ICON_DRV,
    [MENU_AMP] = ICON_AMP, [MENU_CAB] = ICON_CAB, [MENU_IR] = ICON_CAB,    [MENU_EQ] = ICON_EQ,
    [MENU_MOD] = ICON_MOD, [MENU_DLY] = ICON_DLY, [MENU_RVB] = ICON_RVB,   [MENU_CLONE] = ICON_AMP,
    [MENU_LOOP] = ICON_SR, [MENU_SND] = ICON_SR,  [MENU_RTN] = ICON_SR,    [MENU_VOL] = ICON_VOL,
};

const lv_image_dsc_t *ui_menu_icon(int menu, int size)
{
    int i = menu >= 0 && menu < LIB_MAX_MENUS ? menu_icon[menu] : ICON_PLUS;
    return size <= 15 ? icons_15[i] : icons_20[i];
}

const char *ui_menu_name(int menu)
{
    static const char *const names[LIB_MAX_MENUS] = {
        "Din\xC3\xA2mica", "Efeitos", "Wah", "Drive", "Amp", "Gabinete", "IR", "EQ",
        "Modula\xC3\xA7\xC3\xA3o", "Delay", "Reverb", "Clone", "FX Loop", "Send", "Return", "Volume",
    };
    return menu >= 0 && menu < LIB_MAX_MENUS ? names[menu] : "?";
}

int ui_menus(const struct ui_ctx *ui, int *out)
{
    int n = 0;
    for (int m = 0; m < ui->rig->cat.lib.menus && m < LIB_MAX_MENUS; m++)
        if (catalog_count(&ui->rig->cat, m) > 0)
            out[n++] = m;
    return n;
}

void ui_lane_name(const struct ui_ctx *ui, int pos, char *out, unsigned size)
{
    if (pos < 0 || pos >= ui->topo.count) {
        lv_snprintf(out, size, "NOVO BLOCO");
        return;
    }
    ui_cell_lane(ui, ui->topo.node[pos].cell, out, size);
}

void ui_cell_lane(const struct ui_ctx *ui, int cell, char *out, unsigned size)
{
    const struct preset_route *r = &ui->rig->preset.route;
    int lane = route_lane(r, cell), col = cell % ROUTE_COLS, end = route_lanes_end(r);
    if (lane == ROUTE_HIDDEN) {
        out[0] = 0;
        return;
    }
    if (r->mode == ROUTE_SERIAL) {
        lv_snprintf(out, size, "S\xC3\x89RIE \xC2\xB7 %d", cell + 1);
        return;
    }
    static const char *const names[2][4] = {
        { "PR\xC3\x89", "PISTA A", "PISTA B", "P\xC3\x93S" },
        { "PR\xC3\x89", "SA\xC3\x8D" "DA L", "SA\xC3\x8D" "DA R", "P\xC3\x93S" },
    };
    int n = lane == ROUTE_PRE ? col + 1 : lane == ROUTE_POST ? col - end + 1 : col - r->split + 1;
    lv_snprintf(out, size, "%s \xC2\xB7 %d", names[r->mode == ROUTE_DUAL][lane], n);
}

void ui_preset_title(const struct ui_ctx *ui, char *out, unsigned size)
{
    char label[8];
    preset_label(ui->rig->index, label);
    char name[PRESET_NAME_LEN + 1];
    unsigned i = 0;
    for (; ui->rig->preset.name[i] && i < PRESET_NAME_LEN; i++) {
        char c = ui->rig->preset.name[i];
        name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    name[i] = 0;
    lv_snprintf(out, size, "%s%s %s", label, ui->rig->dirty ? "*" : "", name);
}

int ui_turned(const struct input_event *ev, int enc)
{
    return ev->type == IN_TURN && ev->id == enc ? ev->delta : 0;
}

int ui_pressed(const struct input_event *ev, uint32_t button)
{
    return ev->type == IN_PRESS && (1u << ev->id) == button;
}

void ui_param_label(const struct rig_param *p, char *out, unsigned size)
{
    lv_snprintf(out, size, "%s", p->def.name);
    for (char *c = out; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
}

float ui_param_frac(const struct rig_param *p)
{
    float span = p->def.max - p->def.min;
    return span > 0 ? (p->value - p->def.min) / span : 0.0f;
}

/* ---------------------------------------------------------------- touch helpers for the screens */

void ui_press(uint32_t button)
{
    struct input_event ev = { IN_PRESS, input_button_id(button), 0, 0 };
    ui_input(&ev);
}


static int round_div(float v)
{
    return (int)(v >= 0 ? v + 0.5f : v - 0.5f);
}

/* right or up = more */
static int drag_delta(const struct gesture *g)
{
    return g->vertical ? -g->dy : g->dx;
}

void ui_param_drag(struct ui_ctx *ui, int slot, int k, const struct gesture *g)
{
    static float start;
    struct rig_param p;
    if (g->kind != GESTURE_DRAG || slot < 0 || rig_param(ui->rig, slot, k, &p) != 0)
        return;
    if (g->first)
        start = p.value;
    float step = p.def.step > 0 ? p.def.step : 1.0f;
    float target = start + (p.def.max - p.def.min) * (float)drag_delta(g) / DRAG_RANGE_PX;
    int steps = round_div((target - p.value) / step);
    if (steps) {
        rig_param_nudge(ui->rig, slot, k, steps);
        ui_changed();
    }
}

void ui_volume_drag(struct ui_ctx *ui, const struct gesture *g)
{
    static int start;
    if (g->kind != GESTURE_DRAG)
        return;
    if (g->first)
        start = ui->rig->preset.volume;
    int v = start + round_div(100.0f * (float)drag_delta(g) / DRAG_RANGE_PX);
    if (v != ui->rig->preset.volume) {
        rig_set_volume(ui->rig, v);
        ui_changed();
    }
}

void ui_bpm_drag(struct ui_ctx *ui, const struct gesture *g)
{
    static int start;
    if (g->kind != GESTURE_DRAG)
        return;
    if (g->first)
        start = ui->rig->preset.bpm;
    int v = start + round_div(260.0f * (float)drag_delta(g) / DRAG_RANGE_PX);
    if (v != ui->rig->preset.bpm) {
        rig_set_bpm(ui->rig, v);
        ui_changed();
    }
}

/* ---------------------------------------------------------------- screen manager */

void ui_changed(void)
{
    assign_build(ctx.rig, &ctx.assign);
    topology_build(ctx.rig, &ctx.topo);
    if (ctx.sel > ctx.rig->preset.count)
        ctx.sel = ctx.rig->preset.count;
    ctx.rev++;
}

/* the control screens on the global table: left for another screen, the table is stored when it changed */
static void global_leave(enum screen_id id)
{
    if (!ctx.ctl_global || id == SCR_CONTROL_MAP || id == SCR_CONTROLS || id == SCR_TARGET || id == SCR_NAME)
        return;
    ctx.ctl_global = 0;
    if (ctx.global_dirty) {
        ctx.global_dirty = 0;
        if (rig_store_global(ctx.rig) != 0)
            ui_toast("Controles globais: falha ao gravar");
    }
}

static void show(enum screen_id id)
{
    if (ctx.current != id && screens[ctx.current]->leave)
        screens[ctx.current]->leave(&ctx);
    global_leave(id);
    top_bar_close();                                /* the mode chooser belongs to the screen it was opened on */
    ctx.current = id;
    ctx.learned = 0;                                /* learning belongs to the screen that started it */
    if (id == SCR_PRESET || id == SCR_STOMP || id == SCR_SCENE)       /* the footswitches follow the tab */
        footswitch_set_mode(&ctx, id == SCR_SCENE ? FS_MODE_SCENE : id == SCR_STOMP ? FS_MODE_STOMP : FS_MODE_PRESET);
    if (screens[id]->enter)
        screens[id]->enter(&ctx);
    screens[id]->refresh(&ctx);
    seen_rev[id] = ctx.rev;
    lv_screen_load(roots[id]);
}

void ui_go(enum screen_id id)
{
    if (id == ctx.current)
        return;
    if (ctx.depth == UI_STACK) {                    /* forget the oldest step */
        memmove(ctx.stack, ctx.stack + 1, (UI_STACK - 1) * sizeof ctx.stack[0]);
        ctx.depth--;
    }
    ctx.stack[ctx.depth++] = ctx.current;
    show(id);
}

void ui_back(void)
{
    if (ctx.depth) {
        show(ctx.stack[--ctx.depth]);
        return;
    }
    /* a tab: up to the tab of the footswitch mode (home), which stays: going back never changes the mode */
    if (ctx.current != ui_mode_tab())
        show(ui_mode_tab());
}

void ui_tab(enum screen_id id)
{
    ctx.depth = 0;
    show(id);
}

void ui_edit(int pos)
{
    if (pos >= ctx.topo.count) {                    /* "+": choose the effect of a new block */
        ctx.browse_slot = -1;
        ctx.browse_cell = -1;
        ctx.browse_pos = ctx.rig->preset.count;
        ui_go(SCR_BROWSE);
        return;
    }
    ctx.sel = pos;
    ctx.edit_row = 0;
    ctx.edit_knob = 0;
    if (ctx.current == SCR_EDIT)
        ctx.rev++;
    else
        ui_go(SCR_EDIT);
}

enum screen_id ui_mode_tab(void)
{
    return ctx.scene_mode ? SCR_SCENE : ctx.stomp_mode ? SCR_STOMP : SCR_PRESET;
}

void ui_set_mode(int mode)
{
    static const enum screen_id tab[3] = { [FS_MODE_PRESET] = SCR_PRESET, [FS_MODE_STOMP] = SCR_STOMP,
                                           [FS_MODE_SCENE] = SCR_SCENE };
    static const char *const name[3] = { "MODO PRESET", "MODO STOMP", "MODO CENAS" };
    if (mode < 0 || mode > 2)
        return;
    if (ctx.current == SCR_PRESET || ctx.current == SCR_STOMP || ctx.current == SCR_SCENE) {
        ui_tab(tab[mode]);
        return;
    }
    if (mode == footswitch_mode(&ctx))
        return;
    footswitch_set_mode(&ctx, mode);            /* another screen (chain, edit...): it stays */
    ui_toast(name[mode]);
    ctx.rev++;
}

void ui_name_scene(int n)
{
    ctx.name_scene = n;
    ctx.name_key = -1;
    ui_go(SCR_NAME);
}

void ui_name_key(int key)
{
    ctx.name_scene = -1;
    ctx.name_key = key;
    ui_go(SCR_NAME);
}

void ui_toast(const char *text)
{
    theme_set_text(toast_text, text);
    lv_obj_clear_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
    toast_until = time_ms() + TOAST_MS;
    alert_keep_on_top();
}

static char pending_notice[64];             /* a notice given during the start-up animation */

void ui_notice(const char *text)
{
    if (boot_running())
        lv_snprintf(pending_notice, sizeof pending_notice, "%s", text);
    else
        ui_toast(text);
}

void ui_alert(const char *title, const char *text)
{
    alert_show(title, text);                /* the top layer stays hidden while the animation runs */
}

static void toast_create(void)
{
    toast_box = theme_box(lv_layer_top(), 0, 0, 10, 30, C_LINE);
    lv_obj_set_size(toast_box, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(toast_box, 14, 0);
    lv_obj_set_style_border_width(toast_box, 1, 0);
    lv_obj_set_style_border_color(toast_box, rgb(C_OFF), 0);
    lv_obj_align(toast_box, LV_ALIGN_BOTTOM_MID, 0, -40);
    toast_text = theme_label(toast_box, &font_body_14, C_TEXT, 0);
    lv_obj_center(toast_text);
    lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
}

void ui_open_tuner(void)
{
    ui_go(SCR_TUNER);
}

void ui_open_save(void)
{
    ctx.save_target = ctx.rig->index;
    ui_go(SCR_SAVE);
}

struct ui_ctx *ui_context(void)
{
    return &ctx;
}

void ui_learn(ui_learn_fn fn)
{
    ctx.learned = fn;
    if (fn)
        expression_learn_start(&pedals);
    ctx.rev++;
}

void ui_link_param(int slot, int k)
{
    if (!rig_slot_used(ctx.rig, slot) || rig_is_routing(ctx.rig, slot) || k < 0 ||
        k >= rig_param_count(ctx.rig, slot))
        return;
    ctx.link_slot = slot;
    ctx.link_param = k;
    if (ctx.current == SCR_LINK)
        ctx.rev++;
    else
        ui_go(SCR_LINK);
}

void ui_controls(int key, int mode, int link)
{
    ctx.controls_key = key;
    ctx.controls_mode = mode;
    ctx.controls_link = link;
    if (ctx.current == SCR_CONTROLS)
        screen_controls.enter(&ctx);
    else
        ui_go(SCR_CONTROLS);
    ctx.rev++;
}

void ui_target(int link, int is_new)
{
    if (link < 0 || link >= ui_ctl_table(&ctx)->count)
        return;
    ctx.target_link = link;
    ctx.target_new = is_new;
    ui_go(SCR_TARGET);
}

void ui_global_controls(void)
{
    ctx.ctl_global = 1;                             /* kept while the control screens are shown */
    ui_go(SCR_CONTROL_MAP);
}

struct preset_controls *ui_ctl_table(struct ui_ctx *ui)
{
    return ui->ctl_global ? &ui->rig->global : &ui->rig->preset.controls;
}

void ui_ctl_edited(struct ui_ctx *ui)
{
    if (ui->ctl_global)
        ui->global_dirty = 1;
    else
        rig_set_dirty(ui->rig, 1);
    modulation_update(ui->rig, 0);           /* a modulator's links or settings may have changed */
    ui_changed();
}

/* the original's list without the CTRL groups and the slots (blocks are links now) */
const uint8_t ui_functions[] = {
    FSF_TAP, FSF_TUNER, FSF_LOOPER, FSF_DRUM, FSF_DRUM_PREV, FSF_DRUM_NEXT, FSF_EXP_AB, FSF_PRESET_DOWN,
    FSF_PRESET_UP, FSF_BANK_DOWN, FSF_BANK_UP, FSF_BANK_MENU, FSF_PRESET_A, FSF_PRESET_A + 1, FSF_PRESET_A + 2,
    FSF_PRESET_D, FSF_MODE,
};
const int ui_function_count = (int)(sizeof ui_functions / sizeof ui_functions[0]);

const char *ui_key_name(int key)
{
    static const char *const names[CK_COUNT] = {
        "A", "B", "C", "D", "BANK \xE2\x88\x92", "BANK +", "A + B", "B + C", "C + D", "BANK \xE2\x88\x92 E +",
        "PRESET ATIVO", "BICO DO EXP 1", "EXP 1A", "EXP 1B", "EXP 2", "MOD 1", "MOD 2", "MOD 3", "MOD 4",
    };
    _Static_assert(CK_COUNT == 19, "a name for every key");
    if (key >= 0 && CK_IS_MIDI(key)) {          /* two at a time ("CC 1 · CC 2") */
        static char midi[2][12];
        static int turn;
        char *out = midi[turn++ & 1];
        lv_snprintf(out, sizeof midi[0], "MIDI CC %d", key - CK_MIDI);
        return out;
    }
    return key >= 0 && key < CK_COUNT ? names[key] : "?";
}

void ui_init(struct rig *rig, struct settings *settings, struct globals *globals)
{
    lv_init();
    lv_tick_set_cb(time_ms);
    display_init();
    memset(&ctx, 0, sizeof ctx);
    gesture_init(&ctx);
    ctx.rig = rig;
    ctx.settings = settings;
    ctx.globals = globals;
    ctx.pedals = &pedals;
    ctx.browse_slot = -1;
    ctx.browse_cell = -1;
    ctx.name_scene = -1;
    ctx.name_key = -1;
    ctx.controls_key = -1;
    ctx.assign_row = -1;
    loads_seen = rig->loads;
    rig->scene_auto = !settings->scene_manual;
    top_bar_init();
    gesture_cursor_set(settings->touch_cursor);
    ui_changed();
    for (int i = 0; i < SCR_COUNT; i++) {
        roots[i] = lv_obj_create(NULL);
        lv_obj_remove_style_all(roots[i]);
        lv_obj_set_style_bg_color(roots[i], rgb(C_BG), 0);
        lv_obj_set_style_bg_opa(roots[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(roots[i], LV_OBJ_FLAG_SCROLLABLE);
        screens[i]->create(&ctx, roots[i]);
    }
    toast_create();
    expression_init(&pedals);
    expression_apply(&pedals, rig);
    ui_changed();
    /* the mode the pedal was left in */
    show(settings->scene_mode ? SCR_SCENE : globals->fs_group == FS_STOMP ? SCR_STOMP : SCR_PRESET);
    boot_start();
}

/* ---------------------------------------------------------------- input */

/* a screen about one block or link of the preset (its slot, cell or link index would point into the next one) */
static int block_screen(enum screen_id id)
{
    return id == SCR_EDIT || id == SCR_BROWSE || id == SCR_MIC || id == SCR_LINK ||
           (id == SCR_TARGET && !ctx.ctl_global);
}

/* another preset loaded: those screens close, down to the first one that is not (the chain, a tab...) */
static void close_block_screens(void)
{
    if (!block_screen(ctx.current))
        return;
    while (ctx.depth && block_screen(ctx.stack[ctx.depth - 1]))
        ctx.depth--;
    ctx.target_new = 0;                     /* a link just added stays (it belongs to the old preset, saved or not) */
    ui_back();
}

void ui_load_preset(int index)
{
    rig_load(ctx.rig, index);
    loads_seen = ctx.rig->loads;
    expression_apply(&pedals, ctx.rig);
    ctx.sel = 0;
    ctx.edit_row = 0;
    ui_changed();
    close_block_screens();
}

/* controls that act the same on every screen */
static int global_input(const struct input_event *ev)
{
    if (ctx.current == SCR_SAVE || ctx.current == SCR_NAME || ctx.current == SCR_TUNER || ctx.current == SCR_LOOPER)
        return 0;                                   /* these use every control themselves */
    uint32_t b = 1u << ev->id;
    if (ev->type == IN_HOLD && b == BTN_EXIT) {     /* the press went one level up already; home keeps the mode */
        ui_tab(ui_mode_tab());
        return 1;
    }
    if (ctx.current != SCR_PRESETS && footswitch_input(&ctx, ev))     /* the list takes A-D and BANK itself */
        return 1;
    if (ev->type != IN_PRESS)
        return 0;
    if (b == BTN_GLOBAL && ctx.current != SCR_SETTINGS && ctx.current != SCR_EQ && ctx.current != SCR_EXP_CAL) {
        ui_go(SCR_SETTINGS);
        return 1;
    }
    if (b == BTN_DRUM && ctx.current != SCR_DRUM) {
        ui_go(SCR_DRUM);
        return 1;
    }
    if (b == BTN_SAVE) {
        ui_open_save();
        return 1;
    }
    /* EXP: what controls the edited parameter (the edit screen does that itself), elsewhere EXP 1's links */
    if (b == BTN_EXP && ctx.current != SCR_EDIT && ctx.current != SCR_CONTROLS && ctx.current != SCR_LINK &&
        ctx.current != SCR_CONTROL_MAP) {
        ui_controls(ctx.rig->preset.exp_group ? CK_EXP1B : CK_EXP1A, 0, -1);
        return 1;
    }
    return 0;
}

void ui_input(const struct input_event *ev)
{
    if (boot_running()) {
        boot_skip();
        return;
    }
    if (alert_input(ev) || top_bar_input(ev) || global_input(ev) || screens[ctx.current]->input(&ctx, ev))
        return;
    if (ui_pressed(ev, BTN_EXIT))                   /* not used by the screen: one level up */
        ui_back();
}

void ui_tick(void)
{
    uint32_t now = time_ms();
    if (boot_done()) {
        lv_screen_load_anim(roots[ctx.current], LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, true);
        if (pending_notice[0]) {
            ui_toast(pending_notice);
            pending_notice[0] = 0;
        }
    }
    if (engine_overload_take()) {
        rig_overload_bypass(ctx.rig);
        ui_changed();
        alert_show("SOBRECARGA DO DSP",
                   "O processamento passou do limite e todos os blocos foram desligados. Reorganize a cadeia "
                   "ou use efeitos mais leves.");
    }
    if (ctx.rig->loads != loads_seen) {        /* a preset loaded elsewhere (the editor): as ui_load_preset */
        loads_seen = ctx.rig->loads;
        expression_apply(&pedals, ctx.rig);
        ctx.sel = 0;
        ctx.edit_row = 0;
        ui_changed();
        close_block_screens();
    }
    static uint32_t rig_gen, globals_gen, settings_gen;
    if (settings_generation() != settings_gen) {      /* waveod's settings changed by the editor: what the UI owns */
        settings_gen = settings_generation();
        gesture_cursor_set(ctx.settings->touch_cursor);
        ui_changed();
    }
    if (ctx.rig->gen != rig_gen || globals_generation() != globals_gen) {      /* changed elsewhere (the editor) */
        rig_gen = ctx.rig->gen;
        globals_gen = globals_generation();
        ui_changed();                           /* the chain's layout and the assignments too */
    }
    if (ctx.rig->notice[0]) {
        ui_toast(ctx.rig->notice);
        ctx.rig->notice[0] = 0;
    }
    if (toast_until && (int32_t)(now - toast_until) >= 0) {
        lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
        toast_until = 0;
    }
    if (now - meter_at >= METER_MS) {
        meter_at = now;
        engine_stats(&ctx.stats);
        top_bar_meters(ctx.stats.in_peak, ctx.stats.out_peak, ctx.stats.worst_ms);
    }
    if (now - stats_at >= STATS_MS) {
        stats_at = now;
        ctx.rev++;                          /* the DSP load is on screen */
    }
    const int raw[2] = { (int)(input_pot(POT_EXP1) * 4095.0f + 0.5f), (int)(input_pot(POT_EXP2) * 4095.0f + 0.5f) };
    if (ctx.learned || ctx.current == SCR_EXP_CAL) {    /* told (learning) or only read (calibrating), nothing acts */
        int key = expression_learn(&pedals, ctx.rig, ctx.globals, raw);
        if (key >= 0 && ctx.learned) {
            ui_learn_fn fn = ctx.learned;
            ctx.learned = 0;
            fn(&ctx, key, 0);
        }
    } else if (expression_poll(&pedals, ctx.rig, ctx.globals, raw, footswitch_link, &ctx)) {
        ui_changed();
    }
    footswitch_tick(&ctx);
    leds_tick(&ctx);
    if (ctx.globals->drum_sync)                 /* the drums follow the effects' tempo */
        drums_set_bpm(rig_tempo(ctx.rig));
    gesture_tick();
    diag_tick();
    if (screens[ctx.current]->tick)
        screens[ctx.current]->tick(&ctx);
    if (seen_rev[ctx.current] != ctx.rev) {
        seen_rev[ctx.current] = ctx.rev;
        screens[ctx.current]->refresh(&ctx);
    }
    lv_timer_handler();
}
