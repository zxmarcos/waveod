/*
 * Sea (REVERB): the original's Sea reverb (QME-200 V1.2.2, library entry 10.8, id 0x0C000007) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2048, 2187, 2401 and 3125
 * samples, Decay = 0.5 + 0.4 pow0_5(Decay), modulation depth 1, Pre Delay fixed at 0.75, the left sum x 0.75.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model SEA = {
    0.4000000059604645f, 0.5f, 1.0f, 0.75f, 0.75f, 0.0f, 0, 0, 0,
    { 2048.0f, 2187.0f, 2401.0f, 3125.0f },
    { -0.6514372825622559f, 1.2469463348388672f, -0.5784034729003906f, 1.0f, -1.7412030696868896f,
      0.7573518753051758f },
    { 0.1215955913066864f, 0.2431911826133728f, 0.1215955913066864f, 1.4622604846954346f,
      -1.5136176347732544f, 0.5377395749092102f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_fdn_init(state, &SEA, buffer, k[0], 0.0f, k[1]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &SEA, io, k[0], 0.0f, rvb_fdn_decay(&SEA, k[1]), k[2], 0.0f, 0.0f);
}

const struct wvfx_desc rvb_sea_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x108),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sea",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
