/*
 * Sardar Dist (DRIVE › Distortion): the original's Sardar Dist (QME-200 V1.2.2, library entry 3.21, id 0x03000052)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). The effect runs on the left input; both
 * outputs crossfade from their own input to it. Single precision except Gain's taper.
 *
 * Per sample: a fixed 1st-order input low pass, a 2nd-order Tight section (no x1 term), a 2nd-order gain section
 * (Gain), a hard clip at +-3.2 on 4 points per sample with the anti-aliasing FIR, a fixed 2nd-order section, x 0.5, a
 * 5th-degree rational clipper above 0.16 (below it the signal passes) with the FIR, a fixed 1st-order section, a
 * 1st-order presence section (Presence), a hard clip at +-1.76 with the FIR, 1st-order treble (Treble), 2nd-order
 * bass (Bass), 1st-order level (Volume), x 0.52, a fixed 2nd-order bell, x 0.8. The output is dry + (wet - dry) m,
 * m gliding 0.01 per sample towards 1 while the block is on and 0 while it is off: the original runs the whole path
 * in bypass too and fades itself out. The knobs are read as they are at the start of a block while m was not above
 * the on/off state.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
    { "Tight", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0, 0, 0, 0 },
};

static const struct fos INLP = { 0.9187342524528503f, 0.9187342524528503f, 0.9238295555114746f };
static const struct sos G3 = {
    -3.424217939376831f, 1.293463945388794f, 2.130753993988037f, 1.0f, -1.4167189598083496f, 0.4289016127586365f,
};
static const struct fos TRIM = { -7.046713352203369f, -2.814091444015503f, -0.10356315970420837f };
static const struct sos BELL = {
    0.9614404439926147f, -1.6639236211776733f, 0.7032349109649658f, 1.0f, -1.6639236211776733f, 0.6646753549575806f,
};
#define PRES_B 0.11337868124246597f

struct state {
    float gain, volume, bass, treble, presence, tight, mix;
    struct sos tc, gc, bc;              /* Tight, gain, bass sections */
    struct fos pc, trc, vc;             /* presence (a0 in pa0), treble, level */
    float pa0;
    struct fos_state in, tr, pr, ts, vs;
    struct sos_state tgs, gs, g3s, bs, bell;
    struct stage st1, st2, st3;
};

static void tight_set(struct state *s, float t)
{
    float u = 1.0f - t;
    s->tc = (struct sos){ u * 1.3989542722702026f + 0.065750852227211f, 0.0f,
                          -0.065750852227211f - u * 1.3989542722702026f, 1.0577658414840698f - t,
                          st_fma(u, -0.8900101184844971f, -0.030389638617634773f),
                          st_fma(u, -0.10998991131782532f, -0.0044945161789655685f) };
}

static void gain_set(struct sos *c, float g)
{
    float b1 = st_fma(g, -1.9897459745407104f, -0.004130075220018625f);
    *c = (struct sos){ st_fma(g, 11.90884017944336f, 0.05032256990671158f), b1,
                       st_fma(g, -9.91909122467041f, -0.045697759836912155f), g + 0.026323119178414345f, b1,
                       st_fma(g, 0.9897456765174866f, -0.021698320284485817f) };
}

static void presence_set(struct state *s, float p)
{
    s->pc = (struct fos){ PRES_B, PRES_B, p + -1.106621265411377f };
    s->pa0 = 1.3333786725997925f - p;
}

/* from the raw knobs (0-100) */
static void treble_set(struct fos *c, float T)
{
    float u = T * 0.00976445060223341f;
    *c = (struct fos){ u + 0.023554906249046326f, 0.023554906249046326f - u, -0.952890157699585f };
}

static void bass_set(struct sos *c, float B)
{
    float b1 = st_fma(B, -0.019845040515065193f, 2.311939239501953f);
    *c = (struct sos){ B * 0.009999999776482582f + -1.1667938232421875f, b1, B * 0.009845040738582611f + -1.145161509513855f,
                       B * 0.009999999776482582f + -1.1642369031906128f, b1,
                       B * 0.009845040738582611f + -1.1477183103561401f };
}

static void volume_set(struct fos *c, float V)
{
    *c = (struct fos){ V * 0.008326181210577488f, V * -0.008326181210577488f, -0.9982836246490479f };
}

static float gain_of(float G)
{
    return (float)st_fmad((double)G, 0.0009499999575316911, 0.004999999888241291);
}

/* the no-x1 section divided by a0 */
static inline float nox1_run_div(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b0, x, c->b2 * s->x2);
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, (n - d) / c->a0);
}

/* the original's stages here start their points from the stage's last output, not its last input (so prev is not
   the last sample's newest point: all 4 clipped here, and the stage runs sample by sample: each
   sample's points start from the last one's FIR output) */
static inline float stage_fb_run(struct stage *s, float x, float (*clip)(float))
{
    float q[4];
    stage_points(s, x, q);
#pragma clang loop unroll(full)
    for (int i = 0; i < 4; i++)
        q[i] = clip(q[i]);
    float y = stage_fir(s, q);
    s->prev = y;
    return y;
}

/* x held at +-3.2, +-1.76 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip32(float x) { return __builtin_fminf(__builtin_fmaxf(x, -3.2f), 3.2f); }
static inline float clip176(float x) { return __builtin_fminf(__builtin_fmaxf(x, -1.76f), 1.76f); }

/* the clipper above 0.16 */
static inline float clip_poly(float x)
{
    if (!(x > 0.1599999964237213f))
        return x;
    float x2 = x * x, a = st_abs(x), x3 = x2 * a, x4 = x3 * a, x5 = x4 * a;
    float n = st_fma(x2, -2129.0146484375f, -2.047436475753784f);
    n = st_fma(a, 962.4060668945312f, n);
    n = st_fma(x3, 7612.3046875f, n);
    n = st_fma(x4, 610.9527587890625f, n);
    n = st_fma(x5, 1.4591084718704224f, n);
    float d = st_fma(x2, 93.49729919433594f, 842.25537109375f);
    d = st_fma(a, -515.3286743164062f, d);
    d = st_fma(x3, 12168.693359375f, d);
    d = st_fma(x4, 783.0485229492188f, d);
    return n / (d + x5);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->bass = k[2] * 0.01f;
    s->treble = k[3] * 0.01f;
    s->presence = k[4] * 0.01f;
    s->volume = k[1] * 0.01f;
    s->gain = gain_of(k[0]);
    s->tight = k[5] * 0.01f;
    s->mix = 1.0f;
    tight_set(s, s->tight);
    gain_set(&s->gc, s->gain);
    presence_set(s, s->presence);
    treble_set(&s->trc, k[3]);
    bass_set(&s->bc, k[2]);
    volume_set(&s->vc, k[1]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float on = io->enabled ? 1.0f : 0.0f;
    const float *k = io->knobs;
    if (s->mix <= on) {
        float t = k[5] * 0.01f, g = gain_of(k[0]), pr = k[4] * 0.01f, tr = k[3] * 0.01f, b = k[2] * 0.01f;
        if (s->tight != t) {
            s->tight = t;
            tight_set(s, t);
        }
        if (s->gain != g) {
            s->gain = g;
            gain_set(&s->gc, g);
        }
        if (s->presence != pr) {
            s->presence = pr;
            presence_set(s, pr);
        }
        if (s->treble != tr) {
            s->treble = tr;
            treble_set(&s->trc, k[3]);
        }
        if (s->bass != b) {
            s->bass = b;
            bass_set(&s->bc, k[2]);
        }
        if (s->volume != k[1] * 0.01f) {
            s->volume = k[1] * 0.01f;
            volume_set(&s->vc, k[1]);
        }
    }
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = nox1_run_div(&s->tc, &s->tgs, fos_run(&INLP, &s->in, p[0]));
        x = stage_fb_run(&s->st1, sos_run_div(&s->gc, &s->gs, x), clip32);
        x = stage_fb_run(&s->st2, sos_run(&G3, &s->g3s, x) * 0.5f, clip_poly);
        x = fos_run_div(&s->pc, s->pa0, &s->pr, fos_run(&TRIM, &s->ts, x));
        x = stage_fb_run(&s->st3, x, clip176);
        x = sos_run_div(&s->bc, &s->bs, fos_run(&s->trc, &s->tr, x));
        x = fos_run(&s->vc, &s->vs, x) * 0.5199999809265137f;
        float w = sos_run(&BELL, &s->bell, x) * 0.800000011920929f;
        s->mix = st_glide_by(s->mix, on, 0.01f);
        p[0] = st_fma(w - p[0], s->mix, p[0]);
        p[1] = st_fma(w - p[1], s->mix, p[1]);
    }
}

const struct wvfx_desc sardar_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x115),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sardar Dist",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
