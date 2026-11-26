/*
 * Pan Phaser (MOD › Phaser): the original's Pan Phaser (QME-200 V1.2.2, library entry 8.14, id 0x0400001E) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a 4-stage phaser on each output
 * (the same sweep), then an auto-panner. Single precision except the feedback.
 *
 * The phaser: e = tan(0.014247586 + 0.05699 Phs Depth (0.5 + 0.5 sin(2 pi r n))), g = (e - 1) / (e + 1); four
 * 1st-order allpasses y = x1 + (x - y1) g per output (each stage sums it in its own order), stage 2 taking stage 1
 * less 0.44 x the output's last stage-4 output, computed in double. The panner: each output (stage 4 + the input)
 * x (1 - p + p (0.5 +- 0.5 sin(2 pi r' n'))), p = Pan Depth. Both depths and both rates glide 0.00022676 of the way
 * to the knobs each sample (the rates from 0); each LFO counts its own samples, starting over at one period.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Phs Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Phs Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x14, 0, 0, 0 },
    { "Pan Depth", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Pan Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 3, 0x22, 0x15, 0, 0, 0 },
    { "Phs Sync", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x21, 0, 0, 0 },
    { "Pan Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x23, 0, 0, 0 },
};

struct state {
    float depth, pan, rate, pan_rate;   /* glided */
    int n, pan_n;
    struct ap1_state l[4], r[4];
};

/* one output's four stages */
static float stages(struct ap1_state *st, float x, float g)
{
    float y = st_fms(st[0].y1, g, st[0].x1 + g * x);
    st[0] = (struct ap1_state){ x, y };
    x = (float)st_fmad(-(double)st[3].y1, 0.44, (double)y);           /* the feedback, in double */
    y = st_fma(g, x, st_fms(st[1].y1, g, st[1].x1));
    st[1] = (struct ap1_state){ x, y };
    for (int i = 2; i < 4; i++) {
        x = y;
        y = st_fma(x - st[i].y1, g, st[i].x1);
        st[i] = (struct ap1_state){ x, y };
    }
    return y;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = k[0] * 0.01f;
    s->pan = k[2] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[4], io->beat_ms), pan_rate = mod_rate(k[3], k[5], io->beat_ms);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (k[0] * 0.01f - s->depth) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float dp = (k[2] * 0.01f - s->pan) * 0.0002267600066261366f;
    float dq = (pan_rate - s->pan_rate) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->pan_rate += dq;
        s->depth += dd;
        s->pan += dp;
        float period = 1.0f / s->rate, pan_period = 1.0f / s->pan_rate, x = p[0];
        float lfo = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n);
        float pan = st_sinf((s->pan_rate * 6.2831854820251465f) * (float)s->pan_n) * 0.5f;
        s->n++;
        s->pan_n++;
        if (period <= (float)s->n)
            s->n = 0;
        if (pan_period <= (float)s->pan_n)
            s->pan_n = 0;
        float e = st_tanf(st_fma(s->depth * 0.05699034035205841f, st_fma(lfo, 0.5f, 0.5f), 0.014247586019337177f));
        float g = (e + -1.0f) / (e + 1.0f);
        float l = stages(s->l, x, g), r = stages(s->r, x, g), dry = 1.0f - s->pan;
        p[0] = st_fma(pan + 0.5f, s->pan, dry) * (l + x);
        p[1] = st_fma(0.5f - pan, s->pan, dry) * (r + x);
    }
}

const struct wvfx_desc mod_pan_phaser_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10E),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Pan Phaser",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
