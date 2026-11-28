/* Footswitch links: the conversion of the original's functions, the format, and keys at work (model/control.c, with
   the engine stubbed in test_scenes.c). */
#include "fw/wv_preset.h"
#include "model/control.h"
#include "model/expression.h"
#include "test.h"
#include <string.h>

static int ran_fn, ran_scene, runs;

static void run(void *ctx, const struct control *l)
{
    (void)ctx;
    runs++;
    if (l->action == CA_FN)
        ran_fn = l->arg;
    else
        ran_scene = l->arg;
}

static int find(const struct preset_controls *c, int key, int mode, int when, int action, int slot, int arg)
{
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        if (l->key == key && (l->modes & mode) && l->when == when && l->action == action &&
            (slot < -1 || l->slot == slot) && (arg < 0 || l->arg == arg))
            return i;
    }
    return -1;
}

/* a parameter with a range of at least 3 steps in a used slot */
static int some_param(const struct rig *r, int *slot, struct rig_param *p)
{
    for (int s = 0; s < PRESET_SLOTS; s++)
        for (int k = 0; rig_slot_used(r, s) && !rig_is_routing(r, s) && k < rig_param_count(r, s); k++)
            if (rig_param(r, s, k, p) == 0 && p->def.idx < PRESET_KNOBS &&
                p->def.max - p->def.min >= 3 * (p->def.step > 0 ? p->def.step : 1)) {
                *slot = s;
                return k;
            }
    return -1;
}

static void test_legacy(void)
{
    static struct preset_source src;
    static struct preset p;
    presets_open(&src);
    presets_load(&src, 0, &p);
    const struct preset_controls *c = &p.controls;
    CHECK(c->count > 0 && c->count <= CONTROL_MAX, "01A: %d links", c->count);
    /* stomp A = CTRL 1: its member blocks */
    int members = 0;
    for (int s = 0; s < PRESET_SLOTS; s++)
        members += p.ctrl[0][s] == 1 && p.slot[s].menu >= 0;
    int blocks = 0;
    for (int i = 0; i < c->count; i++)
        blocks += c->c[i].key == CK_A && (c->c[i].modes & CM_STOMP) && c->c[i].action == CA_BLOCK;
    CHECK(members > 0 && blocks == members, "stomp A: the %d blocks of CTRL 1 (%d)", members, blocks);
    CHECK(find(c, CK_A, CM_PRESET, CW_PRESS, CA_FN, -2, -1) < 0 && find(c, CK_COMBO, CM_ALL, CW_PRESS, CA_FN, -2, -1) < 0
          && find(c, CK_TOE, CM_ALL, CW_PRESS, CA_FN, -2, -1) < 0, "the factory functions left to the global table");
    static struct preset_controls f, o;
    preset_controls_factory(&f);
    CHECK(find(&f, CK_A, CM_PRESET, CW_PRESS, CA_FN, -2, FSF_PRESET_A) >= 0, "factory global: preset A of the bank");
    CHECK(find(&f, CK_B, CM_SCENE, CW_PRESS, CA_SCENE, -2, 1) >= 0, "factory global: scene mode B = scene 2");
    CHECK(find(&f, CK_BANK_UP, CM_SCENE, CW_HOLD, CA_FN, -2, FSF_PRESET_UP) >= 0,
          "factory global: scene mode BANK+ held = preset +");
    int tuner = find(&f, CK_COMBO, CM_PRESET, CW_PRESS, CA_FN, -2, FSF_TUNER);
    CHECK(tuner >= 0 && (f.c[tuner].modes & CM_SCENE) && (f.c[tuner].modes & CM_STOMP),
          "factory global: A+B = tuner, one link for the three modes");
    int global_ok = 1;
    for (int i = 0; i < f.count; i++)
        global_ok &= preset_control_global_ok(&f.c[i]);
    CHECK(global_ok && f.count > 20, "factory global: %d links, functions and scenes only", f.count);
    CHECK(find(c, CK_AGAIN, CM_PRESET, CW_PRESS, CA_BLOCK, -2, -1) >= 0, "the preset CTRL on the active key again");

    /* every factory preset: each group as the preset's table, or the factory global one where it has none, does
       what the original's fields say */
    int groups_ok = 1, nothing = 0, bad = -1;
    for (int n = 0; n < PRESET_COUNT && groups_ok; n++) {
        static struct preset x;
        presets_load(&src, n, &x);
        preset_controls_original(&x, &o);
        const struct preset_controls *t = &x.controls;
        for (int key = 0; key < CK_COUNT; key++)
            for (int mode = CM_PRESET; mode <= CM_SCENE; mode <<= 1)
                for (int hold = 0; hold < 2; hold++) {
                    int own = 0;
                    for (int i = 0; i < t->count; i++)
                        own += t->c[i].key == key && (t->c[i].modes & mode) &&
                               (hold ? t->c[i].when == CW_HOLD : t->c[i].when != CW_HOLD);
                    const struct preset_controls *e = own ? t : &f;
                    int ne = 0, no = 0;
                    for (int i = 0; i < e->count; i++) {
                        const struct control *l = &e->c[i];
                        if (l->key != key || !(l->modes & mode) || (hold ? l->when != CW_HOLD : l->when == CW_HOLD))
                            continue;
                        if (l->action == CA_NOTHING) {
                            nothing++;
                            continue;
                        }
                        ne++;
                        if (find(&o, key, mode, l->when, l->action, l->slot, l->arg) < 0)
                            groups_ok = 0;
                    }
                    for (int i = 0; i < o.count; i++)
                        no += o.c[i].key == key && (o.c[i].modes & mode) &&
                              (hold ? o.c[i].when == CW_HOLD : o.c[i].when != CW_HOLD);
                    if (ne != no || !groups_ok) {
                        groups_ok = 0;
                        bad = n * 1000 + key * 10 + mode;
                    }
                }
    }
    CHECK(groups_ok, "240 factory presets: preset table + factory global = the original's fields (%d)", bad);

    /* a preset whose stomp D is TAP keeps it; one whose preset mode A does nothing gets a NOTHING link */
    static struct preset y;
    presets_load(&src, 0, &y);
    y.fs[FS_PRESET][0] = FSF_NONE;
    y.fs[FS_PRESET][1] = FSF_NONE;
    static struct preset_controls yc;
    preset_controls_legacy(&y, &yc);
    CHECK(find(&yc, CK_A, CM_PRESET, CW_PRESS, CA_NOTHING, -2, -1) >= 0 &&
          find(&yc, CK_A, CM_PRESET, CW_HOLD, CA_NOTHING, -2, -1) >= 0, "an empty key where the global has links: NOTHING");
    (void)nothing;

    /* the format: not stored while it is the conversion; stored (without the original's fields) once changed */
    static uint8_t buf[4096];
    int plain = wvp_encode(&p, buf, sizeof buf);
    static struct preset q;
    wvp_decode(buf, (unsigned)plain, &q);
    CHECK(q.controls.count == c->count, "plain: converted again on load");
    struct control l = { .key = CK_C, .modes = CM_STOMP, .when = CW_MOMENT, .action = CA_PARAM,
                         .slot = (int8_t)p.order[0], .arg = 1, .off = 10.0f, .on = 90.0f };
    CHECK(preset_control_add(&p.controls, &l) == 0, "add a link");
    strcpy(p.controls.label[2], "SOLO");
    p.controls.colour[2] = 1;
    int n = wvp_encode(&p, buf, sizeof buf);
    CHECK(n > 0 && n < plain + 6 * c->count + 80, "stored table: %d bytes (plain %d)", n, plain);
    wvp_decode(buf, (unsigned)n, &q);
    int i = find(&q.controls, CK_C, CM_STOMP, CW_MOMENT, CA_PARAM, p.order[0], 1);
    CHECK(q.controls.count == p.controls.count && i >= 0 && q.controls.c[i].off == 10.0f &&
          q.controls.c[i].on == 90.0f, "the table back, the parameter link's values");
    CHECK(strcmp(q.controls.label[2], "SOLO") == 0 && q.controls.colour[2] == 1, "label, colour");
    preset_controls_forget(&q, p.order[0], 1);
    CHECK(find(&q.controls, CK_C, CM_STOMP, CW_MOMENT, CA_PARAM, -2, -1) < 0, "another effect: parameter links go");
}

static void test_keys(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    struct preset_controls *c = &r.preset.controls;
    struct rig_param p;
    int ps = -1, k = some_param(&r, &ps, &p);
    CHECK(k >= 0, "a parameter to link");
    if (k < 0)
        return;

    /* key D: two blocks (the second inverted) and a parameter, in stomp mode */
    int a = -1, b = -1;
    for (int pos = 0; pos < r.preset.count; pos++) {
        int s = r.preset.order[pos];
        if (s == ps || !rig_slot_used(&r, s) || rig_is_routing(&r, s))
            continue;
        if (a < 0)
            a = s;
        else if (b < 0)
            b = s;
    }
    for (int i = c->count - 1; i >= 0; i--)
        if (c->c[i].key == CK_D)
            preset_control_remove(c, i);
    float lo = p.def.min, hi = p.def.min + 2 * (p.def.step > 0 ? p.def.step : 1);
    struct control la = { .key = CK_D, .modes = CM_STOMP, .when = CW_PRESS, .action = CA_BLOCK, .slot = (int8_t)a };
    struct control lb = { .key = CK_D, .modes = CM_STOMP, .when = CW_PRESS, .action = CA_BLOCK, .slot = (int8_t)b, .arg = 1 };
    struct control lp = { .key = CK_D, .modes = CM_STOMP, .when = CW_PRESS, .action = CA_PARAM, .slot = (int8_t)ps,
                          .arg = (uint8_t)p.def.idx, .off = lo, .on = hi };
    preset_control_add(c, &la);
    preset_control_add(c, &lb);
    preset_control_add(c, &lp);
    rig_set_on(&r, a, 0);
    rig_set_on(&r, b, 1);
    rig_knob_set(&r, ps, p.def.idx, lo);
    CHECK(control_state(&r, CK_D, CM_STOMP, 0) == 0, "key D off");
    CHECK(control_press(&r, CK_D, CM_STOMP, 0, run, 0) == 3, "a press acts on the three links");
    rig_param(&r, ps, k, &p);
    CHECK(r.preset.slot[a].on && !r.preset.slot[b].on && p.value == hi && r.chain.slot[a].enabled,
          "on: block on, the inverted one off, the parameter at its ON value");
    CHECK(control_state(&r, CK_D, CM_STOMP, 0) == 1, "key D on");
    control_press(&r, CK_D, CM_STOMP, 0, run, 0);
    rig_param(&r, ps, k, &p);
    CHECK(!r.preset.slot[a].on && r.preset.slot[b].on && p.value == lo, "off again: everything back");

    /* the state follows the block when it is switched elsewhere */
    rig_toggle(&r, a);
    CHECK(control_state(&r, CK_D, CM_STOMP, 0) == 1, "the state is read back from the first block");

    /* momentary: on while down */
    for (int i = c->count - 1; i >= 0; i--)
        if (c->c[i].key == CK_C)
            preset_control_remove(c, i);
    lp.key = CK_C;
    lp.when = CW_MOMENT;
    preset_control_add(c, &lp);
    rig_knob_set(&r, ps, p.def.idx, lo);
    control_moment(&r, CK_C, CM_STOMP, 1, run, 0);
    rig_param(&r, ps, k, &p);
    CHECK(p.value == hi, "momentary: ON value while down");
    control_moment(&r, CK_C, CM_STOMP, 0, run, 0);
    rig_param(&r, ps, k, &p);
    CHECK(p.value == lo, "momentary: OFF value on release");
    CHECK(control_press(&r, CK_C, CM_STOMP, 0, run, 0) == 0, "a momentary link does nothing on the press");

    /* functions and scenes go to the caller */
    runs = 0;
    control_press(&r, CK_A, CM_PRESET, 0, run, 0);
    CHECK(runs == 1 && ran_fn == FSF_PRESET_A, "preset mode A: the function to the caller");
    control_press(&r, CK_B, CM_SCENE, 0, run, 0);
    CHECK(ran_scene == 1, "scene mode B: scene 2 to the caller");

    /* a removed block takes its links along */
    int pos = -1;
    for (int i = 0; i < r.preset.count; i++)
        if (r.preset.order[i] == a)
            pos = i;
    rig_remove(&r, pos);
    CHECK(find(c, CK_D, CM_STOMP, CW_PRESS, CA_BLOCK, a, -1) < 0 &&
          find(c, CK_D, CM_STOMP, CW_PRESS, CA_BLOCK, b, -1) >= 0, "removing a block removes its links only");
}

static void test_pedals(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    struct preset_controls *c = &r.preset.controls;

    /* 01A: EXP 1A = the volume block's level, EXP 1B = the wah; the toe switch flips the group */
    const struct control *toe = control_first(&r, CK_TOE, CM_ALL, 0, CA_FN);
    CHECK(toe && toe->arg == FSF_EXP_AB && control_from_global(&r, CK_TOE, CM_ALL, 0), "toe switch: EXP A/B (global)");
    int va = find(c, CK_EXP1A, CM_ALL, CW_PRESS, CA_PARAM, 1, -1);
    int wb = find(c, CK_EXP1B, CM_ALL, CW_PRESS, CA_PARAM, 4, -1);
    CHECK(va >= 0 && wb >= 0 && (c->c[va].flags & CF_ORDER), "EXP targets converted to parameter links");
    if (va < 0 || wb < 0)
        return;
    struct control vol = c->c[va];
    int knob = control_knob(&r, &vol);
    CHECK(knob >= 0, "the converted link finds its knob (%d)", knob);
    rig_set_dirty(&r, 0);
    control_exp(&r, CK_EXP1A, 1.0f, 10);
    CHECK(r.preset.slot[1].knobs[knob] == vol.on && r.chain.slot[1].knobs[knob] == vol.on, "toe: the max (%g)",
          r.preset.slot[1].knobs[knob]);
    control_exp(&r, CK_EXP1A, 0.0f, 20);
    CHECK(r.preset.slot[1].knobs[knob] == vol.off, "heel: the min");
    CHECK(!r.dirty, "pedal moves are not edits");

    /* curves */
    CHECK(control_curve(CC_SLOW, 0.5f) == 0.25f && control_curve(CC_FAST, 0.5f) == 0.75f &&
          control_curve(CC_LINEAR, 0.3f) == 0.3f, "curves");

    /* EXP 1 group B: the wah on, the volume block off */
    rig_set_on(&r, 1, 1);
    rig_set_on(&r, 4, 0);
    rig_exp_group(&r, 1);
    CHECK(r.preset.slot[4].on && !r.preset.slot[1].on, "group B: wah on, volume off");
    rig_exp_group(&r, 0);
    CHECK(!r.preset.slot[4].on && r.preset.slot[1].on, "group A again");

    /* an auto block on EXP 2: on when it moves, off after AUTO_OFF_MS at the heel */
    struct control au = { .key = CK_EXP2, .modes = CM_ALL, .action = CA_BLOCK, .slot = 4, .arg = 1 };
    preset_control_add(c, &au);
    rig_set_on(&r, 4, 0);
    rig_set_dirty(&r, 0);
    control_exp(&r, CK_EXP2, 0.5f, 1000);
    CHECK(r.preset.slot[4].on && r.chain.slot[4].enabled, "auto: on when the pedal moves");
    control_exp(&r, CK_EXP2, 0.0f, 2000);
    CHECK(!control_exp_tick(&r, 2000 + AUTO_OFF_MS - 1) && r.preset.slot[4].on, "auto: still on at the heel");
    CHECK(control_exp_tick(&r, 2000 + AUTO_OFF_MS) && !r.preset.slot[4].on, "auto: off after %d ms", AUTO_OFF_MS);
    CHECK(!r.dirty, "auto switching is not an edit");
    control_exp(&r, CK_EXP2, 0.0f, 3000);
    control_exp(&r, CK_EXP2, 0.3f, 3100);
    CHECK(!control_exp_tick(&r, 3000 + AUTO_OFF_MS) && r.preset.slot[4].on, "leaving the heel cancels the timer");

    /* the pedal links in the format: curve and flags kept */
    struct control cv = { .key = CK_EXP2, .modes = CM_ALL, .action = CA_PARAM, .slot = 4, .arg = 0,
                          .curve = CC_SLOW, .off = 1.0f, .on = 2.0f };
    preset_control_add(c, &cv);
    static uint8_t buf[4096];
    static struct preset q;
    int n = wvp_encode(&r.preset, buf, sizeof buf);
    wvp_decode(buf, (unsigned)n, &q);
    int i = find(&q.controls, CK_EXP2, CM_ALL, CW_PRESS, CA_PARAM, 4, 0);
    int j = find(&q.controls, CK_EXP1A, CM_ALL, CW_PRESS, CA_PARAM, 1, -1);
    CHECK(i >= 0 && q.controls.c[i].curve == CC_SLOW && q.controls.c[i].on == 2.0f, "pedal link: curve kept");
    CHECK(j >= 0 && (q.controls.c[j].flags & CF_ORDER), "converted link: library order kept");
}

/* new links from the assign screen (control_add_param) */
static void test_editing(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    struct preset_controls *c = &r.preset.controls;
    struct rig_param p;
    int slot = -1, k = some_param(&r, &slot, &p);
    CHECK(k >= 0, "01A has a parameter to link");
    if (k < 0)
        return;
    int n0 = c->count;
    int i = control_add_param(&r, CK_B, CM_STOMP, CW_HOLD, slot, k);
    CHECK(i == n0 && c->c[i].key == CK_B && c->c[i].modes == CM_STOMP && c->c[i].when == CW_HOLD &&
          c->c[i].action == CA_PARAM && c->c[i].slot == slot && c->c[i].arg == p.def.idx && !(c->c[i].flags & CF_ORDER),
          "footswitch link: the key, mode, press and parameter");
    CHECK(c->c[i].off == p.value && c->c[i].on == (p.value < p.def.max ? p.def.max : p.def.min),
          "footswitch link: OFF = the value now, ON = the other end");
    int j = control_add_param(&r, CK_EXP2, CM_STOMP, CW_HOLD, slot, k);
    CHECK(j == i + 1 && c->c[j].modes == CM_ALL && c->c[j].when == CW_PRESS && c->c[j].off == p.def.min &&
          c->c[j].on == p.def.max && c->c[j].curve == CC_LINEAR, "pedal link: every mode, the whole range");
    struct rig_param q;
    CHECK(control_param_index(&r, &c->c[i], &q) == k && q.def.idx == p.def.idx, "the link's parameter back");
    int first = control_find_param(&r, slot, p.def.idx, 0);
    CHECK(first >= 0 && first <= i && control_find_param(&r, slot, p.def.idx, i + 1) == j &&
          control_find_param(&r, slot, p.def.idx, j + 1) < 0, "finding the parameter's links");
    CHECK(control_add_param(&r, CK_A, CM_STOMP, CW_PRESS, slot, rig_param_count(&r, slot)) < 0 && c->count == j + 1,
          "no such parameter: no link");

    /* the held key moves it, alone in its group */
    float target = c->c[i].on;
    control_press(&r, CK_B, CM_STOMP, 1, run, 0);
    CHECK(r.preset.slot[slot].knobs[p.def.idx] == target, "held B: the parameter at ON (%g)",
          r.preset.slot[slot].knobs[p.def.idx]);

    while (control_add_param(&r, CK_C, CM_STOMP, CW_PRESS, slot, k) >= 0)
        ;
    CHECK(c->count == CONTROL_MAX, "a full table refuses more (%d)", c->count);
}

/* learning a pedal (model/expression.c): it moved a quarter of its range and rested, or its toe switch */
static int learn_until(struct expression *x, const struct rig *r, const struct globals *g, int raw0, int raw1,
                       int ms, uint32_t *at)
{
    const int raw[2] = { raw0, raw1 };
    for (int t = 0; t < ms; t += 10) {
        test_now += 10;
        int key = expression_learn(x, r, g, raw);
        if (key >= 0) {
            *at = test_now;
            return key;
        }
    }
    return -1;
}

static void test_learn(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    static struct globals g;
    memset(&g, 0, sizeof g);
    g.exp_min[0] = g.exp_min[1] = 0;
    g.exp_max[0] = 200;
    g.exp_max[1] = 255;
    g.exp_toe[0] = 230;
    g.exp2_jack = EXP2_PEDAL;
    static struct expression x;
    expression_init(&x);
    uint32_t at = 0;
    test_now = 1000;

    expression_learn_start(&x);
    CHECK(learn_until(&x, &r, &g, 0, 0, 500, &at) < 0, "pedals at rest: nothing learned");
    uint32_t start = test_now;
    int key = learn_until(&x, &r, &g, 100 << 4, 0, 2000, &at);
    CHECK(key == CK_EXP1A && at - start >= LEARN_REST_MS, "EXP 1 to the middle: EXP 1A once it rests (%d, %u ms)",
          key, (unsigned)(at - start));

    r.preset.exp_group = 1;
    expression_learn_start(&x);
    key = learn_until(&x, &r, &g, 0, 0, 2000, &at);
    CHECK(key == CK_EXP1B, "back to the heel in group B: EXP 1B (%d)", key);

    expression_learn_start(&x);
    start = test_now;
    key = learn_until(&x, &r, &g, 250 << 4, 0, 2000, &at);
    CHECK(key == CK_TOE && at - start < LEARN_REST_MS, "a stomp past the threshold: the toe switch (%d)", key);

    expression_learn_start(&x);
    key = learn_until(&x, &r, &g, 250 << 4, 200 << 4, 2000, &at);
    CHECK(key == CK_EXP2, "EXP 2 (%d)", key);

    g.exp2_jack = EXP2_ONE_SWITCH;
    expression_learn_start(&x);
    CHECK(learn_until(&x, &r, &g, 250 << 4, 0, 1000, &at) < 0, "the EXP 2 jack with a footswitch: not a pedal");
    test_now = 0;
}

/* the MIDI out bytes sent since the last call (TRS port; USB gets the same) */
static int sent(uint8_t *buf)
{
    uint8_t usb[64];
    int n = test_midi_out(0, buf, 64);
    return test_midi_out(1, usb, 64) == n && !memcmp(buf, usb, (size_t)n) ? n : -1;
}

/* MIDI keys (CK_MIDI + CC) and MIDI out links (CA_MIDI) */
static void test_midi_links(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    struct preset_controls *c = &r.preset.controls;
    c->count = 0;
    struct rig_param p;
    int slot = -1, k = some_param(&r, &slot, &p);
    CHECK(k >= 0, "01A has a parameter to link");
    if (k < 0)
        return;
    uint8_t b[64];
    sent(b);

    /* a CC moving a parameter: every mode, the whole range, heel..toe = 0..127 */
    int i = control_add_param(&r, CK_MIDI + 20, CM_STOMP, CW_HOLD, slot, k);
    CHECK(i == 0 && c->c[i].modes == CM_ALL && c->c[i].when == CW_PRESS && c->c[i].off == p.def.min &&
          c->c[i].on == p.def.max, "MIDI key link: every mode, pressed, the whole range");
    CHECK(control_add_param(&r, CK_MIDI + 69, CM_ALL, CW_PRESS, slot, k) < 0 &&
          control_add_param(&r, CK_MIDI, CM_ALL, CW_PRESS, slot, k) < 0, "no links on CC 0 / 69");
    rig_set_dirty(&r, 0);
    CHECK(control_midi(&r, 20, 127, run, 0) == 1 && r.preset.slot[slot].knobs[p.def.idx] == p.def.max,
          "CC 20 at 127: the max (%g)", r.preset.slot[slot].knobs[p.def.idx]);
    control_midi(&r, 20, 0, run, 0);
    CHECK(r.preset.slot[slot].knobs[p.def.idx] == p.def.min, "CC 20 at 0: the min");
    CHECK(!r.dirty, "MIDI moves are not edits");
    CHECK(control_midi(&r, 21, 127, run, 0) == 0 && control_midi(&r, 69, 127, run, 0) == 0,
          "a CC without links: nothing (its fixed meaning)");

    /* a block from 64 up (arg 1: off), a function from 64 up */
    struct control blk = { .key = CK_MIDI + 85, .modes = CM_ALL, .action = CA_BLOCK, .slot = (int8_t)slot };
    struct control fn = { .key = CK_MIDI + 86, .modes = CM_ALL, .action = CA_FN, .arg = FSF_TAP };
    preset_control_add(c, &blk);
    preset_control_add(c, &fn);
    control_midi(&r, 85, 64, run, 0);
    int on = r.preset.slot[slot].on;
    control_midi(&r, 85, 63, run, 0);
    CHECK(on && !r.preset.slot[slot].on, "CC 85: the block on at 64, off at 63");
    runs = ran_fn = 0;
    control_midi(&r, 86, 0, run, 0);
    CHECK(runs == 0, "CC 86 at 0: the function does not run");
    control_midi(&r, 86, 127, run, 0);
    CHECK(runs == 1 && ran_fn == FSF_TAP, "CC 86 at 127: tap");

    /* a footswitch sending a CC: a press flips it, like a block (the LED follows) */
    c->count = 0;
    struct control out = { .key = CK_D, .modes = CM_STOMP, .when = CW_PRESS };
    control_set_midi(&out, 0);
    CHECK(out.action == CA_MIDI && out.arg == 80 && out.slot == 0 && out.off == 0.0f && out.on == 127.0f &&
          !(out.flags & CF_PC), "MIDI out defaults: CC 80, channel 1, 0 / 127");
    out.slot = 2;
    control_set_midi(&out, 0);
    CHECK(out.slot == 2, "the channel kept");
    preset_control_add(c, &out);
    CHECK(control_state(&r, CK_D, CM_STOMP, 0) == 0, "nothing sent yet: off");
    control_press(&r, CK_D, CM_STOMP, 0, run, 0);
    int n = sent(b);
    CHECK(n == 3 && b[0] == 0xB2 && b[1] == 80 && b[2] == 127, "D: CC 80 127 on channel 3 (%d: %02X %d %d)", n, b[0],
          b[1], b[2]);
    CHECK(control_state(&r, CK_D, CM_STOMP, 0) == 1, "on now");
    control_press(&r, CK_D, CM_STOMP, 0, run, 0);
    n = sent(b);
    CHECK(n == 3 && b[2] == 0 && control_state(&r, CK_D, CM_STOMP, 0) == 0, "D again: CC 80 0, off");

    c->c[0].when = CW_MOMENT;
    control_moment(&r, CK_D, CM_STOMP, 1, run, 0);
    n = sent(b);
    control_moment(&r, CK_D, CM_STOMP, 0, run, 0);
    CHECK(n == 3 && b[2] == 127 && sent(b) == 3 && b[2] == 0, "momentary: 127 down, 0 up");

    /* a program change, on every press */
    struct control pc = { .key = CK_A, .modes = CM_STOMP, .when = CW_PRESS };
    control_set_midi(&pc, 1);
    pc.arg = 5;
    preset_control_add(c, &pc);
    control_press(&r, CK_A, CM_STOMP, 0, run, 0);
    control_press(&r, CK_A, CM_STOMP, 0, run, 0);
    n = sent(b);
    CHECK(n == 4 && b[0] == 0xC0 && b[1] == 5 && b[2] == 0xC0 && b[3] == 5, "A: program change 5 twice (%d)", n);
    CHECK(control_state(&r, CK_A, CM_STOMP, 0) < 0, "a program change has no state");

    /* a pedal sweeping a CC: sent when the value changes */
    struct control sw = { .key = CK_EXP2, .modes = CM_ALL };
    control_set_midi(&sw, 0);
    sw.arg = 11;
    preset_control_add(c, &sw);
    control_exp(&r, CK_EXP2, 0.5f, 100);
    n = sent(b);
    control_exp(&r, CK_EXP2, 0.501f, 110);
    CHECK(n == 3 && b[0] == 0xB0 && b[1] == 11 && b[2] == 64 && sent(b) == 0, "EXP 2 halfway: CC 11 64, once");
    control_exp(&r, CK_EXP2, 1.0f, 120);
    CHECK(sent(b) == 3 && b[2] == 127, "EXP 2 at the toe: 127");

    /* a CC sent on as another (MIDI key to MIDI out) */
    struct control tr = { .key = CK_MIDI + 7, .modes = CM_ALL };
    control_set_midi(&tr, 0);
    CHECK(tr.arg == 7, "MIDI key: its own number by default");
    tr.arg = 12;
    tr.on = 100.0f;
    preset_control_add(c, &tr);
    control_midi(&r, 7, 127, run, 0);
    CHECK(sent(b) == 3 && b[1] == 12 && b[2] == 100, "CC 7 at 127: CC 12 at 100");

    /* the format keeps them; a bad channel is dropped */
    c->c[c->count - 1].curve = CC_FAST;
    struct control bad = pc;
    bad.slot = 16;
    preset_control_add(c, &bad);
    static uint8_t buf[4096];
    static struct preset q;
    int len = wvp_encode(&r.preset, buf, sizeof buf);
    wvp_decode(buf, (unsigned)len, &q);
    CHECK(q.controls.count == c->count - 1, "decoded: %d links of %d", q.controls.count, c->count);
    int ok = q.controls.count == c->count - 1;
    for (int j = 0; ok && j < q.controls.count; j++) {
        const struct control *x = &q.controls.c[j], *y = &c->c[j];
        ok = x->key == y->key && x->action == y->action && x->slot == y->slot && x->arg == y->arg &&
             x->flags == y->flags && x->curve == y->curve && x->off == y->off && x->on == y->on && x->when == y->when;
    }
    CHECK(ok, "MIDI keys and MIDI out links kept (flags, channel, values, curve)");
    sent(b);
}

/* the global table: where a preset has no links, its links run; stored and read back */
static void test_global(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    uint8_t b[64];
    sent(b);
    CHECK(r.global.count > 20 && control_from_global(&r, CK_A, CM_PRESET, 0), "power-up: the factory global table");

    /* preset mode BANK+ = TAP in the global table; a preset link of its own wins; NOTHING keeps it empty */
    struct preset_controls *g = &r.global;
    for (int i = g->count - 1; i >= 0; i--)
        if (g->c[i].key == CK_BANK_UP && (g->c[i].modes & CM_PRESET) && g->c[i].when == CW_PRESS)
            preset_control_remove(g, i);
    struct control tap = { .key = CK_BANK_UP, .modes = CM_PRESET, .when = CW_PRESS, .action = CA_FN, .arg = FSF_TAP };
    preset_control_add(g, &tap);
    runs = ran_fn = 0;
    control_press(&r, CK_BANK_UP, CM_PRESET, 0, run, 0);
    CHECK(runs == 1 && ran_fn == FSF_TAP, "global BANK+ = tap, in a preset without a link there");
    struct control own = tap;
    own.arg = FSF_TUNER;
    preset_control_add(&r.preset.controls, &own);
    runs = ran_fn = 0;
    control_press(&r, CK_BANK_UP, CM_PRESET, 0, run, 0);
    CHECK(runs == 1 && ran_fn == FSF_TUNER && !control_from_global(&r, CK_BANK_UP, CM_PRESET, 0),
          "the preset's own link wins (tuner)");
    r.preset.controls.c[r.preset.controls.count - 1].action = CA_NOTHING;
    runs = 0;
    CHECK(control_press(&r, CK_BANK_UP, CM_PRESET, 0, run, 0) == 0 && runs == 0 &&
          control_first(&r, CK_BANK_UP, CM_PRESET, 0, CA_NONE) == 0, "NOTHING: neither the preset nor the global");
    CHECK(control_first(&r, CK_BANK_UP, CM_PRESET, 1, CA_FN) != 0, "the long press group still the global one");

    /* a global MIDI out CC on stomp C held down, its state kept across preset loads; a global MIDI key */
    struct control cc = { .key = CK_C, .modes = CM_STOMP, .when = CW_PRESS, .action = CA_MIDI, .slot = 1, .arg = 30,
                          .off = 0, .on = 127 };
    struct control key = { .key = CK_MIDI + 40, .modes = CM_ALL, .when = CW_PRESS, .action = CA_SCENE, .arg = 2 };
    preset_control_add(g, &cc);
    preset_control_add(g, &key);
    r.preset.controls.count = 0;            /* nothing of the preset's own */
    control_press(&r, CK_C, CM_STOMP, 0, run, 0);
    int n = sent(b);
    CHECK(n == 3 && b[0] == 0xB1 && b[1] == 30 && b[2] == 127 && control_state(&r, CK_C, CM_STOMP, 0) == 1,
          "global MIDI out: CC 30 127 on channel 2, the key on");
    rig_load(&r, 1);
    r.preset.controls.count = 0;
    CHECK(control_state(&r, CK_C, CM_STOMP, 0) == 1, "after a preset load the global CC's state is kept");
    runs = ran_scene = 0;
    CHECK(control_midi(&r, 40, 127, run, 0) == 1 && runs == 1 && ran_scene == 2, "global MIDI key: CC 40 = scene 3");

    /* stored (block and parameter links left out) and read back */
    struct control blk = { .key = CK_D, .modes = CM_STOMP, .action = CA_BLOCK, .slot = 0 };
    preset_control_add(g, &blk);
    strcpy(g->label[CK_C], "LOOP");
    CHECK(presets_store_global_controls(g) == 0, "global table stored");
    static struct preset_controls back;
    presets_global_controls(&back);
    CHECK(back.count == g->count - 1 && strcmp(back.label[CK_C], "LOOP") == 0 &&
          find(&back, CK_D, CM_STOMP, CW_PRESS, CA_BLOCK, -2, -1) < 0 &&
          find(&back, CK_C, CM_STOMP, CW_PRESS, CA_MIDI, 1, 30) >= 0, "read back: %d links, the block link left out",
          back.count);
    static struct rig r2;
    memset(&r2, 0, sizeof r2);
    rig_init(&r2, RIG_AVOID_NONE);
    CHECK(r2.global.count == back.count && find(&r2.global, CK_C, CM_STOMP, CW_PRESS, CA_MIDI, 1, 30) >= 0,
          "power-up: the stored table");

    preset_controls_factory(&back);        /* the other tests start from the factory table */
    CHECK(presets_store_global_controls(&back) == 0, "factory table stored back");
    sent(b);
}

/* the global table: C+D's looper (factory, every mode) removed everywhere, also after being split to be edited in
   one mode; the pair then does nothing in a preset without links of its own, and stays so after a restart */
static void test_global_remove(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    rig_init(&r, RIG_AVOID_NONE);
    rig_load(&r, 0);
    struct preset_controls *g = &r.global;
    const int cd = CK_COMBO + 2;
    int modes = 0, n0 = g->count, orig = 0, i;
    for (i = 0; i < g->count; i++)
        if (g->c[i].key == cd && g->c[i].action == CA_FN && g->c[i].arg == FSF_LOOPER) {
            modes |= g->c[i].modes;
            orig++;
        }
    CHECK(modes == CM_ALL, "factory: C+D = looper in every mode (%#x)", modes);

    /* opened in preset mode: the pedal's links screen splits it (that mode's own copy) */
    for (i = 0; i < g->count && !(g->c[i].key == cd && g->c[i].action == CA_FN); i++)
        ;
    struct control copy = g->c[i];
    copy.modes = CM_PRESET;
    g->c[i].modes &= (uint8_t)~CM_PRESET;
    preset_control_add(g, &copy);
    int removed = preset_control_remove_alike(g, g->count - 1);
    int left = 0;
    for (i = 0; i < g->count; i++)
        left += g->c[i].key == cd;
    CHECK(removed == CM_ALL && left == 0 && g->count == n0 - orig,
          "removed: modes %#x, %d C+D links left, %d -> %d links", removed, left, n0, g->count);
    CHECK(preset_control_remove_alike(g, g->count) == 0, "out of range: nothing");

    r.preset.controls.count = 0;
    for (int mode = CM_PRESET; mode <= CM_SCENE; mode <<= 1) {
        runs = 0;
        CHECK(control_press(&r, cd, mode, 0, run, 0) == 0 && runs == 0, "C+D does nothing in mode %d", mode);
    }
    runs = ran_fn = 0;
    control_press(&r, CK_COMBO, CM_PRESET, 0, run, 0);
    CHECK(runs == 1 && ran_fn == FSF_TUNER, "A+B still the tuner");

    CHECK(presets_store_global_controls(g) == 0, "stored");
    static struct rig r2;
    memset(&r2, 0, sizeof r2);
    rig_init(&r2, RIG_AVOID_NONE);
    left = 0;
    for (i = 0; i < r2.global.count; i++)
        left += r2.global.c[i].key == cd;
    CHECK(r2.global.count == g->count && left == 0, "after a restart: still no C+D link (%d links)", r2.global.count);

    static struct preset_controls back;
    preset_controls_factory(&back);
    CHECK(presets_store_global_controls(&back) == 0, "factory table stored back");
}

void test_controls(void)
{
    test_legacy();
    test_keys();
    test_pedals();
    test_editing();
    test_learn();
    test_midi_links();
    test_global();
    test_global_remove();
}
