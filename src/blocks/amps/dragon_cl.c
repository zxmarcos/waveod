/*
 * Dragon CL (AMP › Dragon): the original's Dragon CL amp (QME-200 V1.2.2, library entry 4.21, id 0x0700001F) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The shared path is in dragon.h; this model:
 *   pre-filters, a high pass (divided by its a0), a 2nd-order gain section from Gain (pow3 x 0.35), the stage +3 / -2.8
 *   (k 3 / 1024), the tone stack, the power amp, x 80.
 * Init computes the gain section from the taper before x 0.35 (its factors x 0.35 instead), so its coefficients
 * differ from Process's by a rounding until Gain moves.
 */
#include "dragon.h"

static const struct wvfx_param params[] = DRAGON_PARAMS(35.0f);

static const struct sos HIGHPASS = { 1.0f, -1.999899983406067f, 1.0f, 1.0190000534057617f, -1.999899983406067f,
                                     0.9810000061988831f };

/* the gain section's factors of p (= g in Process, the taper in Init): n0 n1 n2 d0 d1 d2 */
static const float GAIN_K[6] = { -1.1889640092849731f, -0.1791895031929016f, 0.6754682064056396f,
                                 -0.9019677042961121f, 0.15144099295139313f, 0.7505267262458801f };
static const float GAIN_K_INIT[6] = { -0.4161373972892761f, -0.06271632760763168f, 0.2364138662815094f,
                                      -0.31568869948387146f, 0.053004346787929535f, 0.26268434524536133f };

/* b0 = g^2 + k0 p, b1 = c1 g^2 + k1 p, b2 = c2 g^2 + k2 p, a0 = k3 p - 0.3055 + g^2, a1 = k4 p - 0.3647 + c1 g^2,
   a2 = k5 p - 0.0906 + c2 g^2 (c1 = -0.1672, c2 = -0.8328) */
static void gain_set(struct sos *c, float g, float p, const float *k)
{
    float g2 = g * g, g2c1 = g2 * -0.16715340316295624f, g2c2 = g2 * -0.8328465819358826f;
    c->b0 = st_fma(p, k[0], g2);
    c->b1 = st_fma(p, k[1], g2c1);
    c->b2 = st_fma(p, k[2], g2c2);
    c->a0 = st_fma(p, k[3], -0.3055213987827301f) + g2;
    c->a1 = st_fma(p, k[4], -0.36471059918403625f) + g2c1;
    c->a2 = st_fma(p, k[5], -0.09061410278081894f) + g2c2;
}

static float gain_knob(const float *k)
{
    return st_taper(st_pow3, k[DR_GAIN]) * 0.35f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct dragon_state *s = state;
    (void)buffer; (void)env;
    dragon_init(s, k, gain_knob(k), 0);
    gain_set(&s->gain_c, s->at.gain, st_taper(st_pow3, k[DR_GAIN]), GAIN_K_INIT);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct dragon_state *s = state;
    struct dragon_knobs k;
    dragon_knobs(&k, io->knobs, gain_knob(io->knobs));
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            dragon_glide(s, &k);
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&DRAGON_CL_PF[0], &s->pf[0], x, n);
    sos_block(&DRAGON_CL_PF[1], &s->pf[1], x, n);
    sos_block_div(&HIGHPASS, &s->hp, x, n);
    for (int i = 0; i < n; i++) {
        s->at.gain = st_glide(s->at.gain, k.gain);
        if (s->at.gain != k.gain)
            gain_set(&s->gain_c, s->at.gain, s->at.gain, GAIN_K);
        x[i] = sos_run_div(&s->gain_c, &s->gs, x[i]);
    }
    stage_pow2_block(&s->stage, x, n, 3.0f, 2.799999952316284f, 0.0029296875f, 0.0029296875f);
    for (int i = 0; i < n; i++) {
        if (dragon_glide_post(s, &k))
            dragon_tone_set(s, 0);
        x[i] = tone3_run_b1b0(&s->tone, x[i]);
        vol[i] = s->at.volume;
    }
    for (int j = 0; j < 5; j++)
        sos_block(&DRAGON_CL_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 80.0f * x[i];
}

const struct wvfx_desc dragon_cl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x1F),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dragon CL",
    .category = "Dragon",
    .params = params,
    .state_size = sizeof(struct dragon_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
