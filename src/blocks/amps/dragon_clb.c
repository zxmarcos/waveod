/*
 * Dragon CL B (AMP › Dragon): the original's Dragon CL B amp (QME-200 V1.2.2, library entry 4.22, id 0x0700007B) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The shared path is in dragon.h; this model (the CL's bright channel):
 *   the CL's pre-filters, a 2nd-order gain section from Gain (pow3) computed in double, the stage +3 / -2.8
 *   (k 3 / 1024), the tone stack in double, the CL's power amp, x 48.
 */
#include "dragon.h"

static const struct wvfx_param params[] = DRAGON_PARAMS(20.0f);

/* b0 = g^2 - 1.189 g, b1 = 2 g - 2 g^2, b2 = g^2 - 0.811 g, a0 = -0.858 g - 0.358 + g^2, a1 = 1.812 g + 0.170 -
   2 g^2, a2 = -0.954 g + 0.152 + g^2, in double from g and g^2 in single precision */
static void gain_set(struct sos *c, float gain)
{
    double g = gain, g2 = gain * gain;
    c->b0 = (float)st_fmad(g, -1.188964, g2);
    c->b1 = (float)st_fmad(g, 2.0, g2 * -2.0);
    c->b2 = (float)st_fmad(g, -0.8110355, g2);
    c->a0 = (float)(st_fmad(g, -0.8577538, -0.3580902) + g2);
    c->a1 = (float)(st_fmad(g, 1.812, 0.1697663) + g2 * -2.0);
    c->a2 = (float)(st_fmad(g, -0.9542462, 0.1518566) + g2);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct dragon_state *s = state;
    (void)buffer; (void)env;
    dragon_init(s, k, st_taper(st_pow3, k[DR_GAIN]), 1);
    gain_set(&s->gain_c, s->at.gain);
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
    dragon_knobs(&k, io->knobs, st_taper(st_pow3, io->knobs[DR_GAIN]));
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
    for (int i = 0; i < n; i++) {
        s->at.gain = st_glide(s->at.gain, k.gain);
        if (s->at.gain != k.gain)
            gain_set(&s->gain_c, s->at.gain);
        x[i] = sos_run_div(&s->gain_c, &s->gs, x[i]);
    }
    stage_pow2_block(&s->stage, x, n, 3.0f, 2.799999952316284f, 0.0029296875f, 0.0029296875f);
    for (int i = 0; i < n; i++) {
        if (dragon_glide_post(s, &k))
            dragon_tone_set(s, 1);
        x[i] = tone3_run_b1b0(&s->tone, x[i]);
        vol[i] = s->at.volume;
    }
    for (int j = 0; j < 5; j++)
        sos_block(&DRAGON_CL_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 48.0f * x[i];
}

const struct wvfx_desc dragon_clb_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x7B),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dragon CL B",
    .category = "Dragon",
    .params = params,
    .state_size = sizeof(struct dragon_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
