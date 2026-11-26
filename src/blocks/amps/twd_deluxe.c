/*
 * TWD Deluxe (AMP › TWD): the original's TWD Deluxe amp (QME-200 V1.2.2, library entry 4.0, id 0x07000001) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (amp.h has the pieces):
 *   pre-filter    x 1.1, two fixed 2nd-order sections
 *   tone          a 2nd-order section from Tone (pow2_5; divided by its a0)
 *   gain          x Gain (pow2_5), a fixed 2nd-order pre-amp section, x 0.5, a fixed boost section
 *   stage         the envelope-driven clip a (1 - e^(-k |x|)) x M on 4 points per sample, the FIR: k = 5 + 6 env
 *                 (env: |x| through the Bog SV's low pass), a = 0.25 - 0.1 Gain, M = max(1 / (1 + 3 k), 0.04); the
 *                 FIR's pairs summed in the Calif Star CL's order (AMP_FIR_ORDER2)
 *   output        x Volume x 220
 * All three knobs glide 0.0001 per sample; the tone coefficients stop one glide step short of Tone. The stage scales
 * a by M before 1 - e on the earlier three points, after it on the current one (the original's two forms).
 */
#include "amp.h"

enum { K_GAIN, K_TONE, K_VOLUME };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }
#define KNOBD(name, k, d) { name, 0.0f, 100.0f, 1.0f, d, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = { KNOBD("Gain", K_GAIN, 30.0f), KNOB("Tone", K_TONE), KNOB("Volume", K_VOLUME) };

static const struct sos PF[2] = {
    { 7.446924686431885f, -7.5923614501953125f, 0.15706419944763184f, 1.0f, -1.0799288749694824f,
      0.08212459832429886f },
    { 7.5923614501953125f, 4.413636684417725f, 0.0030392000917345285f, 1.0f, 0.5455666780471802f,
      0.0002494000073056668f },
};
static const struct sos PREAMP = { 0.933403491973877f, -1.7020858526229858f, 0.7691048979759216f, 1.0f,
                                   -1.8207968473434448f, 0.8228176832199097f };
static const struct sos BOOST = { -1.5997896194458008f, 4.038732528686523f, -2.0359811782836914f, 1.0f,
                                  -0.6575729846954346f, 0.0605347566306591f };

struct state {
    float gain, tone, volume;   /* glided */
    struct sos tone_c;
    struct sos_state pf[2], ts, pre, boost;
    struct env_state env;
    struct stage st;
};

/* the tone section from t: (t + 0.00242, -1.980 t + 0.0000542, 0.980 t - 0.00236) / (0.505 t + 0.509,
   -0.990 t - 0.990, 0.485 t + 0.481), plus -0.495 t^2 or 0.990 t^2 (the middle terms) */
static void tone_set(struct sos *c, float t)
{
    float tt = t * t, A = tt * -0.49498501420021057f, B = tt * 0.9899699091911316f;
    c->b0 = (t + 0.002415193011984229f) + A;
    c->b1 = st_fma(t, -1.9799400568008423f, 5.415231134975329e-05f) + B;
    c->b2 = st_fma(t, 0.9799398183822632f, -0.0023610410280525684f) + A;
    c->a0 = st_fma(t, 0.5050150156021118f, 0.5086243152618408f) + A;
    c->a1 = st_fma(t, -0.9899699091911316f, -0.9899157881736755f) + B;
    c->a2 = st_fma(t, 0.4849548935890198f, 0.481399804353714f) + A;
}

/* the stage on x after the envelope; g the glided Gain */
/*
 * The stage over a block in place, the sample's Gain in g[i]: per sample the envelope of x sets k (5 + 6 env), M
 * (max(1 / (1 + 3 k), 0.04)) and a (0.25 - 0.1 g); the 3 older points (a M) - e (a M), the current one (a - e a) M,
 * e = e^(-k |q|), the sign of a by q's (exp_negabs, sign_of: without branches); then the FIR four samples at a time.
 */
static void stage_block(struct state *s, float *y, const float *g, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->st.pts[s->st.at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->st.prev;
    for (int i = 0; i < n; i++) {
        float x = y[i];
        float env = env_run(&s->env, x, 9.920032653099042e-07f, -1.9980080127716064f, 0.9980090260505676f);
        float k = st_fma(env, 6.0f, 5.0f), a = st_fma(g[i], -0.10000000149011612f, 0.25f);
        float m = 1.0f / st_fma(k, 3.0f, 1.0f);
        m = m > 0.03999999910593033f ? m : 0.03999999910593033f;   /* VMAXNM */
        float d = x - prev, q[4] = { x, st_fma(d, 0.6666667f, prev), st_fma(d, 0.33333334f, prev), prev }, v[4];
        prev = x;
#pragma clang loop unroll(full)
        for (int j = 1; j < 4; j++) {
            float am = sign_of(a, q[j]) * m;
            v[j] = st_fms(exp_negabs(q[j], k), am, am);
        }
        float ac = sign_of(a, q[0]);
        v[0] = st_fms(exp_negabs(q[0], k), ac, ac) * m;
        float *o = buf + 40 + 4 * i;
        o[0] = v[3];
        o[1] = v[2];
        o[2] = v[1];
        o[3] = v[0];
    }
    s->st.prev = prev;
    stage_block_fir_ord(&s->st, buf, y, n, AMP_FIR_ORDER2);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2_5, k[K_GAIN]);
    s->tone = st_taper(st_pow2_5, k[K_TONE]);
    s->volume = k[K_VOLUME] * 0.01f;
    tone_set(&s->tone_c, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow2_5, io->knobs[K_GAIN]), tone = st_taper(st_pow2_5, io->knobs[K_TONE]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES], g[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->tone = st_glide(s->tone, tone);
            s->volume = st_glide(s->volume, volume);
        }
        return;
    }
    /* stage by stage over the block (the glides once per sample, the coefficients following them): the same
       samples as sample by sample */
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    sos_block(&PF[0], &s->pf[0], x, n);
    sos_block(&PF[1], &s->pf[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->tone = st_glide(s->tone, tone);
        s->volume = st_glide(s->volume, volume);
        if (s->tone != tone)
            tone_set(&s->tone_c, s->tone);
        x[i] = sos_run_div(&s->tone_c, &s->ts, x[i]) * s->gain;
        g[i] = s->gain;
        vol[i] = s->volume;
    }
    sos_block(&PREAMP, &s->pre, x, n);
    for (int i = 0; i < n; i++)
        x[i] = x[i] * 0.5f;
    sos_block(&BOOST, &s->boost, x, n);
    stage_block(s, x, g, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 220.0f * x[i];
}

const struct wvfx_desc twd_deluxe_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x01),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "TWD Deluxe",
    .category = "TWD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
