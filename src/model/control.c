#include "model/control.h"
#include "midi/midi.h"

static int midi_cc(const struct control *l)
{
    return l->action == CA_MIDI && !(l->flags & CF_PC);
}

static int stateful(const struct control *l)
{
    return l->action == CA_BLOCK || l->action == CA_PARAM || midi_cc(l);
}

static int is_global(const struct rig *r, const struct control *l)
{
    return l >= r->global.c && l < r->global.c + CONTROL_MAX;
}

/* the CC value a MIDI out link sent last + 1 (0 = none yet), by table index; the global table's kept apart */
static int sent_value(const struct rig *r, const struct control *l)
{
    return is_global(r, l) ? r->global_sent[l - r->global.c] : r->midi_sent[l - r->preset.controls.c];
}

/* a MIDI out link's control change at `value` (always, or only when it differs from the last one sent) */
static void send_cc(struct rig *r, const struct control *l, float value, int always)
{
    int v = (int)(value + 0.5f);
    uint8_t *sent = is_global(r, l) ? &r->global_sent[l - r->global.c] : &r->midi_sent[l - r->preset.controls.c];
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (!always && *sent == v + 1)
        return;
    *sent = (uint8_t)(v + 1);
    uint8_t msg[3] = { (uint8_t)(0xB0 | (l->slot & 15)), (uint8_t)(l->arg & 127), (uint8_t)v };
    midi_send(msg, 3);
}

static void send_pc(const struct control *l)
{
    uint8_t msg[2] = { (uint8_t)(0xC0 | (l->slot & 15)), (uint8_t)(l->arg & 127) };
    midi_send(msg, 2);
}

/* the links of a press group: press and momentary links, or the long press ones */
static int in_group(const struct control *l, int key, int mode, int hold)
{
    if (l->key != key || !(l->modes & mode))
        return 0;
    return hold ? l->when == CW_HOLD : l->when != CW_HOLD;
}

static int usable(const struct rig *r, const struct control *l)
{
    if (l->action == CA_NOTHING)
        return 0;
    return (l->action != CA_BLOCK && l->action != CA_PARAM) || rig_slot_used(r, l->slot);
}

const struct preset_controls *control_table(const struct rig *r, int key, int mode, int hold)
{
    const struct preset_controls *c = &r->preset.controls;
    for (int i = 0; i < c->count; i++)
        if (in_group(&c->c[i], key, mode, hold))
            return c;
    return &r->global;
}

int control_from_global(const struct rig *r, int key, int mode, int hold)
{
    const struct preset_controls *c = control_table(r, key, mode, hold);
    if (c != &r->global)
        return 0;
    for (int i = 0; i < c->count; i++)
        if (in_group(&c->c[i], key, mode, hold))
            return 1;
    return 0;
}

/* the state a stateful link shows: the block's switch, the parameter (or the CC value sent last; none = off)
   nearer its `on` value */
static int link_state(const struct rig *r, const struct control *l)
{
    if (l->action == CA_BLOCK)
        return (r->preset.slot[l->slot].on != 0) != (l->arg != 0);
    int sent = l->action == CA_MIDI ? sent_value(r, l) : 0;
    if (l->action == CA_MIDI && !sent)
        return 0;
    float v = l->action == CA_MIDI ? (float)(sent - 1) : r->preset.slot[l->slot].knobs[l->arg];
    float d_on = v - l->on, d_off = v - l->off;
    return d_on * d_on <= d_off * d_off;
}

static void apply(struct rig *r, const struct control *l, int state)
{
    if (l->action == CA_BLOCK)
        rig_set_on(r, l->slot, state != (l->arg != 0));
    else if (l->action == CA_MIDI)
        send_cc(r, l, state ? l->on : l->off, 1);
    else {
        r->preset_wide++;                   /* a link's values: never captured into a scene */
        rig_knob_set(r, l->slot, l->arg, state ? l->on : l->off);
        r->preset_wide--;
    }
}

/* a link that just runs: a program change, or a function / scene for the caller */
static void run_link(const struct control *l, control_run_fn run, void *ctx)
{
    if (l->action == CA_MIDI)
        send_pc(l);
    else if (run)
        run(ctx, l);
}

int control_state(const struct rig *r, int key, int mode, int hold)
{
    const struct preset_controls *c = control_table(r, key, mode, hold);
    for (int i = 0; i < c->count; i++)
        if (in_group(&c->c[i], key, mode, hold) && stateful(&c->c[i]) && usable(r, &c->c[i]))
            return link_state(r, &c->c[i]);
    return -1;
}

int control_press(struct rig *r, int key, int mode, int hold, control_run_fn run, void *ctx)
{
    const struct preset_controls *c = control_table(r, key, mode, hold);
    int state = control_state(r, key, mode, hold), n = 0;
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (!in_group(l, key, mode, hold) || l->when == CW_MOMENT || !usable(r, l))
            continue;
        if (stateful(l))
            apply(r, l, !state);
        else
            run_link(l, run, ctx);
        n++;
    }
    return n;
}

int control_moment(struct rig *r, int key, int mode, int down, control_run_fn run, void *ctx)
{
    const struct preset_controls *c = control_table(r, key, mode, 0);
    int n = 0;
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (!in_group(l, key, mode, 0) || l->when != CW_MOMENT || !usable(r, l))
            continue;
        if (stateful(l))
            apply(r, l, down);
        else if (down)
            run_link(l, run, ctx);
        n++;
    }
    return n;
}

const struct control *control_first(const struct rig *r, int key, int mode, int hold, int action)
{
    const struct preset_controls *c = control_table(r, key, mode, hold);
    for (int i = 0; i < c->count; i++)
        if (in_group(&c->c[i], key, mode, hold) && (action == CA_NONE || c->c[i].action == action) &&
            usable(r, &c->c[i]))
            return &c->c[i];
    return 0;
}

int control_count(const struct rig *r, int key, int mode, int hold, int action)
{
    const struct preset_controls *c = control_table(r, key, mode, hold);
    int n = 0;
    for (int i = 0; i < c->count; i++)
        n += in_group(&c->c[i], key, mode, hold) && (action == CA_NONE || c->c[i].action == action) &&
             usable(r, &c->c[i]);
    return n;
}

/* ---------------------------------------------------------------- pedals */

#define HEEL 0.02f                          /* below this the pedal is at the heel */

float control_curve(int curve, float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    if (curve == CC_SLOW)
        return x * x;
    if (curve == CC_FAST)
        return 1.0f - (1.0f - x) * (1.0f - x);
    return x;
}

int control_knob(const struct rig *r, const struct control *l);

/* a parameter or MIDI out CC link of a pedal / MIDI key at pos 0..1, along its curve: 1 when it is one */
static int sweep(struct rig *r, const struct control *l, float pos)
{
    float v = l->off + (l->on - l->off) * control_curve(l->curve, pos);
    if (midi_cc(l)) {
        send_cc(r, l, v, 0);
        return 1;
    }
    if (l->action != CA_PARAM)
        return 0;
    int knob = control_knob(r, l);
    if (knob >= 0)
        rig_knob_glide(r, l->slot, knob, v);
    return 1;
}

int control_knob(const struct rig *r, const struct control *l)
{
    if (!rig_slot_used(r, l->slot))
        return -1;
    if (l->flags & CF_ORDER)
        return rig_param_knob(r, l->slot, l->arg);
    return l->arg < PRESET_KNOBS ? l->arg : -1;
}

void control_exp(struct rig *r, int key, float pos, uint32_t now)
{
    if (!CK_IS_PEDAL(key))
        return;
    const struct preset_controls *c = control_table(r, key, CM_ALL, 0);
    uint32_t *heel = &r->heel_at[key - CK_EXP1A];
    int at_heel = pos < HEEL;
    if (!at_heel)
        *heel = 0;
    else if (!*heel)
        *heel = now ? now : 1;
    r->performing++;
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (l->key != key || !usable(r, l) || sweep(r, l, pos))
            continue;
        if (l->action == CA_BLOCK && l->arg == 1 && !at_heel)
            rig_set_on(r, l->slot, 1);
    }
    r->performing--;
}

int control_midi(struct rig *r, int cc, int value, control_run_fn run, void *ctx)
{
    int key = CK_MIDI + cc, high = value >= 64, n = 0;
    if (!CK_MIDI_CC_OK(cc))
        return 0;
    const struct preset_controls *c = control_table(r, key, CM_ALL, 0);
    r->performing++;
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (l->key != key || !usable(r, l))
            continue;
        if (!sweep(r, l, (float)value / 127.0f)) {
            if (l->action == CA_BLOCK)
                rig_set_on(r, l->slot, high != (l->arg != 0));
            else if (high)
                run_link(l, run, ctx);
            else
                continue;
        }
        n++;
    }
    r->performing--;
    return n;
}

int control_exp_tick(struct rig *r, uint32_t now)
{
    const struct preset_controls *c = &r->preset.controls;
    int changed = 0;
    for (int p = 0; p < 3; p++) {
        uint32_t *heel = &r->heel_at[p];
        if (!*heel || now - *heel < AUTO_OFF_MS)
            continue;
        *heel = 0;
        r->performing++;
        for (int i = 0; i < c->count; i++) {
            const struct control *l = &c->c[i];
            if (l->key == CK_EXP1A + p && l->action == CA_BLOCK && l->arg == 1 && rig_slot_used(r, l->slot) &&
                r->preset.slot[l->slot].on) {
                rig_set_on(r, l->slot, 0);
                changed = 1;
            }
        }
        r->performing--;
    }
    return changed;
}

/* the blocks that follow EXP 1's group `key` */
static void group_blocks(struct rig *r, int key, int on)
{
    const struct preset_controls *c = &r->preset.controls;
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (l->key != key || !rig_slot_used(r, l->slot))
            continue;
        int menu = r->preset.slot[l->slot].menu;
        if ((l->action == CA_BLOCK && l->arg == 0) ||
            (l->action == CA_PARAM && (menu == MENU_VOL || menu == MENU_WAH)))
            rig_set_on(r, l->slot, on);
    }
}

void control_exp_group(struct rig *r, int from_key, int to_key)
{
    group_blocks(r, from_key, 0);
    group_blocks(r, to_key, 1);
}

/* ---------------------------------------------------------------- editing */

int control_set_param(const struct rig *r, struct control *l, int k)
{
    struct rig_param p;
    if (rig_param(r, l->slot, k, &p) != 0 || p.def.idx >= PRESET_KNOBS)
        return -1;
    l->action = CA_PARAM;
    l->arg = (uint8_t)p.def.idx;
    l->flags &= (uint8_t)~(CF_ORDER | CF_PC);
    if (CK_SWEEPS(l->key)) {
        l->off = p.def.min;
        l->on = p.def.max;
    } else {
        l->off = p.value;
        l->on = p.value < p.def.max ? p.def.max : p.def.min;
    }
    return 0;
}

int control_add_param(struct rig *r, int key, int modes, int when, int slot, int k)
{
    int every = key >= CK_TOE;                  /* the toe switch, the pedals, MIDI: every mode, pressed */
    struct control l = { .key = (uint8_t)key, .modes = (uint8_t)(every ? CM_ALL : modes),
                         .when = (uint8_t)(every ? CW_PRESS : when), .action = CA_PARAM, .slot = (int8_t)slot,
                         .curve = CC_LINEAR };
    struct preset_controls *c = &r->preset.controls;
    if (key < 0 || !CK_VALID(key) || control_set_param(r, &l, k) != 0 || preset_control_add(c, &l) != 0)
        return -1;
    return c->count - 1;
}

void control_set_midi(struct control *l, int program)
{
    int channel = l->action == CA_MIDI ? l->slot & 15 : 0;
    l->action = CA_MIDI;
    l->slot = (int8_t)channel;
    l->arg = (uint8_t)(program ? 0 : CK_IS_MIDI(l->key) ? l->key - CK_MIDI : 80);
    l->flags = program ? CF_PC : 0;
    l->off = 0.0f;
    l->on = 127.0f;
}

int control_param_index(const struct rig *r, const struct control *l, struct rig_param *out)
{
    int knob = l->action == CA_PARAM ? control_knob(r, l) : -1;
    for (int k = 0; knob >= 0 && k < rig_param_count(r, l->slot); k++)
        if (rig_param(r, l->slot, k, out) == 0 && out->def.idx == knob)
            return k;
    return -1;
}

int control_find_param(const struct rig *r, int slot, int knob, int from)
{
    const struct preset_controls *c = &r->preset.controls;
    for (int i = from < 0 ? 0 : from; i < c->count; i++)
        if (c->c[i].action == CA_PARAM && c->c[i].slot == slot && control_knob(r, &c->c[i]) == knob)
            return i;
    return -1;
}
