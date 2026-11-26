/*
 * Flanger (MOD › Flanger): the original's Flanger (QME-200 V1.2.2, library entry 8.4, id 0x04000011) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The left input into two 250-sample delay lines, one per
 * output, swept in opposite phase. Single precision.
 *
 * Each line takes the input plus Feedback x its last read, and is read 5 + 105 Pre Delay + (0.5 +- 0.5 sin(2 pi r n))
 * (13 + 119 Depth) samples back (linear interpolation). Out: 0.8 x + 0.5 x the read. Depth, Pre Delay, Feedback and
 * the rate glide 0.00022676 of the way to the knobs each sample, the rate from 0; the count n starts over at one
 * period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x14, 0, 0, 0 },
    { "Pre Delay", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x21, 0, 0, 0 },
};

struct state {
    float depth, pre, feedback, rate;
    int n;
    float fl, fr;                       /* each line's last read */
    MOD_LINE(250) l, r;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.01f;
    s->pre = k[2] * 0.01f;
    s->feedback = k[3] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[4], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float dp = (k[2] * 0.01f - s->pre) * 0.0002267600066261366f;
    float df = (k[3] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->depth += dd;
        s->pre += dp;
        s->feedback += df;
        float period = 1.0f / s->rate, x = p[0];
        float h = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 0.5f;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        float pre = st_fma(s->pre, 105.0f, 5.0f), width = st_fma(s->depth, 119.0f, 13.0f);
        s->fl = mod_line_run(&s->l, st_fma(s->fl, s->feedback, x), st_fma(h + 0.5f, width, pre));
        s->fr = mod_line_run(&s->r, st_fma(s->fr, s->feedback, x), st_fma(0.5f - h, width, pre));
        float dry = x * 0.800000011920929f;
        p[0] = st_fma(s->fl, 0.5f, dry);
        p[1] = st_fma(s->fr, 0.5f, dry);
    }
}

const struct wvfx_desc mod_flanger_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x104),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Flanger",
    .category = "Flanger",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
