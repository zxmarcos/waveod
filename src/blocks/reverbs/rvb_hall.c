/*
 * Hall (REVERB): the original's Hall reverb (QME-200 V1.2.2, library entry 10.3, id 0x0C000001) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2187, 2401, 3125 and 4096
 * samples, Decay = 0.4 + 0.55 pow0_5(Decay), modulation depth 0.15.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x73, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model HALL = {
    0.550000011920929f, 0.4000000059604645f, 0.15000000596046448f, 0.0f, 0.8999999761581421f, 0.0f, 0, 0, 0,
    { 2187.0f, 2401.0f, 3125.0f, 4096.0f },
    { -0.9408177733421326f, 1.6165367364883423f, -0.6724931597709656f, 1.0f, -1.7089825868606567f,
      0.7122456431388855f },
    { 0.033915430307388306f, 0.06783086061477661f, 0.033915430307388306f, 1.2560275793075562f,
      -1.8643382787704468f, 0.7439724206924438f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_fdn_init(state, &HALL, buffer, k[0], k[1], k[2]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &HALL, io, k[0], k[1], rvb_fdn_decay(&HALL, k[2]), k[3], 0.0f, 0.0f);
}

const struct wvfx_desc rvb_hall_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x103),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Hall",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
