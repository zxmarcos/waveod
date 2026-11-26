/* waveod's modulators (engine/modulators.c, model/modulation.c, docs/modulators.md): the shapes and their timing,
   the sequencer, the envelope, the preset chunk, and the routes from the control table moving the chain's knobs. */
#include "engine/modulators.h"
#include "fw/wv_preset.h"
#include "model/control.h"
#include "model/modulation.h"
#include "test.h"
#include <math.h>
#include <string.h>

/* the value after `samples` samples (in blocks of 64), from a reset */
static float after(struct preset_mod *m, int samples, const float *in, uint32_t beat_ms)
{
    struct mod_state s = { 0 };
    mod_reset(&s, m);
    float v = mod_step(&s, m, in, 64, beat_ms);
    for (int done = 64; done <= samples; done += 64)
        v = mod_step(&s, m, in, 64, beat_ms);
    return v;
}

static void test_shapes(void)
{
    struct preset_mod m;
    preset_mod_default(&m, MOD_LFO);                    /* 1 Hz sine */
    CHECK(after(&m, 0, 0, 500) < 1e-6f, "sine starts at its bottom (%g)", after(&m, 0, 0, 500));
    float mid = after(&m, 22050, 0, 500), quarter = after(&m, 11025, 0, 500);
    CHECK(fabsf(mid - 1.0f) < 1e-3f && fabsf(quarter - 0.5f) < 0.01f, "1 Hz sine: top at 0.5 s (%g), half at 0.25 s (%g)",
          mid, quarter);
    m.phase = 50;
    CHECK(fabsf(after(&m, 0, 0, 500) - 1.0f) < 1e-4f, "phase 50%%: starts at the top");
    m.phase = 0;
    m.sync = 5;                                         /* 1/4: a beat */
    float synced = after(&m, 11025, 0, 500);            /* 500 ms per beat: the top at 250 ms */
    CHECK(fabsf(synced - 1.0f) < 1e-3f, "synced to 1/4 at 120 BPM: top at 250 ms (%g)", synced);
    m.sync = 0;
    m.shape = MS_SQUARE;
    CHECK(after(&m, 10000, 0, 500) == 1.0f && after(&m, 30000, 0, 500) == 0.0f, "square: 1 then 0");
    m.shape = MS_TRIANGLE;
    CHECK(fabsf(after(&m, 11025, 0, 500) - 0.5f) < 0.01f, "triangle: half way up at a quarter");
    m.shape = MS_SAW_DOWN;
    CHECK(fabsf(after(&m, 11025, 0, 500) - 0.75f) < 0.01f, "saw down: 0.75 at a quarter");
    m.shape = MS_RANDOM;
    m.rate = 20.0f;
    int in_range = 1, changes = 0;
    struct mod_state s = { 0 };
    mod_reset(&s, &m);
    float last = -1.0f;
    for (int b = 0; b < 2000; b++) {
        float v = mod_step(&s, &m, 0, 64, 500);
        in_range &= v >= 0.0f && v <= 1.0f;
        changes += v != last;
        last = v;
    }
    CHECK(in_range && changes > 50 && changes < 70, "random 20 Hz: in range, a new value per cycle (%d in 2.9 s)",
          changes);

    preset_mod_default(&m, MOD_SEQ);
    m.steps = 4;
    m.rate = 10.0f;                                     /* a step per 4410 samples */
    m.step[0] = 0, m.step[1] = 25, m.step[2] = 50, m.step[3] = 100;
    CHECK(after(&m, 100, 0, 500) == 0.0f && fabsf(after(&m, 6000, 0, 500) - 0.25f) < 1e-6f &&
          fabsf(after(&m, 15000, 0, 500) - 1.0f) < 1e-6f && after(&m, 18000, 0, 500) == 0.0f,
          "sequencer: 4 steps at 10 Hz, then again");
    m.glide = 50;
    float g = after(&m, 4410 + 1100, 0, 500);           /* a quarter into step 2, half way through its glide */
    CHECK(g > 0.08f && g < 0.17f, "sequencer glide: sliding into the step (%g)", g);

    preset_mod_default(&m, MOD_ENV);                    /* 10 / 200 ms */
    static float loud[128], quiet[128];
    for (int i = 0; i < 128; i++)
        loud[i] = (i & 2) ? 0.1f : -0.1f;               /* -20 dB */
    struct mod_state e = { 0 };
    mod_reset(&e, &m);
    float v = 0.0f;
    for (int b = 0; b < 100; b++)
        v = mod_step(&e, &m, loud, 64, 500);
    CHECK(fabsf(v - 40.0f / 60.0f) < 0.01f, "envelope: -20 dB in = %g (want %g)", v, 40.0 / 60.0);
    for (int b = 0; b < 700; b++)
        v = mod_step(&e, &m, quiet, 64, 500);
    CHECK(v < 0.01f, "envelope: silence brings it down (%g)", v);
}

static void test_chunk(void)
{
    static struct preset p, q;
    static uint8_t buf[WVP_MAX];
    preset_clear(&p, "MODS");
    int n0 = wvp_encode(&p, buf, sizeof buf);
    preset_mod_default(&p.mods.m[1], MOD_SEQ);
    p.mods.m[1].steps = 5;
    p.mods.m[1].step[3] = 77;
    p.mods.m[1].glide = 30;
    preset_mod_default(&p.mods.m[3], MOD_LFO);
    p.mods.m[3].shape = MS_SMOOTH;
    p.mods.m[3].sync = 8;
    p.mods.m[3].rate = 3.5f;
    p.mods.m[3].phase = 25;
    int n = wvp_encode(&p, buf, sizeof buf);
    int ok = n > n0 && wvp_decode(buf, (unsigned)n, &q) == 0 && memcmp(&p.mods, &q.mods, sizeof p.mods) == 0;
    CHECK(ok, "modulators kept through waveod's format (%d bytes more)", n - n0);
    CHECK(q.mods.m[0].type == MOD_OFF && q.mods.m[2].type == MOD_OFF, "the unused ones stay off");
}

/* a modulator linked to a parameter of preset 01A moves the chain's knob */
static void test_routes(void)
{
    static struct rig r;
    rig_init(&r, RIG_AVOID_NONE);
    int slot = -1, k = -1;
    struct rig_param p;
    for (int s = 0; s < PRESET_SLOTS && slot < 0; s++)          /* a parameter with a wide range */
        for (int i = 0; i < rig_param_count(&r, s) && slot < 0; i++)
            if (rig_param(&r, s, i, &p) == 0 && (p.def.max - p.def.min) / p.def.step >= 50)
                slot = s, k = i;
    if (slot < 0) {
        CHECK(0, "a wide parameter in 01A");
        return;
    }
    rig_param(&r, slot, k, &p);
    int at = control_add_param(&r, CK_MOD1 + 2, CM_ALL, CW_PRESS, slot, k);
    struct mod_route routes[MOD_ROUTES];
    CHECK(at >= 0 && modulation_routes(&r, routes, MOD_ROUTES) == 0, "a link to an off modulator: no route");
    preset_mod_default(&r.preset.mods.m[2], MOD_LFO);
    int n = modulation_routes(&r, routes, MOD_ROUTES);
    CHECK(n == 1 && routes[0].mod == 2 && routes[0].slot == slot && routes[0].knob == p.def.idx &&
          routes[0].off == p.def.min && routes[0].on == p.def.max && routes[0].step == 0.0f,
          "MOD 3 -> %s: one route over the whole range, continuous", p.def.name);

    struct preset_mods mods = r.preset.mods;
    mods.m[2].phase = 50;                                /* starts at the top */
    mods_set(&mods, routes, n);
    mods_restart();
    float in[128] = { 0 };
    mods_run(&r.chain, in, 64, 500);
    float top = r.chain.slot[slot].knobs[p.def.idx];
    mods_run(0, in, 64, 500);                            /* not playing: nothing written */
    r.chain.slot[slot].knobs[p.def.idx] = -1234.0f;
    mods_run(0, in, 64, 500);
    CHECK(fabsf(top - p.def.max) < 1e-3f * (p.def.max - p.def.min) && r.chain.slot[slot].knobs[p.def.idx] == -1234.0f,
          "the knob at the top of its range (%g, max %g); nothing written while the chain does not play", top,
          p.def.max);
    CHECK(fabsf(mods_value(2) - 1.0f) < 1e-3f, "the screens read MOD 3 at %g", mods_value(2));
    routes[0].step = 10.0f * p.def.step;                 /* stepped: a value on the grid */
    routes[0].curve = CC_SLOW;
    mods_set(&mods, routes, n);
    mods_run(&r.chain, in, 64, 500);
    float v = r.chain.slot[slot].knobs[p.def.idx], grid = (v - p.def.min) / routes[0].step;
    CHECK(fabsf(grid - floorf(grid + 0.5f)) < 1e-3f, "a stepped route lands on its steps (%g)", v);
    mods_set(&mods, routes, 0);
}

void test_mods(void)
{
    test_shapes();
    test_chunk();
}

void test_mods_rig(void)
{
    test_routes();
}
