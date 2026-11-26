/*
 * Club (REVERB): the original's Club reverb (QME-200 V1.2.2, library entry 10.1, id 0x0C00000C) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The Studio engine (rvb_studio.h): taps 1971, 1444, 748, 5;
 * lines 4797, 3916, 3392, 3033; allpasses 373, 1546 / 295, 1292; the low pass fed 0.5 (pre + late 0 + 2 - late 1 + 3)
 * + 0.1 (early 0 + 2 - early 1 + 3); Hi Damp's filters from 0.570 - 0.214 Hi Damp and 0.641 - 0.427 Hi Damp.
 */
#include "rvb_studio.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 25.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 300.0f, 1.0f, 0.0f, 1, 0x88, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 70.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Low Damp", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Hi Damp", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Mod", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_st_model CLUB = {
    { 1971, 1444, 748, 5 }, { 4797, 3916, 3392, 3033 }, { 373, 1546, 295, 1292 },
    { -0.21371378004550934f, 0.5699034333229065f }, { -0.4274275600910187f, 0.6411413550376892f },
    1, 0.5f, 0.10000000149011612f,
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_st_init(state, &CLUB, buffer, k);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    rvb_st_process(state, &CLUB, io, io->knobs);
}

const struct wvfx_desc rvb_club_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x101),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Club",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_st),
    .buffer_size = RVB_ST_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
