/*
 * Triangle Trem (MOD › Tremolo): the original's Triangle Trem (QME-200 V1.2.2, library entry 8.19, id 0x04000027) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input, the result on both outputs.
 * Single precision.
 *
 * The LFO: asin(sin(2 pi r n)) 2 / pi clipped to +-0.5, doubled and clipped to +-0.625. The gain as the Sine Trem's:
 * x - d (x - (0.5 + 0.8 t) x), x Volume, then the fixed tone section x 2 on each output (`trem.h`); the left one sums
 * its terms in another order, so the two outputs may differ in the last bit.
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
    struct sos_state left, right;
};

/* the left output's section: b1 x1 + b2 x2 first, the denominator taken off, b0 x last */
static inline float tone_left(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b2, s->x2, c->b1 * s->x1);
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, ((n - d) + x * c->b0) / c->a0);
}

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
        float t = st_asinf(st_sinf(ph)) * 0.6366197466850281f;
        t = __builtin_fmaxf(__builtin_fminf(t, 0.5f), -0.5f);                         /* VMINNM, VMAXNM */
        t = __builtin_fmaxf(__builtin_fminf(t + t, 0.625f), -0.625f);
        float g = st_fma(t, -0.800000011920929f, -0.5f);
        float u = st_fms(st_fma(g, x, x), s->t.depth, x) * s->t.volume;
        float l = tone_left(&TREM_TONE, &s->left, u), r = sos_run_div(&TREM_TONE, &s->right, u);
        trem_count(&s->t, period);
        p[0] = l + l;
        p[1] = r + r;
    }
}

const struct wvfx_desc mod_triangle_trem_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x113),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Triangle Trem",
    .category = "Tremolo",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
