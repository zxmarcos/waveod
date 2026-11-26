/*
 * Superb CL (AMP › Superb): the original's Superb CL amp (QME-200 V1.2.2, library entry 4.11, id 0x07000015) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. The path is in superb.h; this model: Gain through the Vol taper, x 40 in,
 * x 60.04 out.
 */
#include "superb.h"

static const struct wvfx_param params[] = SUPERB_PARAMS(35.0f);

static float gain_taper(float knob) { return st_taper(st_vol, knob); }

static const struct superb_model MODEL = { gain_taper, 40.0f, 60.04475784301758f };

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    superb_init(state, &MODEL, k);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    superb_process(state, &MODEL, io);
}

const struct wvfx_desc superb_cl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x15),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Superb CL",
    .category = "Superb",
    .params = params,
    .state_size = sizeof(struct superb_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
