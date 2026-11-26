/*
 * COMP 2 (DYN): the original's COMP 2 (QME-200 V1.2.2, library entry 0.1, id 0x00000001) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). COMP 1's compressor (dyn_comp_1.c) with its detector fed through a
 * 3rd-order filter set by Clipping (c = the clip taper: zeros +-0.29 c +- 0.001, poles c + 0.0029, ...; the signal
 * itself is not filtered) and its times by Attack (a: attack e^(-1 / (44100 (0.002 + 0.008 a))), release 50 times
 * longer; the denominator in double). The output x (4 + 40 s) Volume. Sustain, Attack, Volume and Clipping glide
 * 0.0001 per sample; the times and the filter are recomputed while Attack and Clipping glide (not at the last step:
 * kept). Mono (the left input; both outputs the same). Bypassed it passes the signal (the knobs glide on).
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Sustain", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Attack", 0.0f, 100.0f, 1.0f, 60.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Clipping", 0.0f, 100.0f, 1.0f, 10.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos POST1 = {
    0.6179965138435364f, -1.143829345703125f, 0.5258499979972839f, 1.0f, -1.8443238735198975f, 0.844621479511261f,
};
static const struct sos POST2 = {
    1.143829345703125f, 0.36929789185523987f, 0.008779199793934822f, 1.0f, 0.008318199776113033f,
    9.800000043469481e-06f,
};

struct state {
    float sustain, attack_k, volume, clip, gain_db, attack, release;
    float n[4], d[4];                   /* the detector filter: d[0] divides */
    float x[4], y[4];
    struct sos_state p1, p2;
};

static void times_set(struct state *s, float a)
{
    float t = (float)st_fmad((double)a, 0.008, 0.002);
    s->attack = st_expf(-4.982313112122938e-05f / t);
    s->release = st_expf(-9.964626315195346e-07f / t);
}

static void clip_set(struct state *s, float c)
{
    s->n[0] = c * 0.29387858510017395f + 0.001009683939628303f;
    s->n[1] = 0.0010111939627677202f - c * 0.2934390604496002f;
    s->n[2] = -0.001006663660518825f - c * 0.29387858510017395f;
    s->n[3] = c * 0.2934390604496002f + -0.001008173800073564f;
    s->d[0] = c + 0.002938661025837064f;
    s->d[1] = st_fma(c, -2.0002853870391846f, -0.0005840367521159351f);
    s->d[2] = st_fma(c, 1.0515488386154175f, -0.0029355401638895273f);
    s->d[3] = st_fma(c, -0.051248256117105484f, 0.0005871576722711325f);
}

static float clip_run(struct state *s, float x)
{
    float n = st_fma(s->n[3], s->x[3], st_fma(s->n[2], s->x[2], st_fma(s->n[1], s->x[1], s->n[0] * x)));
    float d = st_fma(s->d[3], s->y[3], st_fma(s->d[2], s->y[2], s->d[1] * s->y[1]));
    float y = (n - d) / s->d[0];
    for (int i = 3; i > 1; i--) {
        s->x[i] = s->x[i - 1];
        s->y[i] = s->y[i - 1];
    }
    s->x[1] = s->x[0] = x;
    s->y[1] = s->y[0] = y;
    return y;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->attack_k = k[1] * 0.01f;
    s->sustain = st_taper(st_pow4, k[0]);
    s->volume = k[2] * 0.01f;
    s->clip = st_taper(st_clip, k[3]);
    times_set(s, s->attack_k);
    clip_set(s, s->clip);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float att = k[1] * 0.01f, vol = k[2] * 0.01f, sus = st_taper(st_pow4, k[0]), clip = st_taper(st_clip, k[3]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->sustain = st_glide(s->sustain, sus);
        s->attack_k = st_glide(s->attack_k, att);
        s->volume = st_glide(s->volume, vol);
        s->clip = st_glide(s->clip, clip);
        if (!io->enabled)
            continue;
        float x = p[0];
        if (s->attack_k != att)
            times_set(s, s->attack_k);
        if (s->clip != clip)
            clip_set(s, s->clip);
        float c = clip_run(s, x);
        float l = st_log10f(st_abs(c * st_fma(s->sustain, 97.0f, 3.0f)) + 9.99999991097579e-38f);
        float db = l * 20.0f, knee = st_fma(l, 0.19999998807907104f, -9.899999618530273f);
        float g = (-10.0f > db ? db : knee) - db;
        s->gain_db = st_fma(g <= s->gain_db ? s->attack : s->release, s->gain_db - g, g);
        float y = sos_run(&POST2, &s->p2, sos_run(&POST1, &s->p1, st_exp2f(s->gain_db * 0.16609640419483185f) * x));
        p[0] = p[1] = (st_fma(s->sustain, 40.0f, 4.0f) * s->volume) * y;
    }
}

const struct wvfx_desc dyn_comp_2_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x101),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "COMP 2",
    .category = "Compressor",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
