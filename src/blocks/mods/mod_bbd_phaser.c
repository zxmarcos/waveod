/*
 * BBD Phaser (MOD › Phaser): the original's BBD Phaser (QME-200 V1.2.2, library entry 8.12, id 0x0400001A) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a 4-stage phaser on each output,
 * their sweeps in opposite phase. Single precision.
 *
 * The LFO: a triangle t = asin(sin(2 pi r n)) / pi (+-0.5). Each output's sweep u = (0.5 +- t) d sets
 * w = u/4 + (u/2)^4 u/4 and the four 1st-order allpasses (`ap1`) from k = 441 w: (k - 10, -k - 10; -k + 10, k + 10).
 * Out: stage 4 + the input. Depth (d) and the rate glide 0.00022676 of the way to the knobs each sample; the Init
 * takes Depth / 100, the Process Depth / 255 (kept), the rate starts at 0.
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
    struct ap1_state l[4], r[4];
};

static struct ap1 sweep(float u)
{
    float h = u * 0.5f, w = u * 0.25f;
    h = h * h;
    h = h * h;
    w = st_fma(h, w, w);
    float kp = w * 441.0f, kn = w * -441.0f;
    return (struct ap1){ kp + -10.0f, kn + -10.0f, kn + 10.0f, kp + 10.0f };
}

static float stages(const struct ap1 *c, struct ap1_state *st, float x)
{
    for (int i = 0; i < 4; i++)
        x = ap1_run(c, &st[i], x);
    return x;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[2], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.003921568859368563f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->depth += dd;
        float period = 1.0f / s->rate, x = p[0];
        float t = st_asinf(st_sinf((s->rate * 6.2831854820251465f) * (float)s->n)) * 0.31830987334251404f;
        const struct ap1 cl = sweep((t + 0.5f) * s->depth), cr = sweep((0.5f - t) * s->depth);
        float l = stages(&cl, s->l, x), r = stages(&cr, s->r, x);
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        p[0] = l + x;
        p[1] = r + x;
    }
}

const struct wvfx_desc mod_bbd_phaser_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10C),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "BBD Phaser",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
