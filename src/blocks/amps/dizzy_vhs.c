/*
 * Dizzy VH S (AMP › Dizzy): the original's Dizzy VH S amp (QME-200 V1.2.2, library entry 4.53, id 0x07000066) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. The
 * path is in dizzy_s.h; this model: clip at +-1 (k 1 / 256),
 * x 5.267808 at the end.
 */
#include "dizzy_s.h"

static const struct wvfx_param params[] = DIZZY_PARAMS;

static const struct dizzy_s_model MODEL = {
    .gain = { .k = 0.9222159, .a1k = -0.9936631, .a1c = -0.7806889, .a0c = 0.8655279 },
    .lowpass = { 0.03138384222984314f, 0.06276768445968628f, 0.03138384222984314f, 1.2466093301773071f,
                 -1.8744646310806274f, 0.7533906698226929f },
    .L = 1.0f, .k = 0.00390625f, .out = 5.267807960510254f,
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    dizzy_s_init(state, k, &MODEL);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    dizzy_s_process(state, io, &MODEL);
}

const struct wvfx_desc dizzy_vhs_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x66),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dizzy VH S",
    .category = "Dizzy",
    .params = params,
    .state_size = sizeof(struct dizzy_s_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};