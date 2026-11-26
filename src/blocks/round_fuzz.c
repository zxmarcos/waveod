/*
 * Round Fuzz (DRV › Circuit, waveod's): two germanium transistors with feedback, the Fuzz Face circuit (AC128s),
 * from its netlist (tools/circuits/drives.py fuzz; docs/drives.md).
 *
 * One stage at 4x (or 2x, Quality), the whole pedal: 2u2 in, Q1 with 33k on its collector driving Q2's base, Q2
 * with 8k2 + 470R on its collector, 100k from Q2's emitter back to Q1's base, FUZZ (1k, linear) in Q2's emitter with
 * 22u on its wiper, the output between the 470R and the 8k2 through 10n into VOLUME (500k, log). Both transistors
 * keep their Ebers-Moll junctions (four nonlinear ports, solved together): Q1 near saturation at rest, Q2 cutting off
 * and saturating on the signal, the bias moving with Fuzz, as on the pedal. Germanium: Is 1 uA, beta 75; written as
 * NPN on a positive supply (the PNP circuit mirrored: the output's polarity inverted).
 *
 *   Fuzz, Volume   the pots (Fuzz linear: 100 = Q2's emitter fully bypassed; Volume audio taper)
 *   Quality        the oversampling: 2x by default (~183k cycles a block on the pedal; -38..-52 dB from the
 *                  circuit, aliasing on the highest notes) or 4x (-49..-54 dB, ~340k: heavy with a full chain)
 */
#define DK_PNJLIM 1                     /* junction steps limited as SPICE does (sdk/wvdk.h) */
#define DK_PREDICT 1                    /* Newton stops on its next step estimated (sdk/wvdk.h) */
#include "circuits/drive.h"
#include "circuits/fuzz.h"

DK_RUN(fuzz_s1, FUZZ_S1)

enum { K_FUZZ, K_VOLUME, K_QUALITY, NKNOBS };

static const struct wvfx_param params[] = {
    { "Fuzz", 0.0f, 100.0f, 1.0f, 100.0f, K_FUZZ, 0, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, K_VOLUME, 0, 0, 0, 0, 0 },
    { "Quality", 0.0f, 1.0f, 1.0f, 0.0f, K_QUALITY, 0, 0, 0, "2x\0" "4x\0", 0 },
};

struct state {
    struct drive d;
    float rate;
    unsigned set;
    dk_stage s1;
    os4_t os;
    uint8_t os_of[2];                   /* each matrix set's oversampling: 2 or 4 */
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[2] = { 1.0f - drive_rot(k[K_FUZZ]), drive_log(drive_rot(k[K_VOLUME])) };
    s->os_of[set] = k[K_QUALITY] >= 0.5f ? 4 : 2;
    dk_build(&s->s1, &s->s1.m[set], s->d.work, rot, (float)s->os_of[set] * s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    s->set = 0;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    dk_init(&s->s1, &FUZZ_S1);
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
    fuzz_s1(&s->s1, &s->s1.m[s->set], &u, &y);
    return y;
}

static inline float run2(void *ctx, float x)
{
    return os2_run(&((struct state *)ctx)->os, x, 0, stage, ctx);
}

static inline float run4(void *ctx, float x)
{
    return os4_run(&((struct state *)ctx)->os, x, stage, ctx);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->set = dk_current(&s->d.cur);
    if (s->os_of[s->set] == 2)
        drive_process(&s->d, io, run2, s);
    else
        drive_process(&s->d, io, run4, s);
}

const struct wvfx_desc round_fuzz_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 5),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Round Fuzz",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
