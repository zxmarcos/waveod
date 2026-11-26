/*
 * Church (REVERB): the original's Church reverb (QME-200 V1.2.2, library entry 10.4, id 0x0C000002) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2187, 2401, 3125 and 4096
 * samples, Decay = 0.4 + 0.55 pow0_5(Decay), modulation depth 0.15: the Hall's, with its own damping and low pass.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x73, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model CHURCH = {
    0.550000011920929f, 0.4000000059604645f, 0.15000000596046448f, 0.0f, 0.8999999761581421f, 0.0f, 0, 0, 0,
    { 2187.0f, 2401.0f, 3125.0f, 4096.0f },
    { -0.869968831539154f, 1.5691450834274292f, -0.6752304434776306f, 1.0f, -1.6376047134399414f,
      0.6612764596939087f },
    { 0.02016240358352661f, 0.04032480716705322f, 0.02016240358352661f, 1.198805570602417f,
      -1.9193503856658936f, 0.8011943697929382f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_fdn_init(state, &CHURCH, buffer, k[0], k[1], k[2]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &CHURCH, io, k[0], k[1], rvb_fdn_decay(&CHURCH, k[2]), k[3], 0.0f, 0.0f);
}

const struct wvfx_desc rvb_church_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x104),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Church",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
