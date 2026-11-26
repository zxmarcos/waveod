/*
 * Sky (REVERB): the original's Sky reverb (QME-200 V1.2.2, library entry 10.7, id 0x0C000006) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2048, 2187, 2401 and 3125
 * samples, Decay = 0.5 + 0.45 pow0_5(Decay), modulation depth 0.5, Pre Delay fixed at 0.75, the left sum x 0.73.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model SKY = {
    0.44999998807907104f, 0.5f, 0.5f, 0.75f, 0.7300000190734863f, 0.0f, 0, 0, 0,
    { 2048.0f, 2187.0f, 2401.0f, 3125.0f },
    { -0.9014608263969421f, 1.7526023387908936f, -0.831852376461029f, 1.0f, -1.7133618593215942f,
      0.732650876045227f },
    { 0.29111504554748535f, 0.5822300910949707f, 0.29111504554748535f, 1.6425409317016602f,
      -0.8355397582054138f, 0.35745906829833984f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_fdn_init(state, &SKY, buffer, k[0], 0.0f, k[1]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &SKY, io, k[0], 0.0f, rvb_fdn_decay(&SKY, k[1]), k[2], 0.0f, 0.0f);
}

const struct wvfx_desc rvb_sky_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x107),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sky",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
