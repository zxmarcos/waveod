/*
 * Dizzy VH+ (AMP › Dizzy): the original's Dizzy VH+ amp (QME-200 V1.2.2, library entry 4.54, id 0x0700006A) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The path is in dizzy.h; this model:
 *   another gain section and low pass, drive x 0.6; stage 1 +-1 (k 3 / 512 above zero, 7 / 1024 below); stage 2
 *   +-0.8 (k 1 / 1024, 1 / 1280); x 9 at the end.
 */
#include "dizzy.h"

static const struct wvfx_param params[] = DIZZY_PARAMS;

static const struct dizzy_model MODEL = {
    .k = 0.48743969202041626f, .a1k = -0.9497588276863098f, .a1c = -0.40684080123901367f, .a0c = 0.45105311274528503f,
    .drive = 0.6000000238418579f,
    .lowpass = { 0.011375010013580322f, 0.022750020027160645f, 0.011375010013580322f, 1.1499934196472168f,
                 -1.9544999599456787f, 0.8500065803527832f },
    .lp1 = 1.0f, .ln1 = 1.0f, .kp1 = 0.005859375f, .kn1 = 0.0068359375f,
    .lp2 = 0.800000011920929f, .ln2 = 0.800000011920929f, .kp2 = 0.0009765625f, .kn2 = 0.0007812500116415322f,
    .out = 9.0f,
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    dizzy_init(state, k, &MODEL);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    dizzy_process(state, io, &MODEL);
}

const struct wvfx_desc dizzy_vhp_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x6A),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dizzy VH+",
    .category = "Dizzy",
    .params = params,
    .state_size = sizeof(struct dizzy_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};