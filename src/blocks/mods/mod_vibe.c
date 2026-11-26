/*
 * Vibe (MOD › Phaser): the original's Vibe (QME-200 V1.2.2, library entry 8.15, id 0x0400001F) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The left input; a 4-stage vibe on each output, their sweeps in
 * opposite phase. Single precision.
 *
 * Each output's sweep u = (0.5 +- 0.5 sin(2 pi r n)) d moves a 2nd-order section and three 1st-order ones whose
 * coefficients are u plus constants (the original's photocell stages); the input gets 0.12 x the output's last
 * stage-4 output back. Out: stage 4 + the input. Depth (d) and the rate glide 0.00022676 of the way to the knobs each
 * sample, the rate from 0; the sample count n starts over at one period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x12, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x21, 0, 0, 0 },
};

/* a 1st-order stage: b0 = u + k0, b1 = -k1 - u, a0 = u + k1 (y1 weighted by b0 too) */
static const float STAGE_K[3][2] = {
    { 0.018799928948283195f, 0.019006071612238884f },
    { -0.029343249276280403f, 0.06714925169944763f },
    { 0.014078374952077866f, 0.023727625608444214f },
};

struct channel {
    struct sos_state s2;                /* the 2nd-order stage */
    struct ap1_state s1[3];             /* the 1st-order stages */
};

struct state {
    float depth, rate;
    int n;
    struct channel l, r;
};

static float stage1(struct ap1_state *st, float u, int i, float x)
{
    float b0 = u + STAGE_K[i][0], b1 = -STAGE_K[i][1] - u;
    float y = st_fma(st->y1, b0, st_fma(st->x1, b1, x * b0)) / (u + STAGE_K[i][1]);
    *st = (struct ap1_state){ x, y };
    return y;
}

static float vibe_run(struct channel *c, float u, float x)
{
    float w = u + 0.01890300028026104f, p = w * 0.24067279696464539f, q = w * 0.759327232837677f;
    float b0 = u + 0.017573198303580284f;
    float y = x * b0;
    y = st_fma(c->s2.x1, -0.002659602789208293f - p, y);
    y = st_fma(c->s2.x2, -0.0013298013946041465f - q, y);
    y = st_fma(c->s2.y1, p + -0.002659602789208293f, y);
    y = st_fma(c->s2.y2, q - 0.001147886854596436f, y);
    y = sos_out(&c->s2, x, y / (u + 0.020414715632796288f));
    for (int i = 0; i < 3; i++)
        y = stage1(&c->s1[i], u, i, y);
    return y;
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
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->depth += dd;
        float period = 1.0f / s->rate, x = p[0];
        float h = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 0.5f;
        float ul = (h + 0.5f) * s->depth, ur = (0.5f - h) * s->depth;
        float l = vibe_run(&s->l, ul, st_fma(s->l.s1[2].y1, 0.11999999731779099f, x));
        float r = vibe_run(&s->r, ur, st_fma(s->r.s1[2].y1, 0.11999999731779099f, x));
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        p[0] = l + x;
        p[1] = r + x;
    }
}

const struct wvfx_desc mod_vibe_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10F),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Vibe",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
