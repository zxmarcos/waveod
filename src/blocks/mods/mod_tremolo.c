/*
 * Tremolo (MOD › Tremolo): the original's Tremolo (QME-200 V1.2.2, library entry 8.17, id 0x04000021) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The left input, a tremolo on each output in opposite phase.
 * Single precision.
 *
 * The LFO: asin(sin(4 pi r n)) 2 / pi, a triangle at twice the rate r, clipped to +-0.5 (so a trapezoid), its sample
 * count n starting over once it reaches 1 / r. The outputs: x - d x + d x (0.5 +- t). Depth (d) and the rate move
 * 0.00022676 of the way to the knobs each sample, from where they were at the block's start; the rate starts at 0.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x12, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x21, 0, 0, 0 },
};

struct state {
    float depth, rate;
    int n;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ k[0] * 0.01f, 0.0f, 0 };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[2], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->depth += dd;
        float x = p[0], period = 1.0f / s->rate;
        float t = st_asinf(st_sinf((s->rate * 12.566370964050293f) * (float)s->n)) * 0.6366197466850281f;
        t = __builtin_fmaxf(__builtin_fminf(t, 0.5f), -0.5f);     /* VMINNM, VMAXNM */
        s->n++;
        float dry = st_fms(s->depth, x, x), wet = s->depth * x;
        p[0] = st_fma(wet, t + 0.5f, dry);
        p[1] = st_fma(wet, 0.5f - t, dry);
        if (period <= (float)s->n)
            s->n = 0;
    }
}

const struct wvfx_desc mod_tremolo_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x111),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Tremolo",
    .category = "Tremolo",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
