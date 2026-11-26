/*
 * Filter (FX): the original's Filter (QME-200 V1.2.2, library entry 1.0, id 0x01000019; its code calls it the Path
 * Filter) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A step filter, mono (the left
 * input; both outputs the same). Single precision.
 *
 * x through a fixed 2nd-order high pass (divided by its a0), then a band pass (x 7.0795 in its numerator) whose
 * frequency steps through the four Step knobs: w = 0.0598 + 0.2394 s rad, alpha = sin w / (12 - 8 s) (s = Step / 100),
 * the band pass (7.0795 alpha (x - x2) + 2 cos w y1 + (alpha - 1) y2) / (1 + alpha). The step comes from a counter j
 * (+1 per sample, back to 0 at 4 q, q = 0.25 / rate): j up to q Step 1, then 2, 3, 4; outside the four (the rate
 * moved) the value of the sample before, which at a block's first sample is the rate's step (as the original). The
 * rate (Rate Hz, or with Sync on a tempo division, mod.h) moves 0.00022676 of the way from the block's start each
 * sample. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../mods/mod.h"

static const struct wvfx_param params[] = {
    { "Step 1", 0.0f, 100.0f, 1.0f, 10.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Step 2", 0.0f, 100.0f, 1.0f, 30.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Step 3", 0.0f, 100.0f, 1.0f, 55.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Step 4", 0.0f, 100.0f, 1.0f, 85.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.0f, 100.0f, 1.0f, 10.0f, 4, 0x15, 0x15, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x24, 0, 0, 0 },
};

static const struct sos HIGHPASS = {
    0.9999492168426514f, -1.9998984336853027f, 0.9999492168426514f, 1.0100756883621216f, -1.999796986579895f,
    0.9899242520332336f,
};

struct state {
    float rate, j;
    struct sos_state hp;
    float x1, x2, y1, y2;
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
    const float *k = io->knobs;
    float rate = mod_rate(k[4], k[5], io->beat_ms);
    if (!io->enabled)
        return;
    const float step[4] = { k[0] * 0.01f, k[1] * 0.01f, k[2] * 0.01f, k[3] * 0.01f };
    float dr = (rate - s->rate) * 0.0002267600066261366f, prev = dr;
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float x = sos_run_div(&HIGHPASS, &s->hp, p[0]);
        s->rate = s->rate + dr;
        float q = 0.25f / s->rate, j = s->j, v = prev;
        if (j >= 0.0f && j <= q)
            v = step[0];
        else if (j > q && j <= q + q)
            v = step[1];
        else if (j > q + q && j <= q * 3.0f)
            v = step[2];
        else if (j > q * 3.0f && j <= q * 4.0f)
            v = step[3];
        prev = v;
        float j1 = j + 1.0f;
        s->j = q * 4.0f > j1 ? j1 : 0.0f;
        float w = st_fma(v, 0.23935943841934204f, 0.05983985960483551f);
        float alpha = st_sinf(w) / st_fma(v, -8.0f, 12.0f);
        float c = st_cosf(w);
        float y = st_fma(s->y2, alpha - 1.0f, st_fma(s->y1, c + c, st_fma(s->x2, alpha * -7.079500198364258f,
                                                                         (alpha * 7.079500198364258f) * x)));
        y = y / (alpha + 1.0f);
        s->x2 = s->x1;
        s->x1 = x;
        s->y2 = s->y1;
        s->y1 = y;
        p[0] = p[1] = y;
    }
}

const struct wvfx_desc fx_filter_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x100),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Filter",
    .category = "Filter",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
