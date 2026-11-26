/*
 * CE-Roto (MOD › Vibrato): the original's CE-Roto (QME-200 V1.2.2, library entry 8.10, id 0x04000016) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The E-Chorus's vibrato sibling (its sections in
 * chorus.h): the left input, a vibrato voice per output in opposite phase. Single precision except the level, the
 * feedback and the delay time, in double.
 *
 * The path: Volume x x / 4 through the input section (y2), then per output y2 + (0.5 + 0.3 d) x that output's last
 * voice, the 3rd-order low pass (each output sums it in its own order), a 300-sample line read
 * 82 + 87 d (1 +- sin(2 pi r n)) / 2 samples back (linear interpolation), a 1st-order low pass that follows d and the
 * 4th-order low pass (the voice). Out: 0.066666 y2 + 0.68 x the voice.
 *
 * d = 0.1 + 0.9 Depth glides 0.001 per process call, also while bypassed; the low pass is recomputed only while d is
 * still on its way, so it keeps the value one step short of the knob (kept). Volume and the rate glide 0.00022676 of
 * the way to the knobs each sample; the rate starts at the knob's (the Init reads the tempo: here the first process
 * call does, from the Init's knobs). The count n starts over at one period.
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x13, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
};

struct voice {
    struct lp3_state lp3;
    MOD_LINE(300) line;
    struct fos_state lp;
    struct ce_lp4_state lp4;
    float last;                         /* the voice's last output (the feedback) */
};

struct state {
    float depth, volume, rate;
    float rate0, sync0;                 /* the Init's Rate and Sync, for the first process call */
    int n, started;
    struct fos lpc;                     /* the 1st-order low pass, a0 in lpa0 */
    float lpa0;
    struct fos_state in;
    struct voice l, r;
};

/* the 3rd-order low pass as each output sums it: from b1 x1, then b0 x, b2 x2 (left) or b2 x2, b0 x (right) */
static float lp3_left(const struct lp3 *c, struct lp3_state *s, float x)
{
    float n = st_fma(c->b3, s->x3, st_fma(c->b2, s->x2, st_fma(c->b0, x, c->b1 * s->x1)));
    float d = st_fma(c->a3, s->y3, st_fma(c->a2, s->y2, c->a1 * s->y1));
    float y = n - d;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

static float lp3_right(const struct lp3 *c, struct lp3_state *s, float x)
{
    float n = st_fma(c->b3, s->x3, st_fma(c->b0, x, st_fma(c->b2, s->x2, c->b1 * s->x1)));
    float d = st_fma(c->a3, s->y3, st_fma(c->a2, s->y2, c->a1 * s->y1));
    float y = n - d;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

static float voice_run(struct voice *v, const struct state *s, float in, float d, int right)
{
    float w = (right ? lp3_right : lp3_left)(&CE_LP3, &v->lp3, in);
    w = fos_run_div(&s->lpc, s->lpa0, &v->lp, mod_line_run(&v->line, w, d));
    v->last = ce_lp4_run(&v->lp4, w);
    return v->last;
}

static float depth_of(float k)
{
    return (float)st_fmad((double)(k * 0.01f), 0.9, 0.1);
}

static void lowpass_set(struct state *s, float d)
{
    s->lpc = (struct fos){ 0.6055259704589844f, 0.6055259704589844f, -1.3944740295410156f - d };
    s->lpa0 = d + 2.6055259704589844f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->depth = depth_of(k[0]);
    s->volume = k[2] * 0.01f;
    s->rate0 = k[1];
    s->sync0 = k[3];
    lowpass_set(s, s->depth);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->rate = mod_rate(s->rate0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = depth_of(k[0]), rate = mod_rate(k[1], k[3], io->beat_ms);
    s->depth = st_glide_by(s->depth, target, 0.0010000000474974513f);
    if (!io->enabled || io->frames == 0)
        return;
    if (s->depth != target)
        lowpass_set(s, s->depth);
    float dv = (k[2] * 0.01f - s->volume) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    double fb = st_fmad((double)s->depth, 0.3, 0.5);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->volume += dv;
        s->rate += dr;
        float x = (float)(((double)s->volume * 0.25) * (double)p[0]);
        float y2 = fos_run_div(&CE_IN, 1.0f, &s->in, x), period = 1.0f / s->rate;
        float in_l = (float)st_fmad(fb, (double)s->l.last, (double)y2);
        float in_r = (float)st_fmad(fb, (double)s->r.last, (double)y2);
        float m = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 87.0f;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        float dl = (float)st_fmad((double)((m + 87.0f) * s->depth), 0.5, 2.0) + 80.0f;
        float dr2 = (float)st_fmad((double)((87.0f - m) * s->depth), 0.5, 2.0) + 80.0f;
        float l = voice_run(&s->l, s, in_l, dl, 0), r = voice_run(&s->r, s, in_r, dr2, 1);
        float dry = y2 * 0.06666599959135056f;
        p[0] = st_fma(l, 0.6800000071525574f, dry);
        p[1] = st_fma(r, 0.6800000071525574f, dry);
    }
}

const struct wvfx_desc mod_ce_roto_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10A),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "CE-Roto",
    .category = "Vibrato",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
