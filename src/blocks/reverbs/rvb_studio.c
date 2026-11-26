/*
 * Studio (REVERB): the original's Studio reverb (QME-200 V1.2.2, library entry 10.0, id 0x0C00000B) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The Studio engine (rvb_studio.h): taps 1493, 1094, 567, 5;
 * lines 3630, 2964, 2566, 2295; allpasses 276, 1045 / 252, 968; the low pass fed pre + 0.5 (late 0 + 2 - late 1 + 3)
 * + 0.3 (early 0 + 2 - early 1 + 3).
 */
#include "rvb_studio.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 25.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 300.0f, 1.0f, 0.0f, 1, 0x88, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Low Damp", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Hi Damp", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Mod", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_st_model STUDIO = {
    { 1493, 1094, 567, 5 }, { 3630, 2964, 2566, 2295 }, { 276, 1045, 252, 968 },
    { -0.28495171666145325f, 0.6411413550376892f }, { -0.5699034333229065f, 0.7836171984672546f },
    0, 0.5f, 0.30000001192092896f,
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_st_init(state, &STUDIO, buffer, k);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    rvb_st_process(state, &STUDIO, io, io->knobs);
}

const struct wvfx_desc rvb_studio_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x100),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Studio",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_st),
    .buffer_size = RVB_ST_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
