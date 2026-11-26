/* Scenes: their place in waveod's preset format and the rig switching them (model/rig.c, with the engine stubbed). */
#include "fw/wv_preset.h"
#include "fw/wv_store.h"
#include "model/rig.h"
#include "test.h"
#include <string.h>

/* ---------------------------------------------------------------- the engine, stubbed: the rig's bookkeeping only */

static int builds;

int chain_build(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm)
{
    (void)cat;
    (void)bpm;
    builds++;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        c->slot[i].menu = p->slot[i].menu;
        c->slot[i].index = p->slot[i].index;
        c->slot[i].id = p->slot[i].id;
        c->slot[i].enabled = p->slot[i].on;
        memcpy(c->slot[i].knobs, p->slot[i].knobs, sizeof c->slot[i].knobs);
    }
    memcpy(c->order, p->order, sizeof c->order);
    c->count = p->count;
    return 0;
}

void chain_set_tempo(struct chain *c, uint16_t bpm) { (void)c; (void)bpm; }
void chain_set_route(struct chain *c, int mode, const uint8_t lane_n[4], const uint8_t joint[JOINT_PARAMS])
{
    (void)c; (void)mode; (void)lane_n; (void)joint;
}
void chain_ramp(struct chain *c, int slot, int knob, float value, int blocks)
{
    (void)blocks;
    c->slot[slot].knobs[knob] = value;
}
void chain_bypass_all(struct chain *c)
{
    for (int i = 0; i < PRESET_SLOTS; i++)
        c->slot[i].enabled = 0;
}
int chain_build_side(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm, int side)
{
    (void)side;
    return chain_build(c, cat, p, bpm);
}
int chain_out_of_memory(const struct chain *c) { (void)c; return 0; }
int engine_spill(struct chain *tail) { (void)tail; return 0; }
void engine_suspend(void) {}
void engine_resume(void) {}
int engine_flash_begin(void) { return 0; }
void engine_flash_end(int suspended) { (void)suspended; }
void engine_set_preset_level(uint8_t volume, int8_t pan) { (void)volume; (void)pan; }
void safety_note_preset(int index) { (void)index; }

/* ---------------------------------------------------------------- format */

static void test_format(void)
{
    static struct preset p, q;
    static uint8_t buf[4096];
    preset_clear(&p, "SCENES");
    for (int i = 0; i < 3; i++) {
        p.slot[i].menu = 3;
        p.slot[i].id = 0x100 + (uint32_t)i;
        p.slot[i].on = 1;
        p.slot[i].knobs[2] = 10.0f * (float)i;
        p.order[i] = (int8_t)i;
    }
    p.count = 3;
    preset_scenes_reset(&p);
    CHECK(p.scenes.on[0] == 7 && p.scenes.on[3] == 7 && !preset_scenes_used(&p), "reset: every scene = the blocks");
    int plain = wvp_encode(&p, buf, sizeof buf);
    CHECK(wvp_decode(buf, (unsigned)plain, &q) == 0 && q.scenes.on[2] == 7 && !preset_scenes_used(&q),
          "no scenes chunk: the blocks in every scene");

    /* scene 1 with block 1 off, a scene parameter, a name */
    p.scenes.on[1] = 5;
    strcpy(p.scenes.name[2], "SOLO");
    p.scenes.nparams = 1;
    p.scenes.param[0] = (struct scene_param){ 2, 2, { 20.0f, 35.0f, 50.0f, 20.0f } };
    int n = wvp_encode(&p, buf, sizeof buf);
    CHECK(n > plain && n - plain < 80, "the scenes chunk is small (%d bytes)", n - plain);
    CHECK(wvp_decode(buf, (unsigned)n, &q) == 0, "decode");
    CHECK(q.scenes.active == 0 && q.scenes.on[0] == 7 && q.scenes.on[1] == 5 && q.scenes.on[3] == 7,
          "masks %x %x %x", q.scenes.on[0], q.scenes.on[1], q.scenes.on[3]);
    CHECK(strcmp(q.scenes.name[2], "SOLO") == 0 && q.scenes.name[0][0] == 0, "names");
    CHECK(q.scenes.nparams == 1 && q.scenes.param[0].slot == 2 && q.scenes.param[0].knob == 2 &&
          q.scenes.param[0].value[1] == 35.0f && q.scenes.param[0].value[2] == 50.0f, "scene parameter");

    /* the slots are what plays: the active scene follows them */
    p.scenes.active = 1;
    p.slot[1].on = 1;
    p.slot[2].knobs[2] = 40.0f;
    n = wvp_encode(&p, buf, sizeof buf);
    wvp_decode(buf, (unsigned)n, &q);
    CHECK(q.scenes.active == 1 && q.scenes.on[1] == 7 && q.scenes.param[0].value[1] == 40.0f,
          "active scene from the slots");

    /* a malformed chunk: no scenes rather than garbage */
    unsigned at = (unsigned)plain;              /* the scenes chunk comes last */
    CHECK(buf[at] == 10, "scenes chunk tag");
    buf[at + 3 + 1] = 200;                      /* 200 scenes in a few bytes */
    wvp_decode(buf, (unsigned)n, &q);
    CHECK(q.scenes.on[1] == q.scenes.on[0] && q.scenes.nparams == 0, "malformed: the blocks in every scene");

    preset_unassign_slot(&p, 2);
    CHECK(p.scenes.nparams == 0, "removing a block forgets its scene parameters");
}

/* every chunk as big as it gets still fits WVP_MAX (the store's buffer) */
static void test_largest(void)
{
    static struct preset p;
    static uint8_t buf[8192];
    preset_clear(&p, "");
    memset(p.name, 'W', PRESET_NAME_LEN);
    memset(p.style, 'S', 10);
    for (int i = 0; i < PRESET_SLOTS; i++) {
        p.slot[i].menu = 3;
        p.slot[i].id = 0x100 + (uint32_t)i;
        p.order[i] = (int8_t)i;
    }
    p.count = PRESET_SLOTS;
    p.controls.count = CONTROL_MAX;
    for (int i = 0; i < CONTROL_MAX; i++)
        p.controls.c[i] = (struct control){ .key = CK_EXP1A, .action = CA_MIDI, .arg = (uint8_t)i };
    for (int k = 0; k < CK_KEYS; k++)
        memset(p.controls.label[k], 'L', CONTROL_LABEL);
    for (int n = 0; n < SCENE_COUNT; n++) {
        memset(p.scenes.name[n], 'N', SCENE_NAME_LEN);
        p.scenes.on[n] = (uint16_t)n;
    }
    p.scenes.nparams = SCENE_PARAMS;
    for (int i = 0; i < SCENE_PARAMS; i++)
        p.scenes.param[i] = (struct scene_param){ (int8_t)(i % PRESET_SLOTS), (int8_t)(i / PRESET_SLOTS), { 1, 2, 3, 4 } };
    for (int m = 0; m < MOD_COUNT; m++)
        p.mods.m[m].type = MOD_LFO;
    int n = wvp_encode(&p, buf, sizeof buf);
    CHECK(n > 0 && n <= WVP_MAX, "the largest preset fits WVP_MAX (%d of %d bytes)", n, WVP_MAX);
    CHECK(n <= WVS_PAYLOAD_MAX, "and one record of the store");
}

/* ---------------------------------------------------------------- rig */

static int first_with_params(const struct rig *r, int from)
{
    for (int s = from; s < PRESET_SLOTS; s++)
        if (rig_slot_used(r, s) && !rig_is_routing(r, s) && rig_param_count(r, s) > 0)
            return s;
    return -1;
}

static void test_rig(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    CHECK(rig_init(&r, RIG_AVOID_NONE) == 0, "rig with the library");
    rig_load(&r, 0);
    int a = first_with_params(&r, 0), b = a >= 0 ? first_with_params(&r, a + 1) : -1;
    CHECK(a >= 0 && b >= 0, "01A has two blocks with parameters");
    if (a < 0 || b < 0)
        return;
    int a_on = r.preset.slot[a].on;
    rig_toggle(&r, a);
    rig_toggle(&r, a);
    CHECK(!preset_scenes_used(&r.preset), "outside scene mode the four scenes stay alike");
    r.scene_mode = 1;

    /* switching a block changes the active scene only */
    rig_toggle(&r, a);
    CHECK((r.preset.scenes.on[0] >> a & 1u) == (unsigned)!a_on && (r.preset.scenes.on[1] >> a & 1u) == (unsigned)a_on,
          "toggle in scene 1 only");
    rig_set_dirty(&r, 0);
    int before = builds;
    CHECK(rig_scene(&r, 1) == 0 && rig_scene_active(&r) == 1, "to scene 2");
    CHECK(r.preset.slot[a].on == a_on && r.chain.slot[a].enabled == a_on, "scene 2 has the block as it was");
    CHECK(builds == before, "no chain rebuild");
    CHECK(!r.dirty, "switching scenes is not an edit");

    /* a scene parameter: its value per scene; the others shared */
    struct rig_param p;
    rig_param(&r, b, 0, &p);
    float base = p.value;
    CHECK(rig_scene_param_toggle(&r, b, 0) == 1 && rig_scene_param(&r, b, 0), "parameter 1 of the block per scene");
    CHECK(r.dirty, "marking is an edit");
    rig_param_nudge(&r, b, 0, 3);
    rig_param(&r, b, 0, &p);
    if (p.value == base) {                      /* at the top of its range */
        rig_param_nudge(&r, b, 0, -3);
        rig_param(&r, b, 0, &p);
    }
    float in2 = p.value;
    CHECK(in2 != base, "moved in scene 2");
    struct rig_param q;
    int shared = rig_param_count(&r, b) > 1 ? 1 : -1;
    float shared_v = 0;
    if (shared > 0) {
        rig_param_nudge(&r, b, shared, 2);
        rig_param(&r, b, shared, &q);
        shared_v = q.value;
    }
    rig_scene(&r, 0);
    rig_param(&r, b, 0, &p);
    CHECK(p.value == base, "scene 1 keeps its value (%g, %g)", p.value, base);
    CHECK(r.preset.slot[a].on == !a_on, "scene 1 has the block switched");
    if (shared > 0) {
        rig_param(&r, b, shared, &q);
        CHECK(q.value == shared_v, "a parameter of the preset is the same in every scene");
    }
    rig_scene(&r, 1);
    rig_param(&r, b, 0, &p);
    CHECK(p.value == in2, "scene 2 has its value back");

    /* the expression pedal moves it without changing the scene */
    r.performing++;
    rig_param_nudge(&r, b, 0, -2);
    r.performing--;
    int i = preset_scene_param(&r.preset, b, p.def.idx);
    CHECK(i >= 0 && r.preset.scenes.param[i].value[1] == in2, "pedal moves are not stored in the scene");

    /* names, copy */
    rig_scene_set_name(&r, 3, "solo");
    char name[SCENE_NAME_LEN + 1];
    rig_scene_name(&r, 3, name);
    CHECK(strcmp(name, "solo") == 0, "scene name");
    rig_scene_set_name(&r, 2, "CENA 3");
    CHECK(r.preset.scenes.name[2][0] == 0, "the default name is stored as none");
    rig_scene_copy(&r, 1, 3);
    CHECK(r.preset.scenes.on[3] == r.preset.scenes.on[1] && r.preset.scenes.param[i].value[3] == in2, "copy");

    /* saved with the preset and back */
    rig_scene(&r, 1);
    CHECK(rig_save(&r, 200) == 0, "save as 51A");
    rig_load(&r, 1);
    rig_load(&r, 200);
    CHECK(rig_scene_active(&r) == 1 && rig_scene_param(&r, b, 0), "loaded in scene 2, the scene parameter kept");
    rig_param(&r, b, 0, &p);
    CHECK(p.value == in2 && r.preset.slot[a].on == a_on, "scene 2's values play");
    rig_scene(&r, 0);
    rig_param(&r, b, 0, &p);
    CHECK(p.value == base && r.preset.slot[a].on == !a_on, "scene 1 from the flash");
    rig_scene_name(&r, 3, name);
    CHECK(strcmp(name, "solo") == 0, "name from the flash");

    /* a new block is on in every scene; a removed one leaves no scene parameter */
    int pos = -1;
    for (int k = 0; k < r.preset.count; k++)
        if (r.preset.order[k] == b)
            pos = k;
    CHECK(rig_remove(&r, pos) == 0 && !rig_scene_param(&r, b, 0) && r.preset.scenes.nparams == 0, "removed");
    for (int n = 0; n < SCENE_COUNT; n++)
        CHECK(!(r.preset.scenes.on[n] >> b & 1u), "removed block off in scene %d", n + 1);
}

/* one step of parameter k, up unless it is at the top */
static void move(struct rig *r, int slot, int k)
{
    struct rig_param p;
    rig_param(r, slot, k, &p);
    rig_param_nudge(r, slot, k, p.value < p.def.max ? 1 : -1);
}

static float value(struct rig *r, int slot, int k)
{
    struct rig_param p;
    rig_param(r, slot, k, &p);
    return p.value;
}

/* turning a parameter while the scenes are in play makes it a scene parameter (KNOBS NA CENA: AUTO) */
static void test_auto(void)
{
    static struct rig r;
    memset(&r, 0, sizeof r);
    CHECK(rig_init(&r, RIG_AVOID_NONE) == 0, "rig with the library");
    rig_load(&r, 0);
    int b = first_with_params(&r, 0);
    while (b >= 0 && rig_param_count(&r, b) <= 2)
        b = first_with_params(&r, b + 1);
    CHECK(b >= 0, "01A has a block with three parameters");
    if (b < 0 || rig_param_count(&r, b) <= 2)
        return;
    r.scene_auto = 1;
    float base = value(&r, b, 0);
    move(&r, b, 0);
    CHECK(!rig_scene_editing(&r) && !rig_scene_param(&r, b, 0), "no scenes in play: the preset's parameter");
    rig_param_set(&r, b, 0, base);

    r.scene_mode = 1;
    CHECK(rig_scene_editing(&r), "scene mode: scenes in play");
    rig_scene(&r, 1);
    move(&r, b, 0);
    float in2 = value(&r, b, 0);
    CHECK(rig_scene_param(&r, b, 0) && in2 != base, "turned in scene 2: a scene parameter");
    rig_scene(&r, 0);
    CHECK(value(&r, b, 0) == base, "scene 1 keeps the value it had");
    rig_scene(&r, 1);
    CHECK(value(&r, b, 0) == in2, "scene 2 its own");

    r.preset_wide++;                            /* a pasted block, a footswitch link */
    move(&r, b, 1);
    r.preset_wide--;
    CHECK(!rig_scene_param(&r, b, 1), "moves that stay the preset's are not captured");
    r.performing++;                             /* a pedal */
    move(&r, b, 1);
    r.performing--;
    CHECK(!rig_scene_param(&r, b, 1), "pedal moves are not captured");
    r.scene_auto = 0;
    move(&r, b, 1);
    CHECK(!rig_scene_param(&r, b, 1), "KNOBS NA CENA: MANUAL");
    r.scene_auto = 1;

    /* back to the stored scene */
    int on = r.preset.slot[b].on;
    CHECK(rig_save(&r, 201) == 0, "save as 51B");
    move(&r, b, 0);
    rig_toggle(&r, b);
    move(&r, b, 2);
    CHECK(value(&r, b, 0) != in2 && r.preset.slot[b].on != on && rig_scene_param(&r, b, 2), "scene 2 edited");
    float k2 = value(&r, b, 2);
    CHECK(rig_scene_revert(&r, 1) == 0, "revert scene 2");
    CHECK(value(&r, b, 0) == in2 && r.preset.slot[b].on == on, "scene 2 as stored");
    CHECK(value(&r, b, 2) != k2 && rig_scene_param(&r, b, 2), "a new scene parameter gets the stored value");
    rig_scene(&r, 0);
    CHECK(value(&r, b, 0) == base, "scene 1 untouched");

    /* the limit */
    int unused = -1;
    for (int s = PRESET_SLOTS - 1; s >= 0 && unused < 0; s--)
        if (!rig_slot_used(&r, s))
            unused = s;
    struct preset_scenes keep = r.preset.scenes;
    for (int i = r.preset.scenes.nparams; i < SCENE_PARAMS; i++)
        r.preset.scenes.param[i] = (struct scene_param){ (int8_t)unused, (int8_t)(i % PRESET_KNOBS), { 0 } };
    r.preset.scenes.nparams = SCENE_PARAMS;
    r.notice[0] = 0;
    move(&r, b, 1);
    CHECK(!rig_scene_param(&r, b, 1) && r.notice[0], "full: the preset's, and a notice (%s)", r.notice);
    r.preset.scenes = keep;
}

void test_scenes(void)
{
    test_format();
    test_largest();
    test_rig();
    test_auto();
}
