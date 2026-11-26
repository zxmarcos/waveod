/*
 * Dizzy VH (AMP › Dizzy): the original's Dizzy VH amp (QME-200 V1.2.2, library entry 4.52, id 0x07000065) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The path is in dizzy.h; this model:
 *   drive x 0.4; stage 1 +0.6 / -0.8 (k 1 / 256 above zero, 1 / 204.8 below); stage 2 +1 / -0.8 (k 3 / 1024,
 *   1 / 512); x 4.8 at the end.
 */
#include "dizzy.h"

static const struct wvfx_param params[] = DIZZY_PARAMS;

static const struct dizzy_model MODEL = {
    .k = 0.9222158789634705f, .a1k = -0.9936630725860596f, .a1c = -0.7806888818740845f, .a0c = 0.8655279278755188f,
    .drive = 0.4000000059604645f,
    .lowpass = { 0.005066275596618652f, 0.010132551193237305f, 0.005066275596618652f, 1.100420355796814f,
                 -1.9797348976135254f, 0.8995797038078308f },
    .lp1 = 0.6000000238418579f, .ln1 = 0.800000011920929f, .kp1 = 0.00390625f, .kn1 = 0.0048828125f,
    .lp2 = 1.0f, .ln2 = 0.800000011920929f, .kp2 = 0.0029296875f, .kn2 = 0.001953125f,
    .out = 4.800000190734863f,
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

const struct wvfx_desc dizzy_vh_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x65),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dizzy VH",
    .category = "Dizzy",
    .params = params,
    .state_size = sizeof(struct dizzy_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};