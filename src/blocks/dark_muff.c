/*
 * Dark Muff (DRV › Circuit, waveod's): the Big Muff Pi "Ram's Head" (Version 2, 1973 #3, Kit Rae's tracing) from
 * its circuit, the Muff of research/pulse's Dark Muff (heard on the pedal: the same samples).
 *
 * The whole pedal is simulated (circuits/bigmuff.h, sdk/wvdk.h) from tools/circuits/bigmuff.py's netlist: the four
 * transistor stages, the diodes, every capacitor and the three pots. Q4 (input booster) keeps its full Ebers-Moll
 * junctions - a hot input saturates and cuts it off - and Q3 (first clipping stage) its base-emitter junction;
 * Q2 and Q1 are linearised at their operating points. Rates: Q4's stage at 2x, the two clipping stages at 4x, tone
 * stack / Q1 / volume (linear) at 1x. Docs: docs/drives.md.
 *
 *   Sustain, Tone, Volume   the pots' rotations, linear taper as traced
 */
#include "circuits/drive.h"
#include "circuits/bigmuff_rams73.h"
#define BM_V BM_RAMS73
#include "circuits/bigmuff.h"

enum { K_SUSTAIN, K_TONE, K_VOLUME, NKNOBS };

static const struct wvfx_param params[] = {
    { "Sustain", 0.0f, 100.0f, 1.0f, 70.0f, K_SUSTAIN, 0, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 40.0f, K_TONE, 0, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, K_VOLUME, 0, 0, 0, 0, 0 },
};

struct state {
    struct drive d;
    float rate;
    bigmuff_t muff;
    os4_t os;
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[3] = { drive_rot(k[K_SUSTAIN]), drive_rot(k[K_TONE]), drive_rot(k[K_VOLUME]) };
    bm_build(&s->muff, set, s->d.work, rot, s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    bm_init(&s->muff);
    os4_init(&s->os);
    build(s, 0, knobs);
    return 0;
}

static int poll(void *state, const float *knobs)
{
    struct state *s = state;
    int set = drive_stale(&s->d, knobs, NKNOBS);
    if (set < 0)
        return 0;
    build(s, (unsigned)set, knobs);
    dk_publish(&s->d.cur, (unsigned)set);
    return 1;
}

static inline float run(void *ctx, float x)
{
    struct state *s = ctx;
    return bm_tail(&s->muff, os4_run2(&s->os, x, bm_os_s1, bm_os_rest, 0, &s->muff));
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->muff.set = dk_current(&s->d.cur);
    drive_process(&s->d, io, run, s);
}

const struct wvfx_desc dark_muff_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 1),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dark Muff",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
