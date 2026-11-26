/*
 * Room (REVERB): the original's Room reverb (QME-200 V1.2.2, library entry 10.2, id 0x0C000000) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The FDN engine (rvb_fdn.h): lines of 1369, 1849, 3125 and 2809
 * samples, Decay = 0.75 pow0_5(Decay), modulation depth 0.15, the left sum unscaled. The Init sets the matrix from
 * pow0_5(Decay) x 0.530325 (0.75 x 0.7071 folded, kept): it stays so until Decay moves.
 */
#include "rvb_fdn.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x73, 0x00, 0, 0, 0 },
    { "Decay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

static const struct rvb_fdn_model ROOM = {
    0.75f, 0.0f, 0.15000000596046448f, 0.0f, 1.0f, 0.5303249955177307f, 0, 0, 0,
    { 1369.0f, 1849.0f, 3125.0f, 2809.0f },
    { -0.8617790937423706f, 1.5588502883911133f, -0.6879372596740723f, 1.0f, -1.7388103008270264f,
      0.7490588426589966f },
    { 0.1215955913066864f, 0.2431911826133728f, 0.1215955913066864f, 1.4622604846954346f,
      -1.5136176347732544f, 0.5377395749092102f },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)env;
    rvb_fdn_init(state, &ROOM, buffer, k[0], k[1], k[2]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    rvb_fdn_process(state, &ROOM, io, k[0], k[1], rvb_fdn_decay(&ROOM, k[2]), k[3], 0.0f, 0.0f);
}

const struct wvfx_desc rvb_room_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 0x102),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Room",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct rvb_fdn),
    .buffer_size = RVB_FDN_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
