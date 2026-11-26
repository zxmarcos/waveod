/*
 * Plate (REVERB): the original's Plate reverb (QME-200 V1.2.2, library entry 10.5, id 0x0C000003) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2048, 2401, 3125 and
 * 3481 samples, Decay = 0.4 + 0.55 pow0_5(Decay), modulation depth 0.15, Pre Delay fixed at 0.5, the damping a shelf
 * set by High Damp (e = 2^(-0.83 (High Damp + 0.1)); recomputed when the knob moves; the Init folds the x 0.01 into
 * its constant).
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "High Damp", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model PLATE = {
    0.550000011920929f, 0.4000000059604645f, 0.15000000596046448f, 0.5f, 0.8999999761581421f, 0.0f, 1, 0, 0,
    { 2048.0f, 2401.0f, 3125.0f, 3481.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.2782568633556366f, 0.5565137267112732f, 0.2782568633556366f, 1.6338622570037842f, -0.8869725465774536f,
      0.3661378026008606f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct rvb_fdn *f = state;
    (void)env;
    rvb_fdn_init(f, &PLATE, buffer, k[0], 0.0f, k[1]);
    f->hd = k[2] * 0.01f;
    rvb_fdn_hi_damp(f, st_fma(k[2], -0.008304819464683533f, -0.08304820209741592f));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &PLATE, io, k[0], 0.0f, rvb_fdn_decay(&PLATE, k[1]), k[3], k[2] * 0.01f, 0.0f);
}

const struct wvfx_desc rvb_plate_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x105),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Plate",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
