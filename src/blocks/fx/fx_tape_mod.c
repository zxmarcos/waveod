/*
 * Tape Mod (FX): the original's Tape Mod (QME-200 V1.2.2, library entry 1.7, id 0x01000033; its code calls it the
 * Satisfaction) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A tape saturation, mono (the
 * left input; both outputs the same). Single precision.
 *
 * x through a fixed pre-emphasis section, the amps' 4-points stage (amp.h) with a power clipper 1 - q above zero,
 * q - 1 below (q = (1 + k |v|)^1024, k = -0.0195 s - 0.00098, s = pow3(Saturation)) and the 40-tap FIR (the order
 * 0, 3, 2, 1, 19 .. 4), a fixed de-emphasis section (summed from b1) and a Butterworth low pass by High Cut
 * (h = 1 - pow0_3 at the knob's whole value, t = tan(0.1425 + 0.5699 h), recomputed when h changes); mixed into x by
 * Mix (pow3), x 2 Volume. Saturation, Mix and Volume glide 0.0001 per sample, bypassed too. Bypassed it passes the
 * signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Saturation", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "High Cut", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PRE = {
    -1.8996000289916992f, 2.9419000148773193f, -1.031599998474121f, 1.0f, -1.0988999605178833f, 0.10840000212192535f,
};
static const struct sos POST = {
    -0.5145999789237976f, 0.5764999985694885f, -0.05700000002980232f, 1.0f, -1.5246000289916992f, 0.5300999879837036f,
};
static const uint8_t FIR_ORDER[20] = { 0, 3, 2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

struct state {
    float sat, mix, volume, high;
    struct sos lp;
    struct sos_state pre, post, lps;
    struct stage st;
};

static float high_of(float knob)
{
    return 1.0f - st_pow0_3[(int)knob];
}

/* the Butterworth low pass from t = tan(0.1425 + 0.5699 h) */
static void lp_set(struct state *s, float h)
{
    float t = st_tanf(st_fma(h, 0.5699034333229065f, 0.14247585833072662f));
    float t2 = t * t, a = t2 * 0.7070000171661377f, b = t2 * 1.4140000343322754f;
    float g = 1.0f / ((t + 0.7070000171661377f) + a);
    s->lp = (struct sos){ g * a, g * b, g * a, 1.0f, g * (b + -1.4140000343322754f),
                          g * ((0.7070000171661377f - t) + a) };
}

/* 1 - q above zero, q - 1 below, q = (1 + k |v|)^1024 by ten squarings */
static inline float clip(float v, float k)
{
    float q = st_fma(k, st_abs(v), 1.0f);
    for (int i = 0; i < 10; i++)
        q = q * q;
    return pick_sign(v, 1.0f - q, q + -1.0f);   /* by v's sign bit (at +0 q = 1: both sides 0) */
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->sat = st_taper(st_pow3, k[0]);
    s->volume = k[2] * 0.01f;
    s->mix = st_taper(st_pow3, k[1]);
    s->high = high_of(k[3]);
    lp_set(s, s->high);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float sat = st_taper(st_pow3, k[0]), vol = k[2] * 0.01f, mix = st_taper(st_pow3, k[1]), h = high_of(k[3]);
    float *p = io->io, y[WVFX_MAX_FRAMES], kc[WVFX_MAX_FRAMES], mx[WVFX_MAX_FRAMES], vl[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->sat = st_glide(s->sat, sat);
            s->mix = st_glide(s->mix, mix);
            s->volume = st_glide(s->volume, vol);
        }
        return;
    }
    /* stage by stage over the block (each glide once per sample; High, not glided, before the first sample): the
       Saturation glides, so the clipper changes every sample and all 4 points are clipped (none is the last one's
       again); the same samples as sample by sample */
    if (s->high != h) {
        s->high = h;
        lp_set(s, h);
    }
    for (int i = 0; i < n; i++) {
        s->sat = st_glide(s->sat, sat);
        s->mix = st_glide(s->mix, mix);
        s->volume = st_glide(s->volume, vol);
        kc[i] = st_fma(s->sat, -0.01953125f, -0.0009765625f);
        mx[i] = s->mix;
        vl[i] = s->volume;
        y[i] = p[2 * i];
    }
    sos_block_div(&PRE, &s->pre, y, n);
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->st.pts[s->st.at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->st.prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, *o = buf + 40 + 4 * i;
        o[0] = clip(prev, kc[i]);
        o[1] = clip(st_fma(d, 0.33333334f, prev), kc[i]);
        o[2] = clip(st_fma(d, 0.6666667f, prev), kc[i]);
        o[3] = clip(x, kc[i]);
        prev = x;
    }
    s->st.prev = prev;
    stage_block_fir_ord(&s->st, buf, y, n, FIR_ORDER);
    for (int i = 0; i < n; i++) {
        float u = sos_run_div_b1(&POST, &s->post, y[i]);
        float l = sos_run(&s->lp, &s->lps, u), x = p[2 * i];
        float m = st_fma(l - x, mx[i], x);
        p[2 * i] = p[2 * i + 1] = vl[i] * (m + m);
    }
}

const struct wvfx_desc fx_tape_mod_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x107),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Tape Mod",
    .category = "Special",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
