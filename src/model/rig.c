#include "model/rig.h"
#include "model/control.h"
#include "model/modulation.h"
#include "engine/audio_engine.h"
#include "engine/blocks.h"
#include "model/param_format.h"
#include "model/route.h"
#include "platform/safety.h"
#include "platform/sdram.h"
#include "platform/time.h"
#include "ui/i18n.h"                       /* the notices */
#include <string.h>

static int knob_param(const struct rig *r, int slot, int knob);

static void emit(struct rig *r, enum rig_event ev, int a, int b)
{
    r->gen++;
    if (r->listener && !r->quiet)
        r->listener(r, ev, a, b);
}

void rig_listen(struct rig *r, rig_listener fn)
{
    r->listener = fn;
}

void rig_set_dirty(struct rig *r, int dirty)
{
    if (r->dirty == (dirty != 0))
        return;
    r->dirty = (uint8_t)(dirty != 0);
    emit(r, RIG_EV_DIRTY, r->dirty, 0);
}

static void notice(struct rig *r, const char *text)
{
    strncpy(r->notice, text, RIG_NOTICE_LEN - 1);
    r->notice[RIG_NOTICE_LEN - 1] = 0;
}

/* fmt (a text of the catalog) with its %d written as v: the model has no printf (the tests build it without LVGL) */
static void format_int(char *out, unsigned size, const char *fmt, int v)
{
    unsigned n = 0;
    for (const char *f = fmt; *f && n + 1 < size; f++) {
        if (f[0] != '%' || f[1] != 'd') {
            out[n++] = *f;
            continue;
        }
        char digits[12];
        int k = 0;
        unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
        do
            digits[k++] = (char)('0' + u % 10);
        while ((u /= 10) && k < (int)sizeof digits);
        if (v < 0 && n + 1 < size)
            out[n++] = '-';
        while (k > 0 && n + 1 < size)
            out[n++] = digits[--k];
        f++;
    }
    out[n] = 0;
}

/* FUN_00022230: defaults go to the knob positions the effect uses, the others keep their values; a Cab / IR
   also gets the parameters of waveod's engine block at their defaults, whatever engine runs it (the knobs may
   hold another effect's) */
static void load_defaults(const struct catalog_item *item, int menu, float *knobs)
{
    struct lib_param p;
    for (int k = 0; k < item->nparams; k++)
        if (catalog_param(item, k, &p) == 0 && p.idx < PRESET_KNOBS)
            knobs[p.idx] = p.def;
    const struct wvfx_desc *e = item->block ? 0 : blocks_engine(menu, 1);
    for (int k = 0; e && blocks_param(e, k, &p) == 0; k++)
        if (p.idx < PRESET_KNOBS)
            knobs[p.idx] = p.def;
}

void rig_resolve(const struct rig *r, struct preset *p)
{
    for (int i = 0; i < PRESET_SLOTS; i++) {
        struct preset_slot *s = &p->slot[i];
        if (s->menu < 0)
            continue;
        int menu = s->menu, index = 0;
        enum resolve_result res = catalog_resolve(&r->cat, &menu, s->id, &index);
        if (res == RESOLVE_INVALID) {
            s->menu = -1;
            continue;
        }
        s->menu = (int8_t)menu;
        s->index = (int16_t)index;
        if (res == RESOLVE_FALLBACK) {
            struct catalog_item item;
            if (catalog_item(&r->cat, menu, index, &item) == 0) {
                s->id = item.id;
                load_defaults(&item, s->menu, s->knobs);
            }
        }
    }
    /* drop chain positions that point at empty slots */
    int n = 0;
    for (int pos = 0; pos < p->count; pos++) {
        int sl = p->order[pos];
        if (sl >= 0 && sl < PRESET_SLOTS && p->slot[sl].menu >= 0)
            p->order[n++] = (int8_t)sl;
    }
    for (int pos = n; pos < PRESET_SLOTS; pos++)
        p->order[pos] = -1;
    p->count = (uint8_t)n;
}

static void resolve_slots(struct rig *r)
{
    rig_resolve(r, &r->preset);
}

static void set_route(struct rig *r)
{
    uint8_t lanes[ROUTE_LANES];
    route_counts(&r->preset, lanes);
    chain_set_route(&r->chain, r->preset.route.mode, lanes, r->preset.route.joint);
}

/* Rebuild the running chain from r->preset; returns the number of slots that failed. */
static int rebuild(struct rig *r)
{
    route_fit(&r->preset);
    engine_suspend();
    r->failed = chain_build(&r->chain, &r->cat, &r->preset, rig_tempo(r));
    set_route(r);
    engine_set_preset_level(r->preset.volume, r->preset.pan);
    modulation_update(r, 0);                /* the routes of these slots before the chain runs */
    engine_resume();
    return r->failed;
}

/* Another preset: with the spillover the old chain's tails ring out in the interrupt (engine_spill) while the
   new one loads into the other side of the pools; without it, or when it does not fit beside them, as rebuild. */
static int rebuild_spill(struct rig *r)
{
    route_fit(&r->preset);
    struct chain *tail = &r->tail[r->chain.side];
    if (!chain_tail(&r->chain, tail) || !engine_spill(tail))
        return rebuild(r);
    r->failed = chain_build_side(&r->chain, &r->cat, &r->preset, rig_tempo(r), !tail->side);
    if (chain_out_of_memory(&r->chain)) {
        engine_suspend();                   /* cuts the tail: the whole pools for the new preset */
        r->failed = chain_build(&r->chain, &r->cat, &r->preset, rig_tempo(r));
    }
    set_route(r);
    engine_set_preset_level(r->preset.volume, r->preset.pan);
    modulation_update(r, 0);
    engine_resume();
    return r->failed;
}

/* the same modules in another order or routing: no reload */
static void relink(struct rig *r)
{
    engine_suspend();
    memcpy(r->chain.order, r->preset.order, sizeof r->chain.order);
    r->chain.count = r->preset.count;
    set_route(r);
    engine_resume();
}

int rig_init(struct rig *r, int avoid)
{
    rig_listener keep = r->listener;
    memset(r, 0, sizeof *r);
    r->listener = keep;
    int err = catalog_open(&r->cat);
    presets_open(&r->src);
    presets_global_controls(&r->global);
    if (err) {
        notice(r, T(MSG_RIG_NO_LIBRARY));
        preset_clear(&r->preset, T(MSG_RIG_NO_LIBRARY_NAME));
        return -1;
    }
    int index = r->src.last_index;
    if (avoid == RIG_AVOID_ANY || avoid == index) {
        /* safe mode: the place of the power-up preset, an empty chain, nothing of it loaded */
        r->index = index;
        r->origin = presets_origin(&r->src, index);
        preset_clear(&r->preset, T(MSG_RIG_SAFE_MODE));
        safety_note_preset(SAFETY_NONE);
        rebuild(r);
        r->loads++;
        emit(r, RIG_EV_LOAD, index, 0);
        return 1;
    }
    rig_load(r, index);
    return 0;
}

int rig_slot_name_in(const struct rig *r, int index, int slot, int *pos, char *out, unsigned size)
{
    static struct preset p SDRAM_BSS;       /* too big for the stack */
    if (slot < 0 || slot >= PRESET_SLOTS || index < 0 || index >= PRESET_COUNT || !size)
        return -1;
    presets_load(&r->src, index, &p);
    const struct preset_slot *s = &p.slot[slot];
    int menu = s->menu, item_index = 0;
    struct catalog_item item;
    if (menu < 0 || catalog_resolve(&r->cat, &menu, s->id, &item_index) == RESOLVE_INVALID ||
        catalog_item(&r->cat, menu, item_index, &item) != 0)
        return -1;
    *pos = -1;
    for (int i = 0; i < p.count; i++)
        if (p.order[i] == slot)
            *pos = i;
    strncpy(out, item.name, size - 1);
    out[size - 1] = 0;
    return 0;
}

void rig_load(struct rig *r, int index)
{
    if (index < 0)
        index = PRESET_COUNT - 1;
    if (index >= PRESET_COUNT)
        index = 0;
    safety_note_preset(index);              /* a crash from here on is told against this preset */
    r->index = index;
    r->origin = presets_load(&r->src, index, &r->preset);
    resolve_slots(r);
    r->scene_chosen = 0;
    memset(r->midi_sent, 0, sizeof r->midi_sent);
    r->dirty = 0;                           /* FUN_00021660: cleared without telling */
    if (rebuild_spill(r))
        notice(r, T(MSG_RIG_NO_MEMORY_CHAIN));
    r->loads++;
    emit(r, RIG_EV_LOAD, index, 0);
}

int rig_slot_used(const struct rig *r, int slot)
{
    return slot >= 0 && slot < PRESET_SLOTS && r->preset.slot[slot].menu >= 0;
}

int rig_slot_item(const struct rig *r, int slot, struct catalog_item *out)
{
    if (!rig_slot_used(r, slot))
        return -1;
    const struct preset_slot *s = &r->preset.slot[slot];
    return catalog_item(&r->cat, s->menu, s->index, out);
}

int rig_is_routing(const struct rig *r, int slot)
{
    if (!rig_slot_used(r, slot))
        return 0;
    uint32_t id = r->preset.slot[slot].id;
    return r->preset.slot[slot].menu == MENU_EQ && (id == ID_SPLIT || id == ID_PATH_B || id == ID_MERGE);
}


int rig_mic_first(const struct rig *r, int slot)
{
    struct catalog_item item;
    if (rig_slot_item(r, slot, &item) != 0 || !item.block || !(item.block->flags & WVFX_ENGINE))
        return -1;                          /* the Cab / IR engine is the one engine block */
    return item.engine.nparams;
}

int rig_param_count(const struct rig *r, int slot)
{
    struct catalog_item item;
    return rig_slot_item(r, slot, &item) == 0 ? item.nparams : 0;
}

/* sync switch of parameter k (f2 = 0x1N: switch is parameter N), or -1 */
static int sync_switch(const struct lib_param *p)
{
    return (p->f2 & 0xf0) == 0x10 ? (p->f2 & 0x0f) : -1;
}

int rig_param(const struct rig *r, int slot, int k, struct rig_param *out)
{
    struct catalog_item item;
    if (rig_slot_item(r, slot, &item) != 0 || catalog_param(&item, k, &out->def) != 0)
        return -1;
    const float *knobs = r->chain.slot[slot].knobs;
    out->value = out->def.idx < PRESET_KNOBS ? knobs[out->def.idx] : 0.0f;
    out->synced = 0;
    int sw = sync_switch(&out->def);
    struct lib_param swp;
    if (sw >= 0 && catalog_param(&item, sw, &swp) == 0 && swp.idx < PRESET_KNOBS && knobs[swp.idx] >= 1.0f) {
        out->synced = 1;
        out->def.min = 0;
        out->def.max = 10;
        out->def.step = 1;
    }
    param_format(&out->def, out->value, out->synced, out->text, sizeof out->text);
    return 0;
}

int rig_scene_editing(const struct rig *r)
{
    return r->scene_mode || r->scene_chosen || preset_scenes_used(&r->preset);
}

_Static_assert(SCENE_PARAMS == 32, "the limit's notice names it");

/* parameter (slot, knob position) becomes a scene parameter, every scene at its value now; its index or -1 */
static int scene_capture(struct rig *r, int slot, int idx)
{
    struct preset_scenes *sc = &r->preset.scenes;
    if (sc->nparams >= SCENE_PARAMS) {
        notice(r, T(MSG_RIG_SCENE_LIMIT));
        return -1;
    }
    struct scene_param *sp = &sc->param[sc->nparams];
    sp->slot = (int8_t)slot;
    sp->knob = (int8_t)idx;
    for (int n = 0; n < SCENE_COUNT; n++)
        sp->value[n] = r->preset.slot[slot].knobs[idx];
    return sc->nparams++;
}

static void set_knob(struct rig *r, int slot, int idx, float v)
{
    if (idx >= PRESET_KNOBS)
        return;
    int i = r->performing ? -1 : preset_scene_param(&r->preset, slot, idx);
    int captured = 0;
    if (i < 0 && !r->performing && !r->preset_wide && r->scene_auto && rig_scene_editing(r)) {
        i = scene_capture(r, slot, idx);    /* turned while a scene plays: it belongs to the scenes from now on */
        captured = i >= 0;
    }
    r->chain.slot[slot].knobs[idx] = v;
    r->preset.slot[slot].knobs[idx] = v;
    if (i >= 0)                             /* a scene parameter: the active scene's value */
        r->preset.scenes.param[i].value[r->preset.scenes.active] = v;
    rig_set_dirty(r, 1);
    if (captured)
        emit(r, RIG_EV_SCENE, r->preset.scenes.active, 0);
}

void rig_param_set(struct rig *r, int slot, int k, float v)
{
    struct rig_param p;
    if (rig_param(r, slot, k, &p) != 0)
        return;
    float step = p.def.step > 0 ? p.def.step : 1.0f;
    if (v < p.def.min)
        v = p.def.min;
    if (v > p.def.max)
        v = p.def.max;
    long n = (long)((v - p.def.min) / step + (v >= p.def.min ? 0.5f : -0.5f));   /* quantize like 0x213CC */
    v = p.def.min + (float)n * step;
    if (v > p.def.max)
        v = p.def.max;
    if (v == p.value)
        return;
    float old = p.value;
    set_knob(r, slot, p.def.idx, v);

    /* a sync switch (f2 = 0x2N) moves its value parameter N to 1/4 when turned on, to the default when off */
    if ((p.def.f2 & 0xf0) == 0x20 && (old >= 1.0f) != (v >= 1.0f)) {
        struct catalog_item item;
        struct lib_param vp;
        if (rig_slot_item(r, slot, &item) == 0 && catalog_param(&item, p.def.f2 & 0x0f, &vp) == 0)
            set_knob(r, slot, vp.idx, v >= 1.0f ? 4.0f : vp.def);
    }
    emit(r, RIG_EV_PARAM, slot, k);
}

void rig_param_nudge(struct rig *r, int slot, int k, int steps)
{
    struct rig_param p;
    if (!steps || rig_param(r, slot, k, &p) != 0)
        return;
    float step = p.def.step > 0 ? p.def.step : 1.0f;
    rig_param_set(r, slot, k, p.value + (float)steps * step);
}

uint16_t rig_tempo(const struct rig *r)
{
    if (r->global_bpm)
        return r->global_bpm;
    return r->preset.bpm >= 40 && r->preset.bpm <= 300 ? r->preset.bpm : 120;
}

void rig_set_global_tempo(struct rig *r, uint16_t bpm)
{
    r->global_bpm = bpm >= 40 && bpm <= 300 ? bpm : 0;
    chain_set_tempo(&r->chain, rig_tempo(r));
}

void rig_set_bpm(struct rig *r, int bpm)
{
    bpm = bpm < 40 ? 40 : bpm > 300 ? 300 : bpm;
    if (r->preset.bpm == bpm)
        return;
    r->preset.bpm = (uint16_t)bpm;
    rig_set_dirty(r, 1);
    chain_set_tempo(&r->chain, rig_tempo(r));
}

/* the bit of `slot` in scene n's blocks */
static void scene_bit(struct rig *r, int n, int slot, int on)
{
    uint16_t bit = (uint16_t)(1u << slot);
    uint16_t *m = &r->preset.scenes.on[n];
    *m = on ? (uint16_t)(*m | bit) : (uint16_t)(*m & ~bit);
}

/* a new block (or no block) in `slot`: the same in every scene */
static void scene_all(struct rig *r, int slot, int on)
{
    for (int n = 0; n < SCENE_COUNT; n++)
        scene_bit(r, n, slot, on);
}

/* a block switched; by a performance control (rig.performing: auto-engaging pedals) it is not an edit */
static void set_on(struct rig *r, int slot, int on)
{
    r->preset.slot[slot].on = (uint8_t)on;
    r->chain.slot[slot].enabled = (uint8_t)on;
    if (r->performing)
        return;
    if (!rig_scene_editing(r))
        scene_all(r, slot, on);             /* no scenes in use: they stay alike */
    else
        scene_bit(r, r->preset.scenes.active, slot, on);
    rig_set_dirty(r, 1);
}

void rig_set_on(struct rig *r, int slot, int on)
{
    if (!rig_slot_used(r, slot) || (r->preset.slot[slot].on != 0) == (on != 0))
        return;
    set_on(r, slot, on != 0);
    emit(r, RIG_EV_ON, slot, 0);
}

void rig_knob_set(struct rig *r, int slot, int knob, float value)
{
    int k = rig_slot_used(r, slot) ? knob_param(r, slot, knob) : -1;
    if (k >= 0)
        rig_param_set(r, slot, k, value);
}

int rig_param_knob(const struct rig *r, int slot, int k)
{
    struct rig_param p;
    return rig_param(r, slot, k, &p) == 0 && p.def.idx < PRESET_KNOBS ? p.def.idx : -1;
}

#define GLIDE_BLOCKS 8                      /* ~12 ms: about the time between two readings of a pedal */
#define GLIDE_STEPS  20                     /* parameters with at least this many steps glide */

void rig_knob_glide(struct rig *r, int slot, int knob, float v)
{
    int k = rig_slot_used(r, slot) ? knob_param(r, slot, knob) : -1;
    struct rig_param p;
    if (k < 0 || rig_param(r, slot, k, &p) != 0)
        return;
    float step = p.def.step > 0 ? p.def.step : 1.0f;
    v = v < p.def.min ? p.def.min : v > p.def.max ? p.def.max : v;
    long n = (long)((v - p.def.min) / step + 0.5f);
    v = p.def.min + (float)n * step;
    if (r->preset.slot[slot].knobs[knob] == v)
        return;
    r->preset.slot[slot].knobs[knob] = v;
    /* switches (tempo sync) and short ranges change at once, the others glide */
    int glide = (p.def.max - p.def.min) / step >= GLIDE_STEPS && (p.def.f2 & 0xf0) != 0x20 && !p.synced;
    chain_ramp(&r->chain, slot, knob, v, glide ? GLIDE_BLOCKS : 0);
}

void rig_exp(struct rig *r, int group, float pos)
{
    if (group < 0 || group >= EXP_GROUPS)
        return;
    control_exp(r, CK_EXP1A + group, pos, time_ms());
    emit(r, RIG_EV_EXP, group, 0);
}

void rig_exp_group(struct rig *r, int group)
{
    if (group < 0 || group > 1 || r->preset.exp_group == group)
        return;
    int from = r->preset.exp_group;
    r->preset.exp_group = (uint8_t)group;
    control_exp_group(r, CK_EXP1A + from, CK_EXP1A + group);
}
void rig_toggle(struct rig *r, int slot)
{
    if (!rig_slot_used(r, slot))
        return;
    set_on(r, slot, !r->preset.slot[slot].on);
    emit(r, RIG_EV_ON, slot, 0);
}

/* ---------------------------------------------------------------- scenes */

/* library order of the parameter at knob position `knob`, or -1 */
static int knob_param(const struct rig *r, int slot, int knob)
{
    struct catalog_item item;
    if (rig_slot_item(r, slot, &item) != 0)
        return -1;
    int n = rig_param_count(r, slot);
    for (int k = 0; k < n; k++) {
        struct lib_param p;
        if (catalog_param(&item, k, &p) == 0 && p.idx == knob)
            return k;
    }
    return -1;
}

int rig_scene(struct rig *r, int n)
{
    if (n < 0 || n >= SCENE_COUNT)
        return -1;
    struct preset_scenes *sc = &r->preset.scenes;
    sc->active = (uint8_t)n;
    for (int slot = 0; slot < PRESET_SLOTS; slot++)
        if (rig_slot_used(r, slot)) {
            uint8_t on = (uint8_t)(sc->on[n] >> slot & 1u);
            r->preset.slot[slot].on = on;
            r->chain.slot[slot].enabled = on;
        }
    for (int i = 0; i < sc->nparams; i++) {
        const struct scene_param *sp = &sc->param[i];
        if (!rig_slot_used(r, sp->slot))
            continue;
        float v = sp->value[n];
        r->preset.slot[sp->slot].knobs[sp->knob] = v;
        r->chain.slot[sp->slot].knobs[sp->knob] = v;
        int k = knob_param(r, sp->slot, sp->knob);
        if (k >= 0)
            emit(r, RIG_EV_PARAM, sp->slot, k);
    }
    emit(r, RIG_EV_ALL_ON, -1, 0);
    emit(r, RIG_EV_SCENE, n, 0);
    return 0;
}

int rig_scene_active(const struct rig *r)
{
    return r->preset.scenes.active;
}

int rig_scene_param(const struct rig *r, int slot, int k)
{
    struct rig_param p;
    return rig_param(r, slot, k, &p) == 0 && preset_scene_param(&r->preset, slot, p.def.idx) >= 0;
}

int rig_scene_param_toggle(struct rig *r, int slot, int k)
{
    struct rig_param p;
    if (rig_param(r, slot, k, &p) != 0 || p.def.idx >= PRESET_KNOBS)
        return -1;
    struct preset_scenes *sc = &r->preset.scenes;
    int i = preset_scene_param(&r->preset, slot, p.def.idx);
    int now;
    if (i >= 0) {                           /* back to the preset: the value it has now, in every scene */
        sc->param[i] = sc->param[--sc->nparams];
        now = 0;
    } else if (scene_capture(r, slot, p.def.idx) < 0) {
        return -1;
    } else {
        now = 1;
    }
    rig_set_dirty(r, 1);
    emit(r, RIG_EV_SCENE, sc->active, 0);
    return now;
}

void rig_scene_set_name(struct rig *r, int n, const char *name)
{
    if (n < 0 || n >= SCENE_COUNT)
        return;
    char t[SCENE_NAME_LEN + 1];
    strncpy(t, name, SCENE_NAME_LEN);
    t[SCENE_NAME_LEN] = 0;
    int len = (int)strlen(t);
    while (len > 0 && t[len - 1] == ' ')
        t[--len] = 0;
    char def[SCENE_NAME_LEN + 1];
    rig_scene_name(r, -1 - n, def);
    if (strcmp(t, def) == 0)                /* the default name: stored as none */
        t[0] = 0;
    if (strcmp(t, r->preset.scenes.name[n]) == 0)
        return;
    memcpy(r->preset.scenes.name[n], t, sizeof t);
    rig_set_dirty(r, 1);
    emit(r, RIG_EV_SCENE, r->preset.scenes.active, 0);
}

/* n < 0: the default name of scene -1 - n */
void rig_scene_name(const struct rig *r, int n, char out[SCENE_NAME_LEN + 1])
{
    if (n >= 0 && n < SCENE_COUNT && r->preset.scenes.name[n][0]) {
        memcpy(out, r->preset.scenes.name[n], SCENE_NAME_LEN + 1);
        return;
    }
    if (n < 0)
        n = -1 - n;
    format_int(out, SCENE_NAME_LEN + 1, T(MSG_RIG_SCENE_DEFAULT), n % SCENE_COUNT + 1);
}

void rig_scene_copy(struct rig *r, int from, int to)
{
    struct preset_scenes *sc = &r->preset.scenes;
    if (from < 0 || from >= SCENE_COUNT || to < 0 || to >= SCENE_COUNT || from == to)
        return;
    sc->on[to] = sc->on[from];
    for (int i = 0; i < sc->nparams; i++)
        sc->param[i].value[to] = sc->param[i].value[from];
    rig_set_dirty(r, 1);
    if (to == sc->active)
        rig_scene(r, to);
    else
        emit(r, RIG_EV_SCENE, sc->active, 0);
}

/* the slot holds the same effect in both presets */
static int same_block(const struct preset *a, const struct preset *b, int slot)
{
    return a->slot[slot].menu >= 0 && b->slot[slot].menu >= 0 && a->slot[slot].id == b->slot[slot].id;
}

int rig_scene_revert(struct rig *r, int n)
{
    if (n < 0 || n >= SCENE_COUNT)
        return -1;
    static struct preset saved SDRAM_BSS;   /* too big for the stack */
    if (presets_load(&r->src, r->index, &saved) == PRESET_EMPTY)
        return -1;
    rig_resolve(r, &saved);
    struct preset_scenes *sc = &r->preset.scenes;
    const struct preset_scenes *ss = &saved.scenes;
    static struct preset_scenes before SDRAM_BSS;
    before = *sc;
    for (int slot = 0; slot < PRESET_SLOTS; slot++)     /* blocks added since keep what they have */
        if (same_block(&r->preset, &saved, slot)) {
            uint16_t bit = (uint16_t)(1u << slot);
            sc->on[n] = (uint16_t)((sc->on[n] & ~bit) | (ss->on[n] & bit));
        }
    for (int i = 0; i < sc->nparams; i++) {
        struct scene_param *sp = &sc->param[i];
        if (!same_block(&r->preset, &saved, sp->slot))
            continue;
        int j = preset_scene_param(&saved, sp->slot, sp->knob);
        sp->value[n] = j >= 0 ? ss->param[j].value[n] : saved.slot[sp->slot].knobs[sp->knob];
    }
    memcpy(sc->name[n], ss->name[n], sizeof sc->name[n]);
    if (memcmp(&before, sc, sizeof before) != 0)
        rig_set_dirty(r, 1);
    if (n == sc->active)
        rig_scene(r, n);
    else
        emit(r, RIG_EV_SCENE, sc->active, 0);
    return 0;
}

void rig_overload_bypass(struct rig *r)
{
    chain_bypass_all(&r->chain);
    r->preset.scenes.on[r->preset.scenes.active] = 0;
    for (int i = 0; i < PRESET_SLOTS; i++) {                /* FUN_00022514: every slot, told one by one */
        r->preset.slot[i].on = 0;
        rig_set_dirty(r, 1);
        emit(r, RIG_EV_ON, i, 0);
    }
}

void rig_set_name(struct rig *r, const char *name)
{
    char n[PRESET_NAME_LEN + 1];
    strncpy(n, name, PRESET_NAME_LEN);
    n[PRESET_NAME_LEN] = 0;
    int len = (int)strlen(n);
    while (len > 0 && n[len - 1] == ' ')                /* stored names are compared trimmed */
        n[--len] = 0;
    if (strcmp(n, r->preset.name) == 0)
        return;
    memcpy(r->preset.name, n, sizeof n);
    if (r->dirty)
        r->gen++;                                       /* already modified: the screens still redraw the name */
    else
        rig_set_dirty(r, 1);
}

void rig_set_volume(struct rig *r, int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    r->preset.volume = (uint8_t)volume;
    engine_set_preset_level(r->preset.volume, r->preset.pan);
    rig_set_dirty(r, 1);
    emit(r, RIG_EV_VOLUME, volume, 0);
}

/* a slot got another effect: quick knob and expression targets on a parameter it does not have go back to
   its first parameter, expression ranges outside it to its range (FUN_00021230) */
static void revalidate(struct rig *r, int slot, const struct catalog_item *item)
{
    struct preset *p = &r->preset;
    int n = rig_param_count(r, slot);
    struct lib_param first;
    int has_first = catalog_param(item, 0, &first) == 0;
    for (int i = 0; i < QUICK_KNOBS; i++)
        if (p->qk_target[i] == slot && p->qk_param[i] >= n)
            p->qk_param[i] = 0;
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            struct exp_target *x = &p->exp[g][t];
            if (x->slot != slot)
                continue;
            struct lib_param lp;
            int ok = x->param < n && catalog_param(item, x->param, &lp) == 0 && x->max <= lp.max &&
                     x->min >= lp.min;
            if (!ok && has_first) {
                x->param = 0;
                x->min = first.min;
                x->max = first.max;
            }
        }
}

/* structural edit helper: apply, rebuild, roll back when a module does not fit */
static int commit(struct rig *r, const struct preset *before, int slot, int assign)
{
    if (rebuild(r) == 0) {
        if (assign)                         /* FUN_00021138 / FUN_00021230 tell the assignments first */
            emit(r, RIG_EV_ASSIGN, slot, 0);
        rig_set_dirty(r, 1);
        emit(r, RIG_EV_CHAIN, slot, 0);
        return 0;
    }
    r->preset = *before;
    rebuild(r);
    notice(r, T(MSG_RIG_NO_MEMORY_BLOCK));
    return -1;
}

int rig_replace(struct rig *r, int slot, int menu, int index)
{
    struct catalog_item item;
    if (!rig_slot_used(r, slot) || catalog_item(&r->cat, menu, index, &item) != 0)
        return -1;
    struct preset_slot *s = &r->preset.slot[slot];
    if (s->menu == menu && s->index == index)       /* FUN_00022230: nothing to do */
        return 0;
    struct preset before = r->preset;
    /* another menu: its assignments are forgotten (FUN_00021138); another model of the same menu: checked
       against it (FUN_00021230) */
    if (s->menu != menu)
        preset_unassign_slot(&r->preset, slot);
    preset_scene_forget(&r->preset, slot);  /* the knobs mean something else now */
    preset_controls_forget(&r->preset, slot, 1);
    s->menu = (int8_t)menu;
    s->index = (int16_t)index;
    s->id = item.id;
    load_defaults(&item, s->menu, s->knobs);
    if (before.slot[slot].menu == menu)
        revalidate(r, slot, &item);
    return commit(r, &before, slot, 1);
}

int rig_insert(struct rig *r, int pos, int menu, int index)
{
    struct preset *p = &r->preset;
    struct catalog_item item;
    if (catalog_item(&r->cat, menu, index, &item) != 0)
        return -1;
    int slot = -1;
    for (int i = 0; i < PRESET_SLOTS && slot < 0; i++)
        if (p->slot[i].menu < 0)
            slot = i;
    if (slot < 0 || p->count >= PRESET_SLOTS) {
        notice(r, T(MSG_RIG_CHAIN_FULL));
        emit(r, RIG_EV_CHAIN_FULL, 0, 0);
        return -1;
    }
    if (pos < 0 || pos > p->count)
        pos = p->count;
    struct preset before = *p;
    preset_unassign_slot(p, slot);          /* the empty slot's leftovers (FUN_00022230 -> FUN_00021138) */
    struct preset_slot *s = &p->slot[slot];
    s->on = 1;                              /* knobs the effect does not use keep their old values */
    s->menu = (int8_t)menu;
    s->index = (int16_t)index;
    s->id = item.id;
    load_defaults(&item, s->menu, s->knobs);
    p->route.cell[slot] = -1;               /* goes into the lane of the block before it (route_fit) */
    scene_all(r, slot, 1);
    for (int i = p->count; i > pos; i--)
        p->order[i] = p->order[i - 1];
    p->order[pos] = (int8_t)slot;
    p->count++;
    return commit(r, &before, slot, 1);
}

int rig_move(struct rig *r, int from, int to)
{
    struct preset *p = &r->preset;
    if (from < 0 || from >= p->count || to < 0 || to >= p->count)
        return -1;
    if (from == to)
        return 0;
    struct preset before = *p;
    int8_t slot = p->order[from];
    for (int i = from; i + 1 < p->count; i++)
        p->order[i] = p->order[i + 1];
    for (int i = p->count - 1; i > to; i--)
        p->order[i] = p->order[i - 1];
    p->order[to] = slot;
    return commit(r, &before, -1, 0);
}

int rig_remove(struct rig *r, int pos)
{
    struct preset *p = &r->preset;
    if (pos < 0 || pos >= p->count)
        return -1;
    struct preset before = *p;
    int slot = p->order[pos];
    for (int i = pos; i + 1 < p->count; i++)
        p->order[i] = p->order[i + 1];
    p->count--;
    p->order[p->count] = -1;
    if (slot >= 0 && slot < PRESET_SLOTS) {
        p->slot[slot].menu = -1;            /* FUN_00022430: knobs stay */
        p->slot[slot].index = 0;
        p->slot[slot].id = 0;
        p->slot[slot].on = 0;
        p->route.cell[slot] = -1;           /* the others keep their cells */
        preset_unassign_slot(p, slot);
        scene_all(r, slot, 0);
    }
    return commit(r, &before, slot, 1);
}

int rig_place(struct rig *r, int cell, int menu, int index)
{
    struct preset *p = &r->preset;
    struct catalog_item item;
    if (route_lane(&p->route, cell) == ROUTE_HIDDEN || route_slot_at(p, cell) >= 0 ||
        catalog_item(&r->cat, menu, index, &item) != 0)
        return -1;
    int slot = -1;
    for (int i = 0; i < PRESET_SLOTS && slot < 0; i++)
        if (p->slot[i].menu < 0)
            slot = i;
    if (slot < 0 || p->count >= PRESET_SLOTS) {
        notice(r, T(MSG_RIG_CHAIN_FULL));
        emit(r, RIG_EV_CHAIN_FULL, 0, 0);
        return -1;
    }
    struct preset before = *p;
    preset_unassign_slot(p, slot);
    struct preset_slot *s = &p->slot[slot];
    s->on = 1;
    s->menu = (int8_t)menu;
    s->index = (int16_t)index;
    s->id = item.id;
    load_defaults(&item, s->menu, s->knobs);
    p->route.cell[slot] = (int8_t)cell;
    scene_all(r, slot, 1);
    route_apply_order(p);
    return commit(r, &before, slot, 1) == 0 ? slot : -1;
}

/* the order changed but not the modules */
static void reordered(struct rig *r)
{
    relink(r);
    rig_set_dirty(r, 1);
    emit(r, RIG_EV_CHAIN, -1, 0);
}

int rig_move_cell(struct rig *r, int slot, int cell)
{
    struct preset *p = &r->preset;
    if (!rig_slot_used(r, slot) || p->route.cell[slot] < 0 || route_lane(&p->route, cell) == ROUTE_HIDDEN ||
        p->route.cell[slot] == cell)
        return -1;
    int other = route_slot_at(p, cell);
    if (other >= 0)
        p->route.cell[other] = p->route.cell[slot];
    p->route.cell[slot] = (int8_t)cell;
    route_apply_order(p);
    reordered(r);
    return 0;
}

int rig_route(struct rig *r, int mode, int split, int merge)
{
    if (!route_set(&r->preset, mode, split, merge))
        return 0;
    reordered(r);
    return 1;
}

void rig_joint(struct rig *r, int k, int value)
{
    if (k < 0 || k >= JOINT_PARAMS)
        return;
    value = value < 0 ? 0 : value > 100 ? 100 : value;
    if (r->preset.route.joint[k] == value)
        return;
    r->preset.route.joint[k] = (uint8_t)value;
    set_route(r);
    rig_set_dirty(r, 1);
}

void rig_rebuild(struct rig *r)
{
    if (rebuild(r))
        notice(r, T(MSG_RIG_NO_MEMORY_CHAIN));
}

void rig_library_writing(struct rig *r)
{
    lib_open(&r->cat.wvl, 0, 0);            /* nothing reads the library while it is rewritten */
}

void rig_library_changed(struct rig *r)
{
    catalog_reopen_wvlib(&r->cat);
    rig_resolve(r, &r->preset);             /* indices moved; a missing effect may be there now, or gone */
    rig_rebuild(r);
    emit(r, RIG_EV_CHAIN, -1, 0);
}

int rig_save(struct rig *r, int index)
{
    if (index < 0 || index >= PRESET_COUNT)
        return -1;
    int flash = engine_flash_begin();       /* the audio plays on: nothing in it reads the flash */
    int err = presets_store(&r->src, index, &r->preset);
    if (!err)
        presets_set_startup(&r->src, index);    /* and power up on it */
    engine_flash_end(flash);
    if (err) {
        char text[RIG_NOTICE_LEN];
        format_int(text, sizeof text, T(MSG_RIG_SAVE_FAILED), -err);
        notice(r, text);
        return -1;
    }
    int moved = r->index != index;
    r->index = index;
    r->origin = PRESET_USER;
    rig_set_dirty(r, 0);                    /* FUN_000210B4: told */
    emit(r, RIG_EV_SAVED, index, moved);
    return 0;
}

int rig_store_global(struct rig *r)
{
    int flash = engine_flash_begin();       /* the audio plays on: nothing in it reads the flash */
    int err = presets_store_global_controls(&r->global);
    engine_flash_end(flash);
    if (!err)
        presets_global_controls(&r->global);    /* as stored: what may not be global left out */
    return err;
}

int rig_swap(struct rig *r, int index)
{
    static struct preset other SDRAM_BSS;
    int from = r->index;
    if (index < 0 || index >= PRESET_COUNT || index == from)
        return -1;
    if (presets_load(&r->src, index, &other) == PRESET_EMPTY)
        preset_clear(&other, "");
    if (rig_save(r, index) != 0)
        return -1;
    int flash = engine_flash_begin();       /* the audio plays on: nothing in it reads the flash */
    int err = presets_store(&r->src, from, &other);
    engine_flash_end(flash);
    if (err) {
        notice(r, T(MSG_RIG_SWAP_FAILED));
        return -1;
    }
    return 0;
}
