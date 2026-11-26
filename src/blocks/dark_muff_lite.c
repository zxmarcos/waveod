/*
 * Dark Muff Lite (DRV › Circuit, waveod's): Dark Muff with cheaper cuts, research/pulse's Dark Muff Lite (the same
 * samples). The same simulation of the circuit, with Q4 (input booster) and Q3 linearised (Q3's diodes and Q2's
 * stay, their solves tabulated per pot setting), the stages not loading each other back, and the tone stack loading
 * Q2 as a resistance (the tone stack itself is exact, at 1x). Q4's stage at 1x, the two clipping stages at 4x.
 * Against Dark Muff: -29..-33 dB in the research's guitar phrases, the same timbre, less than half the instructions.
 * What it leaves out: Q4 saturating on a hot input (> ~0.5 V peaks). Docs: docs/drives.md.
 *
 *   Sustain, Tone, Volume   as Dark Muff
 */
#include "circuits/drive.h"
#include "circuits/bigmuff_rams73_lite.h"
#define BML_V BML_RAMS73
#include "circuits/bigmuff_lite.h"

enum { K_SUSTAIN, K_TONE, K_VOLUME, NKNOBS };

static const struct wvfx_param params[] = {
    { "Sustain", 0.0f, 100.0f, 1.0f, 70.0f, K_SUSTAIN, 0, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 40.0f, K_TONE, 0, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, K_VOLUME, 0, 0, 0, 0, 0 },
};

struct state {
    struct drive d;
    float rate;
    bigmuff_lite_t muff;
    os4_t os;
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[3] = { drive_rot(k[K_SUSTAIN]), drive_rot(k[K_TONE]), drive_rot(k[K_VOLUME]) };
    bml_build(&s->muff, set, s->d.work, rot, s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    bml_init(&s->muff);
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
    return bml_tail(&s->muff, os4_run(&s->os, bml_s1_run(&s->muff, x), bml_os_rest, &s->muff));
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->muff.set = dk_current(&s->d.cur);
    drive_process(&s->d, io, run, s);
}

const struct wvfx_desc dark_muff_lite_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 2),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dark Muff Lite",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
