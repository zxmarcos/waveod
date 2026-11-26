/*
 * Breaker OD (DRIVE › OD): the original's Breaker OD (QME-200 V1.2.2, library entry 3.7, id 0x0300000E) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample: a fixed 1st-order pre-filter, a 3rd-order gain section (Gain), a fixed 1st-order section, x 6, a soft
 * clipper on 4 points per sample (0.08 - 0.08 e^(-8 x) above zero, 0.08 e^(8 x) - 0.08 below) with the anti-aliasing
 * FIR, a 2nd-order tone section (Tone, pow1_5), two fixed 2nd-order sections, x 100 Volume. Gain, Tone and Volume
 * glide 0.0001 per sample, only while the block is on; the gain and tone sections follow their glides (and stop a
 * step short, as the original's do).
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 60.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct fos PF = { 0.9988675117492676f, -0.9988675117492676f, -0.9977350234985352f };
static const struct fos NL = { -0.6522172093391418f, 0.6522172093391418f, -0.9775785207748413f };
static const struct sos POST1 = {
    0.5993964076042175f, -0.9820907115936279f, 0.4022256135940552f, 1.0f, -1.4831558465957642f, 0.5697116851806641f,
};
static const struct sos POST2 = {
    0.9820907115936279f, 0.1655450016260147f, 0.0007267000037245452f, 1.0f, 0.6276671886444092f,
    0.17870590090751648f,
};

struct state {
    float drive, tone, level;   /* glided: Gain and Volume 0-1, Tone the taper */
    struct sos3 gain;
    struct sos tc;
    struct fos_state pf, nl;
    struct sos3_state gs;
    struct sos_state ts, p1, p2;
    struct stage st;
};

/* the gain section from the glided Gain (the Init computes it from the raw knob, its own factors) */
static void gain_set(struct sos3 *c, float d)
{
    *c = (struct sos3){ st_fma(d, 71.63265228271484f, 2.4123120307922363f),
                        st_fma(d, -55.009220123291016f, 0.21231360733509064f),
                        st_fma(d, -69.64383697509766f, -2.014683961868286f),
                        st_fma(d, 53.020408630371094f, 0.18531489372253418f),
                        d + 2.4123120307922363f,
                        st_fma(d, -1.9119880199432373f, 0.21231360733509064f),
                        st_fma(d, 0.9888079166412354f, -2.014683961868286f),
                        st_fma(d, -0.07682041823863983f, 0.18531489372253418f) };
}

static void tone_set(struct sos *c, float t)
{
    float u = t * 0.044843051582574844f, t2 = t * t;
    *c = (struct sos){ -0.002033697906881571f - u, -0.0040673972107470036f, u + -0.002033697906881571f,
                       st_fma(t, -0.9946485161781311f, -0.39105159044265747f) + t2,
                       st_fma(t2, -1.977578043937683f, st_fma(t, 2.0576980113983154f, 0.5540158748626709f)),
                       st_fma(t2, 0.9775785207748413f, st_fma(t, -1.0610159635543823f, -0.1737672984600067f)) };
}

static inline float clip8(float x, float e)
{
    /* by x's sign bit, not a compare (amp.h, pick_sign: at +0 e = 1 and both sides give 0) */
    return pick_sign(x, st_fma(-e, 0.07999999821186066f, 0.07999999821186066f),
                     st_fma(e, 0.07999999821186066f, -0.07999999821186066f));
}

/* the stage over a block in place, x the section's output: the points of 6 x through clip8 with e = e^(-8 |q|), the
   current one with e^(-48 |x|) from x itself (amp.h, exp_negabs; no point is the last one's again); then the FIR
   four samples at a time */
static void stage_block(struct stage *s, float *x, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float c = x[i] * 6.0f, d = c - prev, *o = buf + 40 + 4 * i;
        float q1 = st_fma(d, 0.6666667f, prev), q2 = st_fma(d, 0.33333334f, prev);
        o[0] = clip8(prev, exp_negabs(prev, 8.0f));
        o[1] = clip8(q2, exp_negabs(q2, 8.0f));
        o[2] = clip8(q1, exp_negabs(q1, 8.0f));
        o[3] = clip8(c, exp_negabs(x[i], 48.0f));
        prev = c;
    }
    s->prev = prev;
    stage_block_fir(s, buf, x, n);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    float G = k[0];
    s->drive = G * 0.01f;
    s->tone = st_taper(st_pow1_5, k[1]);
    s->level = k[2] * 0.01f;
    s->gain = (struct sos3){ st_fma(G, 0.7163265347480774f, 2.4123120307922363f),
                             st_fma(G, -0.5500921607017517f, 0.21231360733509064f),
                             st_fma(G, -0.6964383721351624f, -2.014683961868286f),
                             st_fma(G, 0.5302040576934814f, 0.18531489372253418f),
                             s->drive + 2.4123120307922363f,
                             st_fma(G, -0.019119879230856895f, 0.21231360733509064f),
                             st_fma(G, 0.009888079017400742f, -2.014683961868286f),
                             st_fma(G, -0.0007682041614316404f, 0.18531489372253418f) };
    tone_set(&s->tc, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    if (!io->enabled)
        return;
    float dt = io->knobs[0] * 0.01f, tt = st_taper(st_pow1_5, io->knobs[1]), lt = io->knobs[2] * 0.01f;
    /* stage by stage over the block: Drive's glide before the stage, Tone's and Level's after it (each once per
       sample, its section following it); the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], lev[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        s->drive = st_glide(s->drive, dt);
        if (s->drive != dt)
            gain_set(&s->gain, s->drive);
        float v = fos_run(&PF, &s->pf, p[2 * i]);
        x[i] = fos_run(&NL, &s->nl, sos3_run_div(&s->gain, &s->gs, v));
    }
    stage_block(&s->st, x, n);
    for (int i = 0; i < n; i++) {
        s->tone = st_glide(s->tone, tt);
        s->level = st_glide(s->level, lt);
        if (s->tone != tt)
            tone_set(&s->tc, s->tone);
        lev[i] = s->level;
        x[i] = sos_run_div_b1(&s->tc, &s->ts, x[i]);
    }
    sos_block(&POST1, &s->p1, x, n);
    sos_block(&POST2, &s->p2, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = (lev[i] * 100.0f) * x[i];
}

const struct wvfx_desc breaker_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x107),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Breaker OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
