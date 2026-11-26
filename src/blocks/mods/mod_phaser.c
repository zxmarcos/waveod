/*
 * Phaser (MOD › Phaser): the original's Phaser (QME-200 V1.2.2, library entry 8.11, id 0x04000019) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a 4-stage phaser on each output, their
 * sweeps in opposite phase. Single precision.
 *
 * The LFO m = 0.085674 sin(2 pi r n) moves the four 1st-order allpasses of each output (`ap1`, the left's from
 * +m, the right's from -m); stage 2 takes stage 1's output less 0.44 x the last stage-4 output (the feedback). Out:
 * stage 4 + the input. The rate glides 0.00022676 of the way to the knob each sample, from 0; the sample count n
 * starts over once it reaches one period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 0, 0x22, 0x11, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 1, 0x00, 0x20, 0, 0, 0 },
};

struct state {
    float rate;
    int n;
    struct ap1_state l[4], r[4];
};

/* the four stages of one output; feedback from its last stage-4 output */
static float phaser_run(const struct ap1 *c, struct ap1_state *st, float x)
{
    float y = ap1_run(c, &st[0], x);
    y = ap1_run(c, &st[1], st_fms(st[3].y1, 0.4399999976158142f, y));
    y = ap1_run(c, &st[2], y);
    return ap1_run(c, &st[3], y);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env; (void)k;
    *(struct state *)state = (struct state){ 0 };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float rate = mod_rate(io->knobs[0], io->knobs[1], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        float period = 1.0f / s->rate, x = p[0];
        float m = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 0.08567400276660919f;
        const struct ap1 cl = { m + 0.16505084931850433f, -0.18566514551639557f - m, -0.16505084931850433f - m,
                                m + 0.18566514551639557f };
        const struct ap1 cr = { 0.16505084931850433f - m, m + -0.18566514551639557f, m + -0.16505084931850433f,
                                0.18566514551639557f - m };
        float l = phaser_run(&cl, s->l, x), r = phaser_run(&cr, s->r, x);
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        p[0] = l + x;
        p[1] = r + x;
    }
}

const struct wvfx_desc mod_phaser_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10B),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Phaser",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
