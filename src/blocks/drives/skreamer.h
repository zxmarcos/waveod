/*
 * The original's Skreamer and Skreamer 9 (docs/stock_port.md): the same code in two modules, so one path here and a
 * file per model. Mono: the left input, the result on both outputs. Single precision throughout.
 *
 * Per sample: two fixed pre-filters (2nd and 1st order), a 2nd-order gain section (Gain, pow2_5), a soft clipper on
 * 4 points per sample (0.012 x + 0.988 A (1 - e^(-k |x|)), A and k each side following Gain), the anti-aliasing FIR
 * summed onto half the pre-filtered signal (the clean part), a 2nd-order tone section (Tone), x 1.65 Volume, two
 * fixed post filters and a fixed low pass. Volume glides 0.005 per block (also while bypassed); Gain and Tone are
 * read as they are, only while the block is on.
 */
#pragma once
#include "../amps/amp.h"

#define SKREAMER_PARAMS(gain, tone) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), 0, 0x15, 0, 0, 0, 0 }, \
    { "Tone", 0.0f, 100.0f, 1.0f, (tone), 1, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 }, \
}

static const struct sos SKR_PF1 = {
    0.9845619201660156f, -1.7552024126052856f, 0.7709872126579285f, 1.0f, -1.780913233757019f, 0.7818086743354797f,
};
static const struct fos SKR_PF2 = { 0.9988675117492676f, -0.9988675117492676f, -0.9977350234985352f };
static const struct sos SKR_POST1 = {
    0.541398286819458f, 0.22682030498981476f, 0.04633849859237671f, 1.0f, 0.8085755109786987f, 0.38884231448173523f,
};
static const struct sos SKR_POST2 = {
    3.6347339153289795f, 1.8749810457229614f, 0.21243919432163239f, 1.0f, 0.9645239114761353f, 0.12377160042524338f,
};
static const struct sos SKR_LOWPASS = {
    0.6388455629348755f, 1.277691125869751f, 0.6388455629348755f, 1.6793991327285767f, 0.555382251739502f,
    0.32060086727142334f,
};
/* the FIR's pair order */
static const uint8_t SKR_FIR_ORDER[20] = { 0, 3, 2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

struct skreamer {
    float drive, tone, level;   /* Gain as read (taper), Tone as read (0-1), Volume glided (0-1) */
    struct sos gain, tc;        /* the gain and tone sections */
    struct sos_state pf1, gs, ts, post1, post2, lp;
    struct fos_state pf2;
    struct stage st;
};

static inline void skr_gain_set(struct sos *c, float d)
{
    c->b0 = st_fma(d, 45.99103927612305f, 5.135708808898926f);
    c->b1 = st_fma(d, -1.902359962463379f, -0.15062770247459412f);
    c->b2 = st_fma(d, -44.088680267333984f, -4.898254871368408f);
    c->a0 = d + 0.5466222763061523f;
    c->a1 = c->b1;
    c->a2 = st_fma(d, 0.9023599028587341f, -0.30916860699653625f);
}

/* from the raw knob t (0-100) and tt = t x 0.01 */
static inline void skr_tone_set(struct sos *c, float t, float tt)
{
    float t2 = tt * tt, u = t * 0.0005120951682329178f;
    c->b0 = st_fma(t2, 0.048770971596241f, -0.0006621531792916358f - u);
    c->b1 = -0.0002513448998797685f;
    c->b2 = st_fma(t2, -0.048770971596241f, u + 0.0004108081921003759f);
    c->a0 = st_fma(t, -0.009975614957511425f, -0.016015339642763138f) + t2;
    c->a1 = st_fma(tt - t2, 1.8927040100097656f, 0.020543260499835014f);
    c->a2 = st_fma(t2, 0.8927038908004761f, st_fma(t, -0.008951423689723015f, -0.005080884788185358f));
}

/* the clipper: k x through e^, A above zero, B below */
/* kp = -kn: e^(-kn |x|) either side (amp.h, exp_negabs); A or B by x's sign bit (at +0 e = 1 and both give 0) */
static inline float skr_clip(float x, float kn, float A, float B)
{
    float e = exp_negabs(x, kn);
    return st_fma(pick_sign(x, A, B) * 0.9879999756813049f, 1.0f - e, x * 0.012000024318695068f);
}

static inline void skr_init(struct skreamer *s, const float *k)
{
    *s = (struct skreamer){ 0 };
    s->drive = st_taper(st_pow2_5, k[0]);
    s->tone = k[1] * 0.01f;
    s->level = k[2] * 0.01f;
    skr_gain_set(&s->gain, s->drive);
    skr_tone_set(&s->tc, k[1], s->tone);
}

static inline void skr_process(struct skreamer *s, const struct wvfx_io *io)
{
    s->level = st_glide_by(s->level, io->knobs[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float d = st_taper(st_pow2_5, io->knobs[0]), tt = io->knobs[1] * 0.01f;
    if (s->drive != d) {
        s->drive = d;
        skr_gain_set(&s->gain, d);
    }
    if (s->tone != tt) {
        s->tone = tt;
        skr_tone_set(&s->tc, io->knobs[1], tt);
    }
    float level = s->level * 1.649999976158142f;
    float kn = st_fma(d, 0.5f, 2.5f);           /* kp = -(0.5 d + 2.5) = -kn (VFNMA) */
    float A = st_fma(d, -0.12999999523162842f, 0.41999998688697815f);
    float B = st_fma(d, 0.12999999523162842f, -0.41999998688697815f);       /* VFNMA */
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&SKR_PF1, &s->pf1, x, n);
    for (int i = 0; i < n; i++) {
        x[i] = fos_run(&SKR_PF2, &s->pf2, x[i]);
        y[i] = x[i];
    }
    sos_block_div(&s->gain, &s->gs, y, n);
    STAGE_BLOCK_POINTS(&s->st, y, n, buf, skr_clip(v, kn, A, B));
    for (int i = 0; i < n; i++)
        x[i] = x[i] * 0.5f;                     /* the FIR is summed onto half the clean signal */
    stage_block_fir_acc(&s->st, buf, y, n, SKR_FIR_ORDER, x);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div_b1(&s->tc, &s->ts, y[i]) * level;
    sos_block(&SKR_POST1, &s->post1, y, n);
    sos_block(&SKR_POST2, &s->post2, y, n);
    sos_block_div(&SKR_LOWPASS, &s->lp, y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i];
}

#define SKREAMER_BLOCK(sym, label, n, gain, tone) \
    static const struct wvfx_param params[] = SKREAMER_PARAMS(gain, tone); \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    { \
        (void)buffer; (void)env; \
        skr_init(state, k); \
        return 0; \
    } \
    static void process(void *state, const struct wvfx_io *io) { skr_process(state, io); } \
    const struct wvfx_desc sym = { \
        .magic = WVFX_MAGIC, \
        .abi = WVFX_ABI, \
        .id = WVFX_ID(WVFX_DRV, n), \
        .menu = WVFX_DRV, \
        .nparams = sizeof params / sizeof params[0], \
        .name = label, \
        .category = "OD", \
        .params = params, \
        .state_size = sizeof(struct skreamer), \
        .buffer_size = 0, \
        .init = init, \
        .process = process, \
        .poll = 0, \
    }
