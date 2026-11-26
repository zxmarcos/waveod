/*
 * NAM captures in the Clone slots (docs/nam.md): an engine block (sdk/wvfx.h) that runs every Clone item whose
 * data is a NAM capture (nam/capture.h) instead of the original's Clone module, which keeps the others (its
 * own captures, empty slots). fw/catalog.c picks the engine per item.
 *
 * The knobs are the Clone entry's (0..100, 50 = neutral), so presets, the edit screen and the PC editor stay as
 * they are:
 *   0 Gain    input drive, (k - 50) x 0.4 dB
 *   1 Volume  output level, (k - 50) x 0.4 dB, 0 = mute
 *   2 Bass    low shelf 100 Hz, (k - 50) x 0.24 dB
 *   3 Middle  peak 700 Hz Q 0.7, (k - 50) x 0.24 dB
 *   4 Treble  high shelf 3 kHz, (k - 50) x 0.24 dB
 * The input is the mono sum of the chain, scaled to NAM's full scale; the output goes to both channels, at the
 * capture's loudness brought to -18 dB.
 *
 * 48 kHz captures (all of NAM's trainings) run at 48 kHz through nam/rs48.h unless the capture says
 * NAM_CAPTURE_NATIVE_RATE; other rates run at the pedal's 44.1 kHz. The network takes whole groups of 4 samples
 * best, so the 69 or 70 samples of a block go through two small FIFOs: q48 keeps the 0..3 samples left for
 * the next block, o48 the outputs not resampled yet. o48 starts with 3 samples of the silent output, so
 * q_n + o_n = 3 always and the down path never runs dry: 3 samples (62 us) more latency, 1.09 ms in all.
 *
 * A tone knob at 50 is 0 dB: its band is not run (the usual case; the three bands are 1 % of the block).
 *
 * Memory: the network's long histories and everything else in the state pool (~104 KB); its derived weights
 * (7.9 KB), read for every frame, in the module code pool (the ITCM: no wait states, and outside the D-cache
 * the histories keep flushing). The short histories (30 KB, all read and written in every block: with the
 * long ones they are twice what the D-cache holds, so most of their lines are fetched again every block) go
 * there too while that leaves CODE_POOL_KEEP (half of the pool) for the modules of the slots after this one and
 * of the next preset, loaded beside this one's tail, and not when this preset is itself loaded beside a tail
 * (what fits there stays what it was); otherwise they stay in the block's state. Code pool full for the
 * weights: WVFX_ERR_NO_MEMORY.
 */
#include "engine/pools.h"
#include "fw/crc32.h"
#include "nam/a2.h"
#include "nam/capture.h"
#include "nam/rs48.h"
#include "platform/sdram.h"
#include "sdk/wvfx.h"

#ifdef __clang__
#pragma clang fp contract(off)
#endif

enum { K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

#define ADC_TO_NAM 0.45659f         /* the chain's full scale is +-2.19, NAM's +-1 */
#define CODE_POOL_KEEP 0x10000u     /* of the 128 KB; a stock module's code is 5 KB on average, 11 KB at most */

struct eq_band {
    float b0, b1, b2, a1, a2, z1, z2;
};

struct nam_block {
    struct a2 net;
    struct rs48 rs;
    float *dw;                      /* the network's derived weights (code pool) */
    int at48;                       /* through the resampler */
    float norm;                     /* the capture's loudness to -18 dB */
    float g_in, g_out;              /* the gains of the last block (ramped over the next) */
    float eq_knob[3];
    struct eq_band eq[3];
    float buf[A2_BLOCK];
    float q48[RS_MAX48 + 4], o48[RS_MAX48 + 6];
    int q_n, o_n;
    float lin[A2_LIN_FLOATS];       /* the short histories when the code pool has no room for them */
};

/* cos and sin of 2 pi f / 44100 for 100 Hz, 700 Hz, 3 kHz */
static const float eq_cos[3] = {0.99989850f, 0.99503078f, 0.91003511f};
static const float eq_sin[3] = {0.01424710f, 0.09956785f, 0.41453118f};

static float knob_db(float k, float scale)
{
    return (k - 50.0f) * scale;
}

static inline float sqrt_f(float x)
{
#if defined(__arm__)
    float r;
    __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    return __builtin_sqrtf(x);
#endif
}

/* RBJ: low shelf (band 0) and high shelf (2) with S = 1, peak with Q = 0.7 (1) */
static void eq_design(struct eq_band *q, int i, float db)
{
    const float A = wvfx_db(db * 0.5f), c = eq_cos[i], s = eq_sin[i];
    float b0, b1, b2, a0, a1, a2;
    if (i == 1) {
        const float al = s / (2.0f * 0.7f);
        b0 = 1.0f + al * A;
        b1 = -2.0f * c;
        b2 = 1.0f - al * A;
        a0 = 1.0f + al / A;
        a1 = -2.0f * c;
        a2 = 1.0f - al / A;
    } else {
        const float t = 2.0f * sqrt_f(A) * s * 0.70710678f;
        const float p = A + 1.0f, m = A - 1.0f, sg = i == 0 ? 1.0f : -1.0f;
        b0 = A * (p - sg * m * c + t);
        b1 = sg * 2.0f * A * (m - sg * p * c);
        b2 = A * (p - sg * m * c - t);
        a0 = p + sg * m * c + t;
        a1 = -sg * 2.0f * (m + sg * p * c);
        a2 = p + sg * m * c - t;
    }
    const float inv = 1.0f / a0;
    q->b0 = b0 * inv;
    q->b1 = b1 * inv;
    q->b2 = b2 * inv;
    q->a1 = a1 * inv;
    q->a2 = a2 * inv;
}

static void eq_update(struct nam_block *s, const float *knobs, int force)
{
    for (int i = 0; i < 3; i++)
        if (force || knobs[K_BASS + i] != s->eq_knob[i]) {
            s->eq_knob[i] = knobs[K_BASS + i];
            eq_design(&s->eq[i], i, knob_db(s->eq_knob[i], 0.24f));
            if (s->eq_knob[i] == 50.0f)             /* not run: it starts again from rest */
                s->eq[i].z1 = s->eq[i].z2 = 0.0f;
        }
}

static float in_gain(const float *knobs)
{
    return wvfx_db(knob_db(knobs[K_GAIN], 0.4f)) * ADC_TO_NAM;
}

static float out_gain(const struct nam_block *s, const float *knobs)
{
    return knobs[K_VOLUME] < 0.5f ? 0.0f : wvfx_db(knob_db(knobs[K_VOLUME], 0.4f)) * s->norm;
}

SDRAM_FUNC static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct nam_block *s = state;
    (void)buffer;
    const struct nam_capture *h = env->data;
    if (!nam_capture_is(env->data, env->data_size))
        return -1;
    const uint8_t *w = (const uint8_t *)env->data + NAM_CAPTURE_HEADER;
    if (crc32(0, w, A2_NUM_WEIGHTS * 4) != h->crc)
        return -1;
    uint32_t at = pool_alloc(POOL_CODE, A2_DW_FLOATS * 4);
    s->dw = at ? (float *)pool_ptr(at, A2_DW_FLOATS * 4) : 0;
    if (!s->dw)
        return WVFX_ERR_NO_MEMORY;
    float *lin = s->lin;
    if (!pool_shared(POOL_CODE) && pool_room(POOL_CODE) >= A2_LIN_FLOATS * 4 + CODE_POOL_KEEP) {
        at = pool_alloc(POOL_CODE, A2_LIN_FLOATS * 4);
        float *fast = at ? (float *)pool_ptr(at, A2_LIN_FLOATS * 4) : 0;
        if (fast)
            lin = fast;
    }
    a2_init(&s->net, s->dw, lin, (const float *)w); /* copied: the slot may be written while the preset plays */
    float loud = h->loudness;
    if (!(loud > -60.0f && loud < 20.0f))
        loud = -18.0f;
    s->norm = wvfx_db(-18.0f - loud);
    s->at48 = nam_capture_resampled(h) && env->max_frames == RS_FRAMES;
    if (s->at48) {
        rs48_init(&s->rs);
        for (int i = 0; i < 4; i++)
            s->q48[i] = 0.0f;
        a2_process(&s->net, s->q48, s->o48, 4);     /* the steady state of silence stays as it is */
        s->o48[1] = s->o48[2] = s->o48[0];
        s->o_n = 3;
        s->q_n = 0;
    }
    for (int i = 0; i < 3; i++)
        s->eq[i].z1 = s->eq[i].z2 = 0.0f;
    eq_update(s, knobs, 1);
    s->g_in = in_gain(knobs);
    s->g_out = out_gain(s, knobs);
    return 0;
}

/* the 48 kHz path over one block of s->buf */
static void run48(struct nam_block *s)
{
    const int c = rs48_up(&s->rs, s->buf, s->q48 + s->q_n);
    const int p = s->q_n + c, m = p & ~3;
    a2_process(&s->net, s->q48, s->o48 + s->o_n, m);
    s->o_n += m;
    rs48_down(&s->rs, s->o48, c, s->buf);
    s->o_n -= c;
    for (int i = 0; i < s->o_n; i++)
        s->o48[i] = s->o48[c + i];
    s->q_n = p - m;
    for (int i = 0; i < s->q_n; i++)
        s->q48[i] = s->q48[m + i];
}

static void process(void *state, const struct wvfx_io *io)
{
    struct nam_block *s = state;
    const int n = io->frames;
    if (!io->enabled || n > A2_BLOCK || (s->at48 && n != RS_FRAMES))
        return;
    float *x = io->io;
    eq_update(s, io->knobs, 0);
    const float ti = in_gain(io->knobs), to = out_gain(s, io->knobs);
    const float inv = 1.0f / (float)n;
    const float di = (ti - s->g_in) * inv, dout = (to - s->g_out) * inv;
    float gi = s->g_in, go = s->g_out;
    for (int f = 0; f < n; f++) {
        gi += di;
        s->buf[f] = (x[2 * f] + x[2 * f + 1]) * 0.5f * gi;
    }
    if (s->at48)
        run48(s);
    else
        a2_process(&s->net, s->buf, s->buf, n);
    for (int i = 0; i < 3; i++) {                   /* transposed direct form II */
        struct eq_band *q = &s->eq[i];
        if (s->eq_knob[i] == 50.0f)
            continue;
        float z1 = q->z1, z2 = q->z2;
        for (int f = 0; f < n; f++) {
            const float v = s->buf[f], y = q->b0 * v + z1;
            z1 = q->b1 * v - q->a1 * y + z2;
            z2 = q->b2 * v - q->a2 * y;
            s->buf[f] = y;
        }
        q->z1 = z1;
        q->z2 = z2;
    }
    for (int f = 0; f < n; f++) {
        go += dout;
        const float y = s->buf[f] * go;
        x[2 * f] = y;
        x[2 * f + 1] = y;
    }
    s->g_in = ti;
    s->g_out = to;
}

const struct wvfx_desc nam_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_CLONE, 0),
    .menu = WVFX_CLONE,
    .nparams = 0,
    .flags = WVFX_ENGINE | WVFX_INHERIT,
    .name = "NAM A2-Lite",
    .category = "",
    .params = 0,
    .state_size = sizeof(struct nam_block),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
