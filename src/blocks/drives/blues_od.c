/*
 * Blues OD (DRIVE › OD): the original's Blues OD (QME-200 V1.2.2, library entry 3.5, id 0x03000009) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: a 2nd-order gain section (Gain, A25), a fixed 3rd-order section, the rational clipper (drive.h) on 4
 * points per sample with the anti-aliasing FIR, a second 2nd-order gain section, a hard clip at +-4 on 4 points with
 * the FIR again, a 2nd-order tone section (Tone, B25), a fixed 2nd-order section, x 0.6 x Volume (A25). Volume glides
 * 0.005 per block (also while bypassed); Gain and Tone are read as they are, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 60.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct sos3 PRE = {
    0.16084496676921844f, -0.44240814447402954f, 0.40339550375938416f, -0.12183234840631485f,
    1.0f, -2.732818603515625f, 2.4669156074523926f, -0.7340967059135437f,
};
static const struct sos POST = {
    1.00299072265625f, -1.9975732564926147f, 0.9948745965957642f, 1.0f, -1.9975732564926147f, 0.9978652596473694f,
};
static const struct clip_rat CLIP = {
    38.779998779296875f, 13.609999656677246f, 1.4930000305175781f, 36.040000915527344f, 52.0099983215332f,
    14.6899995803833f,
};

struct state {
    float gain, tone, volume;   /* Gain and Tone as read (tapers), Volume glided (taper) */
    struct sos g1, g2, tc;
    struct sos_state g1s, g2s, ts, post;
    struct sos3_state pre;
    struct stage st1, st2;
};

static void gain_set(struct state *s, float g)
{
    float b1 = st_fma(g, -1.904054045677185f, -0.07497578114271164f);
    s->g1 = (struct sos){ st_fma(g, 154.10580444335938f, 14.526229858398438f), b1,
                          st_fma(g, -152.2017059326172f, -14.266090393066406f), g + 1.0529249906539917f, b1,
                          st_fma(g, 0.9040536880493164f, -0.7927873134613037f) };
    b1 = st_fma(g, -1.9897459745407104f, -0.25799599289894104f);
    s->g2 = (struct sos){ st_fma(g, 52.27153015136719f, 7.353356838226318f), b1,
                          st_fma(g, -50.28179168701172f, -7.086060047149658f), g + 0.5855147242546082f, b1,
                          st_fma(g, 0.9897456765174866f, -0.31821781396865845f) };
}

static void tone_set(struct state *s, float t)
{
    s->tc = (struct sos){ st_fma(t, 0.5078752040863037f, 0.03199011832475662f),
                          st_fma(t, -0.7827082872390747f, 0.014678889885544777f),
                          st_fma(t, 0.2748331129550934f, -0.017311230301856995f),
                          1.0f, -1.3979339599609375f, 0.45664939284324646f };
}

/* x held at +-4 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip4(float x)
{
    return __builtin_fminf(__builtin_fmaxf(x, -4.0f), 4.0f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[0]);
    s->tone = st_taper(st_b25, k[1]);
    s->volume = st_taper(st_a25, k[2]);
    gain_set(s, s->gain);
    tone_set(s, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->volume = st_glide_by(s->volume, st_taper(st_a25, io->knobs[2]), 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float g = st_taper(st_a25, io->knobs[0]), t = st_taper(st_b25, io->knobs[1]);
    if (s->gain != g) {
        s->gain = g;
        gain_set(s, g);
    }
    if (s->tone != t) {
        s->tone = t;
        tone_set(s, t);
    }
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block_div(&s->g1, &s->g1s, x, n);
    for (int i = 0; i < n; i++)
        x[i] = sos3_run_div(&PRE, &s->pre, x[i]);
    STAGE_BLOCK_POINTS(&s->st1, x, n, buf, clip_rat(&CLIP, v));
    stage_block_fir_ord(&s->st1, buf, x, n, AMP_FIR_ORDER2);
    for (int i = 0; i < n; i++)
        x[i] = sos_run_div_b1(&s->g2, &s->g2s, x[i]);
    STAGE_BLOCK_POINTS(&s->st2, x, n, buf, clip4(v));
    stage_block_fir(&s->st2, buf, x, n);
    for (int i = 0; i < n; i++)
        x[i] = sos_run_div_b1(&s->tc, &s->ts, x[i]);
    sos_block_div(&POST, &s->post, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.6000000238418579f * s->volume;
}

const struct wvfx_desc blues_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x105),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Blues OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
