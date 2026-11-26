/*
 * Sine Trem (MOD › Tremolo): the original's Sine Trem (QME-200 V1.2.2, library entry 8.18, id 0x04000026) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input, the result on both outputs. Single
 * precision.
 *
 * The LFO: sin(2 pi r n) clipped to +-0.625. The gain: x - d (x - (0.5 + 0.8 t) x), x Volume, then the fixed tone
 * section x 2 (`trem.h`; the original runs it twice, once per output, on the same samples: the same result).
 */
#include "trem.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x13, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
};

struct state {
    struct trem t;
    struct sos_state tone;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    trem_init(&s->t, k[0], k[2]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[3], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    trem_block(&s->t, k[0], k[2], rate);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float ph = trem_step(&s->t), period = 1.0f / s->t.rate, x = p[0];
        float t = __builtin_fmaxf(__builtin_fminf(st_sinf(ph), 0.625f), -0.625f);     /* VMINNM, VMAXNM */
        float g = st_fma(t, -0.800000011920929f, -0.5f);
        float u = st_fms(st_fma(g, x, x), s->t.depth, x) * s->t.volume;
        float y = sos_run_div(&TREM_TONE, &s->tone, u);
        trem_count(&s->t, period);
        p[0] = y + y;
        p[1] = y + y;
    }
}

const struct wvfx_desc mod_sine_trem_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x112),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sine Trem",
    .category = "Tremolo",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
