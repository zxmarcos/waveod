/*
 * Rodent Dist (DRV › Circuit, waveod's): a distortion, an op-amp at high gain then two diodes to ground, the RAT
 * circuit, from its netlist (tools/circuits/drives.py rodent; docs/drives.md).
 *
 *   S1: 22n / 1M / 1k / 1n into the LM308, DISTORTION (100k, log) with 100p in its feedback, 47R + 2u2 and 560R + 4u7
 *       to ground. The op-amp as the LM308 with its 30p: 1 MHz, its input stage's current limited (a tanh: the
 *       0.3 V/us slew rate, the RAT's character at high gain), its output clamped +-3 V around the bias (rails) -
 *       three nonlinear ports solved together;
 *   S2: 4u7 + 1k into the two 1N914 to ground (one port, tabulated per setting), FILTER (100k, log) + 1k5 into 3n3,
 *       22n / 1M into the JFET buffer;
 *   S3, at 1x: the buffer, 1u into VOLUME (100k, log).
 * S1 and S2 at 4x or 2x (Quality). The op-amp's output and the buffer drive what follows them, so the cuts are exact.
 *
 *   Distortion, Filter, Volume   the pots (audio taper)
 *   Quality                      the oversampling: 2x by default (~155k cycles a block on the pedal, -48..-61 dB
 *                                from the circuit) or 4x (-61..-74 dB, ~295k)
 */
#define DK_PNJLIM 1                     /* junction steps limited as SPICE does (sdk/wvdk.h) */
#define DK_PREDICT 1                    /* Newton stops on its next step estimated (sdk/wvdk.h) */
#include "circuits/drive.h"
#include "circuits/rodent.h"

DK_RUN(rodent_s1, RODENT_S1)
DK_RUN(rodent_s2, RODENT_S2)
DK_RUN(rodent_s3, RODENT_S3)

#define NTAB 512
#define QMAX 6.0f

enum { K_DISTORTION, K_FILTER, K_VOLUME, K_QUALITY, NKNOBS };

static const struct wvfx_param params[] = {
    { "Distortion", 0.0f, 100.0f, 1.0f, 50.0f, K_DISTORTION, 0, 0, 0, 0, 0 },
    { "Filter", 0.0f, 100.0f, 1.0f, 30.0f, K_FILTER, 0, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, K_VOLUME, 0, 0, 0, 0, 0 },
    { "Quality", 0.0f, 1.0f, 1.0f, 0.0f, K_QUALITY, 0, 0, 0, "2x\0" "4x\0", 0 },
};

struct state {
    struct drive d;
    float rate;
    unsigned set;
    dk_stage s1, s2, s3;
    os4_t os;
    float tab[2][NTAB + 1];
    uint8_t os_of[2];                   /* each matrix set's oversampling: 2 or 4 */
};

static void build(struct state *s, unsigned set, const float *k)
{
    float rot[3] = { drive_log(drive_rot(k[K_DISTORTION])), drive_log(drive_rot(k[K_FILTER])),
                     drive_log(drive_rot(k[K_VOLUME])) };
    s->os_of[set] = k[K_QUALITY] >= 0.5f ? 4 : 2;
    float fs = (float)s->os_of[set] * s->rate;
    dk_build(&s->s1, &s->s1.m[set], s->d.work, rot, fs);
    dk_build(&s->s2, &s->s2.m[set], s->d.work, rot, fs);
    dk_tab_build(&s->s2, &s->s2.m[set], s->tab[set], NTAB, QMAX);
    dk_build(&s->s3, &s->s3.m[set], s->d.work, rot, s->rate);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    s->rate = env->rate;
    s->set = 0;
    drive_init(&s->d, buffer, knobs, NKNOBS);
    dk_init(&s->s1, &RODENT_S1);
    dk_init(&s->s2, &RODENT_S2);
    dk_init(&s->s3, &RODENT_S3);
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

static inline float gain_clip(void *ctx, float u)
{
    struct state *s = ctx;
    float a, b;
    rodent_s1(&s->s1, &s->s1.m[s->set], &u, &a);
    rodent_s2(&s->s2, &s->s2.m[s->set], &a, &b);
    return b;
}

static inline float tail(struct state *s, float g)
{
    float y;
    rodent_s3(&s->s3, &s->s3.m[s->set], &g, &y);
    return y;
}

static inline float run2(void *ctx, float x)
{
    struct state *s = ctx;
    return tail(s, os2_run(&s->os, x, 0, gain_clip, s));
}

static inline float run4(void *ctx, float x)
{
    struct state *s = ctx;
    return tail(s, os4_run(&s->os, x, gain_clip, s));
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

const struct wvfx_desc rodent_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 4),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Rodent Dist",
    .category = "Circuit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(dk_work),
    .init = init,
    .process = process,
    .poll = poll,
};
