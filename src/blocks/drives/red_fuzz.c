/*
 * Red Fuzz (DRIVE › Fuzz): the original's Red Fuzz (QME-200 V1.2.2, library entry 3.13, id 0x03000024) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: x 400, a fixed 1st-order pre-filter, x Fuzz (B25), a fixed 2nd-order filter, the hard clip +-1 on 2
 * points per sample with its averages (fuzz.h), a fixed 1st-order post filter, x Volume (B25) x 8. Fuzz and Volume
 * glide 0.005 per block of samples (also while bypassed).
 */
#include "fuzz.h"

static const struct wvfx_param params[] = FUZZ_PARAMS;

static const struct fos PRE1 = { 0.033203125f, 0.033203125f, -0.93359375f };
static const struct sos PRE2 = { 0.44921875f, 0.8994140625f, 0.44921875f, 1.0f, 0.568359375f, 0.2294921875f };
static const struct fos POST = { 0.076416015625f, -0.076416015625f, -0.997314453125f };

struct state {
    float gain, volume;         /* glided */
    struct fos_state pre1, post;
    struct sos_state pre2;
    struct fz2 st;
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
    float level = s->volume * 8.0f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = fos_run(&PRE1, &s->pre1, p[0] * 400.0f) * s->gain;
        x = sos_run(&PRE2, &s->pre2, x);
        x = fos_run(&POST, &s->post, fz2_run(&s->st, x, 1.0f));
        float y = level * x;
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc red_fuzz_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x124),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Red Fuzz",
    .category = "Fuzz",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
