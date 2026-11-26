/*
 * Mod Reverb (REVERB): the original's Mod Reverb (QME-200 V1.2.2, library entry 10.9, id 0x0C000008) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 2048, 2187, 2401
 * and 3125 samples, Decay = 0.5 + 0.45 pow0_5(Decay), modulation depth 0.5; the lines feed back their reads as they
 * are, the damping (a low and a high shelf by Lo End and Hi End, recomputed when they move) on the output sums, the
 * left one x 0.8.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x73, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Lo End", -50.0f, 50.0f, 1.0f, 0.0f, 3, 0x5d, 0x00, 0, 0, 0 },
    { "Hi End", -50.0f, 50.0f, 1.0f, 0.0f, 4, 0x5d, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model MOD_REVERB = {
    0.44999998807907104f, 0.5f, 0.5f, 0.0f, 0.800000011920929f, 0.0f, 2, 1, 0,
    { 2048.0f, 2187.0f, 2401.0f, 3125.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0449824333190918f, 0.0899648666381836f, 0.0449824333190918f, 1.2931621074676514f, -1.8200702667236328f,
      0.7068378925323486f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct rvb_fdn *f = state;
    (void)env;
    rvb_fdn_init(f, &MOD_REVERB, buffer, k[0], k[1], k[2]);
    f->ld = st_fma(-k[3], 0.01f, 0.5f);
    f->hd = st_fma(-k[4], 0.01f, 0.5f);
    rvb_fdn_mod_damp(f, f->ld, f->hd);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &MOD_REVERB, io, k[0], k[1], rvb_fdn_decay(&MOD_REVERB, k[2]), k[5], st_fma(-k[4], 0.01f, 0.5f),
                    st_fma(-k[3], 0.01f, 0.5f));                                       /* VFMS */
}

const struct wvfx_desc rvb_mod_reverb_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x109),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Mod Reverb",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
