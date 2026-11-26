/*
 * Gate 1 (DYN): the original's Gate 1 (QME-200 V1.2.2, library entry 0.11, id 0x0000001B) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). The gate (gate.h) with fixed times: attack 0.9835, release 0.9995.
 */
#include "gate.h"

static const struct wvfx_param params[] = {
    { "Threshold", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct gate *s = state;
    (void)buffer;
    (void)env;
    *s = (struct gate){ 0 };
    s->thr_knob = k[0] * 0.01f;
    s->threshold = gate_threshold(k[0]);
    s->attack = 0.9835000038146973f;
    s->release = 0.9994999766349792f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    if (io->frames == 0)
        return;
    gate_run(state, io, k[0] * 0.01f, 0.0f, 0.0f, gate_threshold(k[0]), 0.0f, 0.0f);
}

const struct wvfx_desc dyn_gate_1_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x10B),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "Gate 1",
    .category = "Gate",
    .params = params,
    .state_size = sizeof(struct gate),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};