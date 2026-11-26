/*
 * Treble Boost (DRV › Circuit, waveod's): one germanium transistor, the Rangemaster circuit (OC44), from its
 * netlist (tools/circuits/drives.py treble; docs/drives.md).
 *
 * One stage at 4x, the whole pedal: 5n in (the treble: it lets little below 1 kHz through), 470k / 68k bias, 3k9 +
 * 47u in the emitter, BOOST (10k, log) as the collector load with the output taken from its wiper through 10n. The
 * transistor keeps its Ebers-Moll junctions (two nonlinear ports): it compresses and clips asymmetrically when the
 * amp after it is pushed. Germanium: Is 1 uA, beta 100; written as NPN on a positive supply (mirrored).
 *
 *   Boost   the pot (audio taper)
 */
#define DK_PNJLIM 1                     /* junction steps limited as SPICE does (sdk/wvdk.h) */
#define DK_PREDICT 1                    /* Newton stops on its next step estimated (sdk/wvdk.h) */
#define DK_EXTRAP 1                     /* and starts extrapolated: one iteration nearly always */
#include "circuits/drive.h"
#include "circuits/treble.h"

DK_RUN(treble_s1, TREBLE_S1)

enum { K_BOOST, NKNOBS };

static const struct wvfx_param params[] = {
    { "Boost", 0.0f, 100.0f, 1.0f, 70.0f, K_BOOST, 0, 0, 0, 0, 0 },
};

struct state {
    struct drive d;
    float rate;
    unsigned set;
    dk_stage s1;
    os4_t os;
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[1] = { drive_log(drive_rot(k[K_BOOST])) };
    dk_build(&s->s1, &s->s1.m[set], s->d.work, rot, 4.0f * s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    s->set = 0;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    dk_init(&s->s1, &TREBLE_S1);
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

static inline float stage(void *ctx, float u)
{
    struct state *s = ctx;
    float y;
    treble_s1(&s->s1, &s->s1.m[s->set], &u, &y);
    return y;
}

static inline float run(void *ctx, float x)
{
    struct state *s = ctx;
    return os4_run(&s->os, x, stage, s);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->set = dk_current(&s->d.cur);
    drive_process(&s->d, io, run, s);
}

const struct wvfx_desc treble_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 6),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Treble Boost",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
