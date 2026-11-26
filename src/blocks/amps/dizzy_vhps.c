/*
 * Dizzy VH+ S (AMP › Dizzy): the original's Dizzy VH+ S amp (QME-200 V1.2.2, library entry 4.55, id 0x0700006B) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * The path is in dizzy_s.h; this model: the VH+'s gain section, another low pass, clip at +-0.8 (k
 * 0.00830078125), x 7 at the end.
 */
#include "dizzy_s.h"

static const struct wvfx_param params[] = DIZZY_PARAMS;

static const struct dizzy_s_model MODEL = {
    .gain = { .k = 0.4874397, .a1k = -0.9497588, .a1c = -0.4068408, .a0c = 0.4510531 },
    .lowpass = { 0.0449824333190918f, 0.0899648666381836f, 0.0449824333190918f, 1.2931621074676514f,
                 -1.8200702667236328f, 0.7068378925323486f },
    .L = 0.800000011920929f, .k = 0.00830078125f, .out = 7.0f,
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

const struct wvfx_desc dizzy_vhps_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x6B),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dizzy VH+ S",
    .category = "Dizzy",
    .params = params,
    .state_size = sizeof(struct dizzy_s_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};