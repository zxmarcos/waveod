/*
 * Gate 2 (DYN): the original's Gate 2 (QME-200 V1.2.2, library entry 0.12, id 0x0000001D) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). The gate (gate.h) with its times by Attack (1 .. 20 ms) and Release
 * (10 .. 200 ms).
 */
#include "gate.h"

static const struct wvfx_param params[] = {
    { "Threshold", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Attack", 0.0f, 100.0f, 1.0f, 25.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Release", 0.0f, 100.0f, 1.0f, 60.0f, 2, 0x15, 0x00, 0, 0, 0 },
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct gate *s = state;
    (void)buffer;
    (void)env;
    *s = (struct gate){ 0 };
    s->thr_knob = k[0] * 0.01f;
    s->threshold = gate_threshold(k[0]);
    s->att_knob = k[1] * 0.01f;
    s->rel_knob = k[2] * 0.01f;
    s->attack = gate_coeff(s->att_knob, 0.019, 0.001);
    s->release = gate_coeff(s->rel_knob, 0.19, 0.01);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    if (io->frames == 0)
        return;
    float att = k[1] * 0.01f, rel = k[2] * 0.01f;
    gate_run(state, io, k[0] * 0.01f, att, rel, gate_threshold(k[0]), gate_coeff(att, 0.019, 0.001),
             gate_coeff(rel, 0.19, 0.01));
}

const struct wvfx_desc dyn_gate_2_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x10C),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "Gate 2",
    .category = "Gate",
    .params = params,
    .state_size = sizeof(struct gate),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};