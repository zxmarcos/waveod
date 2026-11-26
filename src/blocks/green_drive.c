/*
 * Green Drive (DRV › Circuit, waveod's): an overdrive with two diodes in an op-amp's feedback, the TS808 circuit,
 * from its netlist (tools/circuits/drives.py green; docs/drives.md).
 *
 *   S1: the input buffer and the clipping amplifier - 4k7 + 47n to ground (the 720 Hz low cut), 51k + DRIVE (500k,
 *       log) in the feedback with 51p and the two 1N4148, the op-amp (3 MHz) linear: the diodes are the one
 *       nonlinear port, their solve tabulated per Drive setting;
 *   S2, linear: the 1k / 220n low pass, the active tone control (TONE 20k on the op-amp's two inputs, 220R + 220n on
 *       its wiper), LEVEL (100k, log), the output buffer.
 * S1 at 4x, S2 at 2x after the first decimation (at 1x it would bend its highs: -1 dB at 8 kHz; at 2x the DI is
 * -62..-65 dB from the circuit, -70..-74 at 4x, for 12 % fewer cycles). The op-amp's output drives the tone stage,
 * so the cut between the stages is exact.
 *
 *   Drive, Tone, Level   the pots (Drive and Level audio taper, Tone linear)
 */
#include "circuits/drive.h"
#include "circuits/green.h"

DK_RUN(green_s1, GREEN_S1)
DK_RUN(green_s2, GREEN_S2)

#define NTAB 512
#define QMAX 6.0f

enum { K_DRIVE, K_TONE, K_LEVEL, NKNOBS };

static const struct wvfx_param params[] = {
    { "Drive", 0.0f, 100.0f, 1.0f, 50.0f, K_DRIVE, 0, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, K_TONE, 0, 0, 0, 0, 0 },
    { "Level", 0.0f, 100.0f, 1.0f, 50.0f, K_LEVEL, 0, 0, 0, 0, 0 },
};

struct state {
    struct drive d;
    float rate;
    unsigned set;
    dk_stage s1, s2;
    os4_t os;
    float tab[2][NTAB + 1];
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[3] = { drive_log(drive_rot(k[K_DRIVE])), drive_rot(k[K_TONE]), drive_log(drive_rot(k[K_LEVEL])) };
    dk_build(&s->s1, &s->s1.m[set], s->d.work, rot, 4.0f * s->rate);
    dk_tab_build(&s->s1, &s->s1.m[set], s->tab[set], NTAB, QMAX);
    dk_build(&s->s2, &s->s2.m[set], s->d.work, rot, 2.0f * s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    s->set = 0;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    dk_init(&s->s1, &GREEN_S1);
    dk_init(&s->s2, &GREEN_S2);
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

static inline float stage1(void *ctx, float u)
{
    struct state *s = ctx;
    float c;
    green_s1(&s->s1, &s->s1.m[s->set], &u, &c);
    return c;
}

static inline float stage2(void *ctx, float c)
{
    struct state *s = ctx;
    float y;
    green_s2(&s->s2, &s->s2.m[s->set], &c, &y);
    return y;
}

static inline float run(void *ctx, float x)
{
    struct state *s = ctx;
    return os4_run2(&s->os, x, 0, stage1, stage2, s);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->set = dk_current(&s->d.cur);
    drive_process(&s->d, io, run, s);
}

const struct wvfx_desc green_drive_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 3),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Green Drive",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
