/*
 * B-Chorus (MOD › Chorus): the original's B-Chorus (QME-200 V1.2.2, library entry 8.2, id 0x04000008) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a chorus voice per output, swept in
 * opposite phase, added to the dry signal. Single precision.
 *
 * The wet path: a fixed 1st-order high pass and a 3rd-order low pass, then two 300-sample delay lines read
 * 154 + (45.5 +- 28.966 asin(sin(4 pi r n))) d samples back (a triangle; linear interpolation), each through the
 * same 3rd-order low pass. Out: x + wet x Volume. Depth (d = Depth x 0.007), Volume and the rate glide 0.00022676 of
 * the way to the knobs each sample, the rate from 0; the count n starts over at one period.
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x13, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
};

static const struct fos HP = { 0.9985772967338562f, -0.9985772967338562f, -0.9971545338630676f };
static const struct lp3 LP = {
    0.0449041910469532f, 0.1347125619649887f, 0.1347125619649887f, 0.0449041910469532f, -1.229875087738037f,
    0.7447680830955505f, -0.15565942227840424f,
};

struct state {
    float depth, volume, rate;
    int n;
    struct fos_state hp;
    struct lp3_state pre, pl, pr;
    MOD_LINE(300) l, r;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.00699999975040555f;
    s->volume = k[2] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[3], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.00699999975040555f - s->depth) * 0.0002267600066261366f;
    float dv = (k[2] * 0.01f - s->volume) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = p[0], w = lp3_run(&LP, &s->pre, fos_run(&HP, &s->hp, x));
        s->rate += dr;
        s->depth += dd;
        s->volume += dv;
        float period = 1.0f / s->rate;
        float t = st_asinf(st_sinf((s->rate * 12.566370964050293f) * (float)s->n)) * 28.966197967529297f;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        float l = mod_line_run(&s->l, w, st_fma(t + 45.5f, s->depth, 154.0f));
        float r = mod_line_run(&s->r, w, st_fma(45.5f - t, s->depth, 154.0f));
        p[0] = st_fma(lp3_run(&LP, &s->pl, l), s->volume, x);
        p[1] = st_fma(lp3_run(&LP, &s->pr, r), s->volume, x);
    }
}

const struct wvfx_desc mod_b_chorus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x102),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "B-Chorus",
    .category = "Chorus",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
