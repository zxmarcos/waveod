/*
 * Volume (VOL): the original's Volume (QME-200 V1.2.2, library entry 15.0, id 0x06000003) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A gain on both channels: Volume / 100, 1 while bypassed.
 *
 * The gain g (0 after the Init, which does not read the knob) moves to its target by (target - g) / frames per
 * sample, the step held to +-0.0002 (the original's compares: a step of 0.0002 or more is 0.0002, of -0.0002 or
 * less -0.0002), and multiplies both channels. Single precision.
 */
#include "../sdk/wvfx.h"

static const struct wvfx_param params[] = {
    { "Volume", 0.0f, 100.0f, 1.0f, 100.0f, 0, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    float gain;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    (void)k;
    s->gain = 0.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float target = io->enabled ? io->knobs[0] * 0.009999999776482582f : 1.0f;
    float g = s->gain, step = (target - g) / (float)(uint32_t)io->frames;
    /* the original's BGE / BLE / LT after VCMP: a NaN takes the BLE and the LT (-0.0002) */
    if (step >= 0.00019999999494757503f || !(step > -0.00019999999494757503f))
        step = !(step >= 0.0f) ? -0.00019999999494757503f : 0.00019999999494757503f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        g += step;
        p[0] *= g;
        p[1] *= g;
    }
    s->gain = g;
}

const struct wvfx_desc vol_volume_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_VOL, 0x100),
    .menu = WVFX_VOL,
    .nparams = sizeof params / sizeof params[0],
    .name = "Volume",
    .category = "Volume",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
