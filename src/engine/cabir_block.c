/*
 * The Cab / User IR engine (engine/cabir.h) as a block of waveod's (sdk/wvfx.h): an engine block, run for every
 * item of the Cab and User IR menus while GLOBAL › PRESETS › MOTOR CAB / IR = WAVEOD. Its parameters, the virtual mic's
 * (engine/cabmic.h), come after the library entry's own (VOLUME, LOW CUT, HIGH CUT).
 */
#include "engine/cabir.h"
#include "engine/cabmic.h"
#include "sdk/wvfx.h"

static const struct wvfx_param params[CABMIC_PARAMS] = {
    { "Mic Position", -100.0f, 100.0f, 5.0f, 0.0f, CABMIC_KNOB_POSITION, WVFX_FMT_SIGNED, 0, 0, 0, 0 },
    { "Mic Distance", -100.0f, 100.0f, 5.0f, 0.0f, CABMIC_KNOB_DISTANCE, WVFX_FMT_SIGNED, 0, 0, 0, 0 },
    { "Mic Angle", 0.0f, 45.0f, 1.0f, 0.0f, CABMIC_KNOB_ANGLE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    /* "Original", then cabmic_type_names (tests/test_cabir.c checks they match) */
    { "Mic Type", 0.0f, (float)CABMIC_TYPES, 1.0f, 0.0f, CABMIC_KNOB_TYPE, 0, 0, 0, "Original\0" "57 Dynamic\0", 0 },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    (void)buffer;
    cabir_init(state, knobs, env->data, env->max_frames);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    cabir_process(state, io->io, io->frames, io->knobs, io->enabled);
}

static int poll(void *state, const float *knobs)
{
    return cabir_poll(state, knobs);
}

const struct wvfx_desc cabir_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_CAB, 0),
    .menu = WVFX_CAB,
    .nparams = CABMIC_PARAMS,
    .flags = WVFX_ENGINE | WVFX_INHERIT,
    .name = "waveod Cab / IR",
    .category = "",
    .params = params,
    .state_size = sizeof(struct cabir),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = poll,
};
