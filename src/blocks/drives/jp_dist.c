/*
 * JP Dist (DRIVE › Distortion): the original's JP Dist (QME-200 V1.2.2, library entry 3.14, id 0x0300002A) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample: a fixed 1st-order pre-filter (x 58), a hard clip at +-4.25 on 4 points per sample with the
 * anti-aliasing FIR, a fixed 1st-order section, a 2nd-order gain section (Gain), a hard clip at +-2.5 with the FIR,
 * a fixed 2nd-order section, a piecewise clipper (0.987 |x| up to 0.5753, then quadratic in x, then 0.7051 + 0.0323
 * |x| beyond 1.5385, odd) with the FIR, a 2nd-order tone section (Tone), x 3 Volume. Volume glides 0.005 per block
 * (also while bypassed); Gain and Tone are read as they are, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct fos PRE1 = { -58.42430114746094f, 58.42430114746094f, -0.906000018119812f };
static const struct fos POST1 = { 0.9983000159263611f, -0.9983000159263611f, -0.9966999888420105f };
static const struct sos POST2 = {
    0.28220000863075256f, 0.03739999979734421f, -0.24490000307559967f, 1.0f, -0.9729999899864197f,
    0.07180000096559525f,
};

struct state {
    float gain, tone, volume;   /* Gain x 0.01 and Tone x 0.0075 as read, Volume glided (0-1) */
    struct sos pre2, tc;
    struct fos_state p1, q1;
    struct sos_state p2, q2, ts;
    struct stage st1, st2, st3;
};

/* the gain section from the raw Gain G (0-100) */
static void gain_set(struct sos *c, float G)
{
    float gg = G * 0.01f, g2 = gg * gg;
    float a1 = -0.0005470095784403384f - st_fma(G, -0.02094000019133091f, g2 + g2);
    *c = (struct sos){ st_fma(st_fma(G, -0.010472412221133709f, g2), 1.5f, -1.781022548675537f), a1 * 1.5f,
                       st_fma(st_fma(G, -0.010467587038874626f, g2), 1.5f, 1.7802019119262695f),
                       st_fma(G, 0.0008654561243019998f, -1.1873483657836914f) + g2, a1,
                       st_fma(G, -0.021805455908179283f, 1.1868013143539429f) + g2 };
}

/* the tone section from the raw Tone t */
static void tone_set(struct sos *c, float t)
{
    *c = (struct sos){ st_fma(t, 0.004749460611492395f, 0.019930938258767128f),
                       st_fma(t, -0.00957467220723629f, 0.002158943796530366f),
                       st_fma(t, 0.0048010447062551975f, -0.017771994695067406f), 1.0f, -1.8250603675842285f,
                       0.830473780632019f };
}

/* x held at +-4.25, +-2.5 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip425(float x) { return __builtin_fminf(__builtin_fmaxf(x, -4.25f), 4.25f); }
static inline float clip25(float x) { return __builtin_fminf(__builtin_fmaxf(x, -2.5f), 2.5f); }

static inline float clip_poly(float x)
{
    float a = st_abs(x), sg = x < 0.0f ? -1.0f : 1.0f;
    if (a <= 0.5752999782562256f)
        return a * 0.9869999885559082f * sg;
    if (a <= 1.5384999513626099f) {
        float x2 = x * x;
        return st_fma(st_fma(x2, 0.40619999170303345f, 1.9707000255584717f), a,
                      st_fma(x2, -1.5286999940872192f, -0.13770000636577606f)) * sg;
    }
    return st_fma(a, 0.03229999914765358f, 0.7050999999046326f) * sg;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = k[0] * 0.01f;
    s->tone = k[1] * 0.007499999832361937f;
    s->volume = k[2] * 0.01f;
    gain_set(&s->pre2, k[0]);
    tone_set(&s->tc, k[1]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->volume = st_glide_by(s->volume, io->knobs[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float gg = io->knobs[0] * 0.01f, tt = io->knobs[1] * 0.007499999832361937f;
    if (s->gain != gg) {
        s->gain = gg;
        gain_set(&s->pre2, io->knobs[0]);
    }
    if (s->tone != tt) {
        s->tone = tt;
        tone_set(&s->tc, io->knobs[1]);
    }
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = fos_run(&PRE1, &s->p1, p[2 * i]);
    STAGE_BLOCK_POINTS(&s->st1, x, n, buf, clip425(v));
    stage_block_fir(&s->st1, buf, x, n);
    for (int i = 0; i < n; i++)
        x[i] = sos_run_div(&s->pre2, &s->p2, fos_run(&POST1, &s->q1, x[i]));
    STAGE_BLOCK_POINTS(&s->st2, x, n, buf, clip25(v));
    stage_block_fir(&s->st2, buf, x, n);
    sos_block(&POST2, &s->q2, x, n);
    STAGE_BLOCK_POINTS(&s->st3, x, n, buf, clip_poly(v));
    stage_block_fir(&s->st3, buf, x, n);
    sos_block(&s->tc, &s->ts, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = (x[i] * 3.0f) * s->volume;
}

const struct wvfx_desc jp_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x10E),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "JP Dist",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
