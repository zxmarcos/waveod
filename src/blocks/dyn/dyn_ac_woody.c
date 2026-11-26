/*
 * AC Woody (DYN): the original's AC Woody (QME-200 V1.2.2, library entry 0.9, id 0x01000000; its code calls it the
 * Acoustic Refiner) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). An acoustic body
 * filter, mono (the left input; both outputs the same). Single precision.
 *
 * x through eleven 2nd-order peak sections: peaks 1 to 5 twice each, then peak 100 once, their coefficients read
 * from the original's tables of 63 points (dyn_woody.h; point 31 flat) at points the Shape knob picks, each its own
 * way. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"
#include "dyn_woody.h"

static const struct wvfx_param params[] = {
    { "Shape", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    struct sos_state sec[11];
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    (void)k;
    *s = (struct state){ 0 };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float k = io->knobs[0], h = k * 0.1574999839067459f;
    const struct sos *c[11];
    c[0] = c[1] = &WOODY_PEAK1[(int)st_fma(k, 0.3149999678134918f, 31.499996185302734f)];
    c[2] = c[3] = &WOODY_PEAK2[(int)(31.499996185302734f - h)];
    c[4] = c[5] = &WOODY_PEAK3[(int)st_fma(st_mid[(int)k], -0.24705879390239716f, 31.499996185302734f)];
    c[6] = c[7] = &WOODY_PEAK4[(int)st_fma(k, 0.07874999195337296f, 31.499996185302734f)];
    c[8] = c[9] = &WOODY_PEAK5[(int)(h + 24.70587921142578f)];
    c[10] = &WOODY_PEAK100[(int)(h + 31.499996185302734f)];
    float *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0)
        return;
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    for (int j = 0; j < 11; j++)                    /* the coefficients are the block's: section by section */
        sos_block(c[j], &s->sec[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i];
}

const struct wvfx_desc dyn_ac_woody_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x109),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "AC Woody",
    .category = "Acoustic",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
