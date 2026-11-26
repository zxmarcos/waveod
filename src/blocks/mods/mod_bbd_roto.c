/*
 * BBD Roto (MOD › Vibrato): the original's BBD Roto (QME-200 V1.2.2, library entry 8.9, id 0x04000015) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a bucket-brigade vibrato per
 * output, swept in opposite phase. Single precision.
 *
 * The path: two fixed 2nd-order input sections, then two 100-sample delay lines read 20 + 34.5 (1 +- sin(2 pi r n)) d
 * samples back (linear interpolation), each through a fixed 4th-order band pass (no x2 term), a 2nd-order low pass
 * and a 3rd-order section. Depth (d) and the rate glide 0.00022676 of the way to the knobs each sample, the rate from
 * 0; the count n starts over at one period. (The sections' a0, all 1, are divided or multiplied in: exact.)
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x12, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x21, 0, 0, 0 },
};

static const struct sos IN1 = {
    -5.389610767364502f, -0.3199824094772339f, 5.0696282386779785f, 1.0f, 0.12308261543512344f,
    -0.48311781883239746f,
};
static const struct sos IN2 = {
    0.6903539896011353f, 1.3807079792022705f, 0.6903539896011353f, 1.0f, 0.8758875727653503f, 0.8855282664299011f,
};
static const struct sos LP = {
    0.5103132724761963f, 1.0206265449523926f, 0.5103132724761963f, 1.0f, 0.40133002400398254f, 0.6399229764938354f,
};
static const struct lp3 OUT = {
    -0.1931397020816803f, 0.1379380077123642f, 0.1931397020816803f, -0.1379380077123642f, -0.9852522015571594f,
    -0.9091520309448242f, 0.8949368596076965f,
};

/* the 4th-order band pass: (b0 x + b1 x1 + b3 x3 + b4 x4) - (a1 y1 + .. + a4 y4) */
static const float BP_B[5] = { 0.11671118438243866f, 0.23342236876487732f, 0.0f, -0.23342236876487732f,
                               -0.11671118438243866f };
static const float BP_A[4] = { -1.2478394508361816f, 0.639980673789978f, -0.5399928092956543f, 0.1492166370153427f };
struct bp_state { float x[4], y[4]; };

static float bp_run(struct bp_state *s, float x)
{
    float n = BP_B[0] * x, d = BP_A[0] * s->y[0];
    n = st_fma(BP_B[1], s->x[0], n);
    n = st_fma(BP_B[3], s->x[2], n);
    n = st_fma(BP_B[4], s->x[3], n);
    for (int i = 1; i < 4; i++)
        d = st_fma(BP_A[i], s->y[i], d);
    float y = n - d;
    for (int i = 3; i > 0; i--) {
        s->x[i] = s->x[i - 1];
        s->y[i] = s->y[i - 1];
    }
    s->x[0] = x;
    s->y[0] = y;
    return y;
}

struct voice {
    MOD_LINE(100) line;
    struct bp_state bp;
    struct sos_state lp;
    struct lp3_state out;
};

struct state {
    float depth, rate;
    int n;
    struct sos_state in1, in2;
    struct voice l, r;
};

static float voice_run(struct voice *v, float x, float d)
{
    float y = bp_run(&v->bp, mod_line_run(&v->line, x, d));
    return lp3_run(&OUT, &v->out, sos_run_div(&LP, &v->lp, y));
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
        s->depth += dd;
        s->rate += dr;
        float period = 1.0f / s->rate;
        float m = st_sinf((s->rate * 6.2831854820251465f) * (float)s->n) * 34.5f;
        s->n++;
        float w = sos_run_div(&IN2, &s->in2, sos_run_div(&IN1, &s->in1, p[0]));
        if (period <= (float)s->n)
            s->n = 0;
        p[0] = voice_run(&s->l, w, st_fma(m + 34.5f, s->depth, 20.0f));
        p[1] = voice_run(&s->r, w, st_fma(34.5f - m, s->depth, 20.0f));
    }
}

const struct wvfx_desc mod_bbd_roto_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x109),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "BBD Roto",
    .category = "Vibrato",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
