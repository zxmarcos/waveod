/*
 * Spring (REVERB): the original's Spring reverb (QME-200 V1.2.2, library entry 10.6, id 0x0C000004) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h) with the full 4 x 4 Hadamard
 * matrix (+-Decay / 2): lines of 2048, 2187, 3125 and 2401 samples, Decay = 0.9 pow0_5(Decay), modulation depth 0.15,
 * no pre-delay (the line read 2205 - 33 lfo back), the damping a shelf set by Tone (e = 2^(-0.498 (1 - Tone / 100)),
 * recomputed when the knob moves), the left sum unscaled.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model SPRING = {
    0.8999999761581421f, 0.0f, 0.15000000596046448f, -1.0f, 1.0f, 0.0f, 3, 0, 1,
    { 2048.0f, 2187.0f, 3125.0f, 2401.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.024361789226531982f, 0.048723578453063965f, 0.024361789226531982f, 1.2180618047714233f, -1.902552843093872f,
      0.7819381952285767f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct rvb_fdn *f = state;
    (void)env;
    rvb_fdn_init(f, &SPRING, buffer, k[0], 0.0f, k[1]);
    f->hd = st_fma(-k[2], 0.01f, 1.0f);                                             /* VFMS */
    rvb_fdn_tone_damp(f, f->hd);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &SPRING, io, k[0], 0.0f, rvb_fdn_decay(&SPRING, k[1]), k[3], st_fma(-k[2], 0.01f, 1.0f), 0.0f);
}

const struct wvfx_desc rvb_spring_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x106),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Spring",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
