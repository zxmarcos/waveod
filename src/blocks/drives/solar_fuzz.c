/*
 * Solar Fuzz (DRIVE › Fuzz): the original's Solar Fuzz (QME-200 V1.2.2, library entry 3.11, id 0x03000026) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample: x 50, a fixed 1st-order pre-filter, x Fuzz (B25), the hard clip +-0.6 on 2 points per sample with
 * its averages (fuzz.h), a fixed 2nd-order post filter, x Volume (B25) x 2.4. Fuzz and Volume glide 0.005 per block
 * of samples (also while bypassed).
 */
#include "fuzz.h"

static const struct wvfx_param params[] = FUZZ_PARAMS;

static const struct fos PRE = { 2.98095703125f, -2.98046875f, -0.99951171875f };
static const struct sos POST = { 0.43359375f, -0.7802734375f, 0.3544921875f, 1.0f, -1.736328125f, 0.7509765625f };

struct state {
    float gain, volume;         /* glided */
    struct fos_state pre;
    struct fz2 st;
    struct sos_state post;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_b25, k[0]);
    s->volume = st_taper(st_b25, k[1]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->gain = st_glide_by(s->gain, st_taper(st_b25, io->knobs[0]), 0.005f);
    s->volume = st_glide_by(s->volume, st_taper(st_b25, io->knobs[1]), 0.005f);
    if (!io->enabled)
        return;
    float level = s->volume * 2.4000000953674316f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = fos_run(&PRE, &s->pre, p[0] * 50.0f) * s->gain;
        x = sos_run(&POST, &s->post, fz2_run(&s->st, x, 0.6000000238418579f));
        float y = level * x;
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc solar_fuzz_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x126),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Solar Fuzz",
    .category = "Fuzz",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
