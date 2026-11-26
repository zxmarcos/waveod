/*
 * Dragon OD (AMP › Dragon): the original's Dragon OD amp (QME-200 V1.2.2, library entry 4.23, id 0x0700007C) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The shared path is in dragon.h; this model:
 *   its own pre-filters, x 5 Gain (pow2), a fixed 3rd-order section and a low pass (both divided by their a0), the
 *   stage +-1 (k 1 / 256), the tone stack, its own power amp, x 12.853.
 */
#include "dragon.h"

static const struct wvfx_param params[] = DRAGON_PARAMS(30.0f);

static const struct sos PF[2] = {
    { 6.542626857757568f, -11.839554786682129f, 5.299405574798584f, 1.0f, -1.8083058595657349f,
      0.8094068765640259f },
    { 11.839554786682129f, 4.231464862823486f, 8.000000093488779e-07f, 1.0f, 0.31390708684921265f,
      1.0000000116860974e-07f },
};

static const struct tone3 GAIN3 = {
    .b = { -0.1931689977645874f, 0.5635647773742676f, -0.547892689704895f, 0.177496999502182f },
    .a = { -0.2197078913450241f, 0.5891178846359253f, -0.5213549137115479f, 0.15194258093833923f },
};

static const struct sos LOWPASS = { 0.011375010013580322f, 0.022750020027160645f, 0.011375010013580322f,
                                    1.1499934196472168f, -1.9544999599456787f, 0.8500065803527832f };

static const struct sos POWER[5] = {
    { 0.7761319279670715f, -1.2472939491271973f, 0.4762186110019684f, 1.0f, -1.82845938205719f,
      0.8329734802246094f },
    { 0.7035660743713379f, -1.2472939491271973f, 0.5656815767288208f, 1.0f, -1.6729216575622559f,
      0.7693163752555847f },
    { 0.8012545704841614f, -1.2472939491271973f, 0.5258393287658691f, 1.0f, -0.49122390151023865f,
      0.0880730003118515f },
    { 1.2472939491271973f, -0.583103597164154f, 0.15490590035915375f, 1.0f, -1.3638055324554443f,
      0.42226290702819824f },
    { 0.9398494958877563f, 1.2472939491271973f, 0.43632540106773376f, 1.0f, 1.4172916412353516f,
      0.5307977199554443f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct dragon_state *s = state;
    (void)buffer; (void)env;
    dragon_init(s, k, st_taper(st_pow2, k[DR_GAIN]), 0);
    s->gain3 = GAIN3;
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
    dragon_knobs(&k, io->knobs, st_taper(st_pow2, io->knobs[DR_GAIN]));
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            dragon_glide(s, &k);
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PF[0], &s->pf[0], x, n);
    sos_block(&PF[1], &s->pf[1], x, n);
    for (int i = 0; i < n; i++) {
        s->at.gain = st_glide(s->at.gain, k.gain);
        x[i] = tone3_run_std(&s->gain3, s->at.gain * 5.0f * x[i]);
    }
    sos_block_div(&LOWPASS, &s->lowpass, x, n);
    stage_pow_block(&s->stage, x, n, 1.0f, 0.00390625f);
    for (int i = 0; i < n; i++) {
        if (dragon_glide_post(s, &k))
            dragon_tone_set(s, 0);
        x[i] = tone3_run_b1b0(&s->tone, x[i]);
        vol[i] = s->at.volume;
    }
    for (int j = 0; j < 5; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 12.85321044921875f * x[i];
}

const struct wvfx_desc dragon_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x7C),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dragon OD",
    .category = "Dragon",
    .params = params,
    .state_size = sizeof(struct dragon_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
