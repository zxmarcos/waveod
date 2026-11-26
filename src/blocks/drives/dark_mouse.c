/*
 * Dark Mouse (DRIVE › Distortion): the original's Dark Mouse (QME-200 V1.2.2, library entry 3.15, id 0x0300002B) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample: a 3rd-order gain section (Gain, gainLine), a hard clip at +-9 on 4 points per sample with the
 * anti-aliasing FIR, a slew limiter on 4 points (each point at most 1.1338 from the one before, the first clipped at
 * +-1.1338) with the FIR, a fixed 3rd-order section, a rational clipper (0.9465 |x|^3 - 0.6001 x^2 + 0.3038 |x|) /
 * (|x|^3 - 0.1612 x^2 - 0.2258 |x| + 0.2663) with the sign of x, with the FIR, a fixed 2nd-order high pass, a
 * 1st-order tone section (Filter, volumeLine), x 4 Volume (volumeLine). Volume glides 0.005 per block (also while
 * bypassed); Gain and Filter are read as they are, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Filter", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct sos3 PRE1 = {
    1.5001044273376465f, -1.4641867876052856f, -1.497839093208313f, 1.4619230031967163f,
    2.133787155151367f, -3.6937804222106934f, 1.4574120044708252f, 0.10642558336257935f,
};
static const struct sos HPF = { 1.0f, -2.0f, 1.0f, 1.0f, -1.9959701299667358f, 0.9959782958030701f };
#define TONE_B 0.03435717895627022f
#define SLEW 1.1337867975234985f
/* the slew stage's FIR pair order */
static const uint8_t SLEW_FIR_ORDER[20] = { 3, 2, 1, 0, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };
static const struct clip_cub CLIP = {
    0.9465000033378601f, 0.6000999808311462f, 0.30379998683929443f, 0.2662999927997589f, -0.16120000183582306f,
    -0.22579999268054962f,
};

struct state {
    float gain, tone, volume;   /* Gain and Filter as read (tapers), Volume glided (taper) */
    struct sos3 pre2;
    struct fos tc;              /* the tone section, a0 in ta0 */
    float ta0;
    struct sos3_state p2, p1;
    struct sos_state hp;
    struct fos_state ts;
    struct stage st1, st2, st3;
};

/* the gain section from the gainLine value gl: m = max(0.9999 gl, 0.01), g2 = (0.9999 gl)^2 */
static void gain_set(struct sos3 *c, float gl)
{
    float g = gl * 0.9998999834060669f, m = g > 0.009999999776482582f ? g : 0.009999999776482582f;   /* VMAXNM */
    float g2 = g * g, u = m * 35.80437088012695f, v = m * 30.087549209594727f;
    float w = m * 0.04869312047958374f, z = m * 0.044758401811122894f;
    c->b0 = (u + 0.8924363255500793f) * m;
    c->b1 = (1.0102850198745728f - v) * m;
    c->b2 = (-0.6567397713661194f - u) * m;
    c->b3 = (v + -0.7745879888534546f) * m;
    c->a0 = (g2 + 0.0005630513187497854f) + w;
    c->a1 = st_fma(g2, -3.0f, 0.0006374035147018731f) - z;
    c->a2 = st_fma(g2, 3.0f, -0.0004143468977417797f) - w;
    c->a3 = (-0.0004886991227976978f - g2) + z;
}

static void tone_set(struct state *s, float t)
{
    s->tc = (struct fos){ TONE_B, TONE_B, 0.018357180058956146f - t };
    s->ta0 = t + 0.05035718157887459f;
}

/* x held at +-9 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip9(float x) { return __builtin_fminf(__builtin_fmaxf(x, -9.0f), 9.0f); }

/* one slew step: y may move at most SLEW from y0 */
static inline float slew(float y0, float x)
{
    /* |d| > SLEW by d's bits (positive floats order as their bits), the step's sign d's (at d = -0 |d| is 0) */
    float d = x - y0, lim = sign_of(SLEW, d) + y0;
    uint32_t over = (uint32_t)((int32_t)(st_word(SLEW) - (st_word(d) & 0x7fffffffu)) >> 31);
    return st_bits((st_word(lim) & over) | (st_word(x) & ~over));
}

/* the slew stage over a block in place: the oldest point held at +-SLEW (VMAXNM, VMINNM), each next one at most
   SLEW from the one before; then the FIR four samples at a time */
static void slew_stage_block(struct stage *s, float *y, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, *o = buf + 40 + 4 * i;
        o[0] = __builtin_fminf(__builtin_fmaxf(prev, -SLEW), SLEW);
        o[1] = slew(o[0], st_fma(d, 0.33333334f, prev));
        o[2] = slew(o[1], st_fma(d, 0.6666667f, prev));
        o[3] = slew(o[2], x);
        prev = x;
    }
    s->prev = prev;
    stage_block_fir_ord(s, buf, y, n, SLEW_FIR_ORDER);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_gainline, k[0]);
    s->tone = st_taper(st_volumeline, k[1]);
    s->volume = st_taper(st_volumeline, k[2]);
    gain_set(&s->pre2, s->gain);
    tone_set(s, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->volume = st_glide_by(s->volume, st_taper(st_volumeline, io->knobs[2]), 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float g = st_taper(st_gainline, io->knobs[0]), t = st_taper(st_volumeline, io->knobs[1]);
    if (s->gain != g) {
        s->gain = g;
        gain_set(&s->pre2, g);
    }
    if (s->tone != t) {
        s->tone = t;
        tone_set(s, t);
    }
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = sos3_run_div(&s->pre2, &s->p2, p[2 * i]);
    STAGE_BLOCK_POINTS(&s->st1, x, n, buf, clip9(v));
    stage_block_fir(&s->st1, buf, x, n);
    slew_stage_block(&s->st2, x, n);
    for (int i = 0; i < n; i++)
        x[i] = sos3_run_div_b1(&PRE1, &s->p1, x[i]);
    STAGE_BLOCK_POINTS(&s->st3, x, n, buf, clip_cub(&CLIP, v));
    stage_block_fir_ord(&s->st3, buf, x, n, DRV_FIR_ORDER_0321);
    for (int i = 0; i < n; i++) {
        float v = sos_run_b1(&HPF, &s->hp, x[i]);
        p[2 * i] = p[2 * i + 1] = (fos_run_div(&s->tc, s->ta0, &s->ts, v) * 4.0f) * s->volume;
    }
}

const struct wvfx_desc dark_mouse_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x10F),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dark Mouse",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
