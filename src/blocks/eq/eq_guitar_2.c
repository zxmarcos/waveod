/*
 * Guitar EQ 2 (EQ): the original's Guitar EQ 2 (QME-200 V1.2.2, library entry 7.1, id 0x01000036) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). A five-band EQ, mono (the left input; both outputs the
 * same). Single precision.
 *
 * x through each band's peak section twice, in band order, each read from the original's 63-point table
 * (eq_guitar_2_tab.h; point 31 flat) at (int)(31 + 0.62 knob) (knob -50 .. 50), x 2 Volume. Volume glides 0.005
 * per process call, bypassed too. Bypassed it passes the signal.
 */
#include "eq_guitar_2_tab.h"

static const struct wvfx_param params[] = {
    { "100Hz", -50.0f, 50.0f, 1.0f, 0.0f, 0, 0x5d, 0x00, 0, 0, 0 },
    { "500Hz", -50.0f, 50.0f, 1.0f, 0.0f, 1, 0x5d, 0x00, 0, 0, 0 },
    { "1kHz", -50.0f, 50.0f, 1.0f, 0.0f, 2, 0x5d, 0x00, 0, 0, 0 },
    { "3kHz", -50.0f, 50.0f, 1.0f, 0.0f, 3, 0x5d, 0x00, 0, 0, 0 },
    { "6kHz", -50.0f, 50.0f, 1.0f, 0.0f, 4, 0x5d, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos *const BANDS[5] = { GEQ2_PEAK1, GEQ2_PEAK2, GEQ2_PEAK3, GEQ2_PEAK4, GEQ2_PEAK5 };

struct state {
    float volume;
    struct sos_state s[10];
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ .volume = k[5] * 0.01f };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    s->volume = st_glide_by(s->volume, k[5] * 0.01f, 0.004999999888241291f);
    if (!io->frames || !io->enabled)
        return;
    const struct sos *c[5];
    for (int b = 0; b < 5; b++)
        c[b] = &BANDS[b][(int)st_fma(k[b], 0.6200000047683716f, 31.0f)];
    float g = s->volume + s->volume, *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    for (int j = 0; j < 10; j++)                    /* the block's coefficients: section by section */
        sos_block(c[j / 2], &s->s[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = g * y[i];
}

const struct wvfx_desc eq_guitar_2_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_EQ, 0x101),
    .menu = WVFX_EQ,
    .nparams = sizeof params / sizeof params[0],
    .name = "Guitar EQ 2",
    .category = "EQ",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
