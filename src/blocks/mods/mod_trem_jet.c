/*
 * Trem Jet (MOD › Flanger): the original's Trem Jet (QME-200 V1.2.2, library entry 8.6, id 0x04000014) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). A flanger and a tremolo with their own LFOs. The left
 * input into two 200-sample delay lines, one per output, swept in opposite phase. Single precision except the mix.
 *
 * Each line takes the input plus Feedback x its last read, clipped to +-1.7, and is read 44.1 + (0.5 +- 0.5 sin(2 pi
 * r n)) (13 + 119 Flg Depth) samples back (linear interpolation). Out: (0.8 x + 0.7 x the read, in double) x
 * (1 - t + t (0.5 + 0.5 sin(2 pi r' n'))), t = Trm Depth. Both depths, Feedback and both rates glide 0.00022676 of
 * the way to the knobs each sample (the rates from 0); each LFO counts its own samples, starting over at one period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Flg Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Flg Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x15, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trm Depth", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Trm Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 4, 0x22, 0x16, 0, 0, 0 },
    { "Flg Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x21, 0, 0, 0 },
    { "Trm Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x24, 0, 0, 0 },
};

struct state {
    float depth, feedback, trem, rate, trem_rate;   /* glided */
    int n, trem_n;
    float fl, fr;                       /* each line's last read */
    MOD_LINE(200) l, r;
};

static inline float line_in(float x, float last, float feedback)
{
    return __builtin_fmaxf(__builtin_fminf(st_fma(last, feedback, x), 1.7000000476837158f), -1.7000000476837158f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->trem = k[3] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[5], io->beat_ms), trem_rate = mod_rate(k[4], k[6], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float df = (k[2] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dt = (k[3] * 0.01f - s->trem) * 0.0002267600066261366f;
    float dq = (trem_rate - s->trem_rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->trem_rate += dq;
        s->depth += dd;
        s->feedback += df;
        s->trem += dt;
        float period = 1.0f / s->rate, trem_period = 1.0f / s->trem_rate, x = p[0];
        float h = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 0.5f;
        float lfo = st_sinf((s->trem_rate * 6.2831854820251465f) * (float)s->trem_n);
        s->n++;
        s->trem_n++;
        if (period <= (float)s->n)
            s->n = 0;
        if (trem_period <= (float)s->trem_n)
            s->trem_n = 0;
        float width = st_fma(s->depth, 119.0f, 13.0f);
        s->fl = mod_line_run(&s->l, line_in(x, s->fl, s->feedback), st_fma(h + 0.5f, width, 44.099998474121094f));
        s->fr = mod_line_run(&s->r, line_in(x, s->fr, s->feedback), st_fma(0.5f - h, width, 44.099998474121094f));
        float g = st_fma(st_fma(lfo, 0.5f, 0.5f), s->trem, 1.0f - s->trem);
        double dry = (double)x * 0.8;
        p[0] = g * (float)st_fmad((double)s->fl, 0.7, dry);
        p[1] = g * (float)st_fmad((double)s->fr, 0.7, dry);
    }
}

const struct wvfx_desc mod_trem_jet_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x106),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Trem Jet",
    .category = "Flanger",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
