/*
 * U-Vibe (MOD › Phaser): the original's U-Vibe (QME-200 V1.2.2, library entry 8.16, id 0x04000020) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a 4-stage vibe on each output, their sweeps
 * in opposite phase. Single precision except the LFO's scaling.
 *
 * The LFO v = +-0.7 cos(2 pi r n) (the product in double) goes through a rational curve
 * f(v) = sign(v) (0.1297 v^2 + (0.8374 v^2 + 0.2423) |v|) / ((|v| + 0.7854) v^2 + 0.34 - 0.2664 |v|) (the
 * photocell's response); u = (f + 0.6) d sets four 1st-order stages b0 = u + B0, b1 = B1 - u, y1 weight u - C,
 * a0 = u + A0. Mode 0 (Chorus) mixes the input in and feeds 0.35 x the last stage-4 output back; Mode 1 (Vibrato)
 * neither: the mix c (1 at Mode 0, else 0) glides. Out: (stage 4 + c x) Volume. Depth (d), Volume, c and the rate
 * glide 0.00022676 of the way to the knobs each sample, the rate from 0; the count n starts over at one period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x14, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Mode", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x70, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x21, 0, 0, 0 },
};

/* each stage's B0, B1, C, A0 */
static const float STAGE[4][4] = {
    { 0.00843295082449913f, -0.011089049279689789f, -0.008781654760241508f, 0.011820345185697079f },
    { 0.009670451283454895f, -0.009851548820734024f, -0.010197408497333527f, 0.01040459144860506f },
    { -0.03262355178594589f, -0.05214555561542511f, 0.03818873316049576f, 0.05879073590040207f },
    { 0.0055225444957613945f, -0.013999455608427525f, -0.00545202661305666f, 0.015149973332881927f },
};

struct state {
    float depth, volume, mix, rate;
    int n;
    struct ap1_state l[4], r[4];
};

static float photocell(float v)
{
    float v2 = v * v, a = st_abs(v), p = st_fma(v2, 0.8374000191688538f, 0.2423000037670135f);
    float q = v2 * 0.12970000505447388f;
    /* VFNMA below zero: -(p a + q) exactly, picked by v's bits (amp.h, clip_sign; at -0 the original's +0 stays) */
    float num = clip_sign(st_fma(p, a, q), v);
    float den = st_fma(a + 0.7853999733924866f, v2, st_fma(a, -0.266400009393692f, 0.3400000035762787f));
    return num / den;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.01f;
    s->volume = k[2] * 0.01f;
    s->mix = k[3] == 0.0f ? 1.0f : 0.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float mix = k[3] == 0.0f ? 1.0f : 0.0f, rate = mod_rate(k[1], k[4], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dv = (k[2] * 0.01f - s->volume) * 0.0002267600066261366f;
    float dm = (mix - s->mix) * 0.0002267600066261366f;
    /* the glides and the LFO for the block first, then both channels' stages side by side, their states in
       registers (the same operations on the same values as sample by sample) */
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float *p = io->io, U[2][WVFX_MAX_FRAMES], FB[WVFX_MAX_FRAMES], V[WVFX_MAX_FRAMES], D[WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++) {
        s->rate += dr;
        s->depth += dd;
        s->volume += dv;
        s->mix += dm;
        float period = 1.0f / s->rate;
        float sn = st_cosf((s->rate * 6.2831854820251465f) * (float)s->n);
        U[0][i] = (photocell((float)((double)sn * 0.7)) + 0.6000000238418579f) * s->depth;
        U[1][i] = (photocell((float)((double)-sn * 0.7)) + 0.6000000238418579f) * s->depth;
        FB[i] = s->mix * 0.3499999940395355f;
        V[i] = s->volume;
        D[i] = s->mix;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
    }
    float lx[4], ly[4], rx[4], ry[4];
    for (int k = 0; k < 4; k++) {
        lx[k] = s->l[k].x1;
        ly[k] = s->l[k].y1;
        rx[k] = s->r[k].x1;
        ry[k] = s->r[k].y1;
    }
    for (int i = 0; i < n; i++) {
        float x = p[2 * i], ul = U[0][i], ur = U[1][i];
        float a = st_fma(FB[i], ly[3], x), b = st_fma(FB[i], ry[3], x);
#pragma clang loop unroll(full)
        for (int k = 0; k < 4; k++) {
            float ya = st_fma(ly[k], ul - STAGE[k][2], st_fma(lx[k], STAGE[k][1] - ul, a * (ul + STAGE[k][0])))
                       / (ul + STAGE[k][3]);
            float yb = st_fma(ry[k], ur - STAGE[k][2], st_fma(rx[k], STAGE[k][1] - ur, b * (ur + STAGE[k][0])))
                       / (ur + STAGE[k][3]);
            lx[k] = a;
            ly[k] = ya;
            rx[k] = b;
            ry[k] = yb;
            a = ya;
            b = yb;
        }
        float dry = D[i] * x;
        p[2 * i] = (a + dry) * V[i];
        p[2 * i + 1] = (b + dry) * V[i];
    }
    for (int k = 0; k < 4; k++) {
        s->l[k] = (struct ap1_state){ lx[k], ly[k] };
        s->r[k] = (struct ap1_state){ rx[k], ry[k] };
    }
}

const struct wvfx_desc mod_u_vibe_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x110),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "U-Vibe",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
