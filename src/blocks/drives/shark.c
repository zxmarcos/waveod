/*
 * Shark (DRIVE › Distortion): the original's Shark (QME-200 V1.2.2, library entry 3.19, id 0x03000030) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision except one gain coefficient.
 *
 * Per sample: two 2nd-order gain sections (Gain), a rational soft clipper on 4 points per sample, (5.705 x^2 + 5.959
 * |x| + 2.084 |x|^3) / (4.923 x^2 + 9.76 + |x|^3 - 2.031 |x|) with the sign of x, the anti-aliasing FIR, three fixed
 * 2nd-order sections, a 2nd-order tone section (Tone), x 1.4 Volume. Volume (A25) glides 0.0001 per sample, also
 * while bypassed; Gain and Tone are read as they are, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct sos CUT = {
    0.9840999841690063f, -1.9105000495910645f, 0.9296000003814697f, 1.0f, -1.9105000495910645f, 0.9136000275611877f,
};
static const struct sos CUT2 = {
    0.986299991607666f, -1.8543000221252441f, 0.8801000118255615f, 1.0f, -1.8543000221252441f, 0.8664000034332275f,
};
static const struct sos SHEL = {
    1.4277000427246094f, -1.6871000528335571f, 0.614300012588501f, 1.0f, -0.9990000128746033f, 0.3538999855518341f,
};
#define TONE_B0 0.009777978993952274f
#define TONE_B1 0.0019324298482388258f
#define TONE_B2 -0.007845548912882805f
#define PF2_B 1.0183299779891968f

struct state {
    float gain, tone, volume;   /* Gain x 0.008, 1 - Tone, Volume glided (taper) */
    struct sos pf1, pf2, tc;
    struct sos_state s1, s2, cut, cut2, shel, ts;
    struct stage st;
};

/* from the raw Gain G (0-100) and g = 0.008 G */
static void gain_set(struct state *s, float G, float g)
{
    s->pf1.b0 = st_fma(G, 0.870576024055481f, 1.1337867975234985f);
    s->pf1.b1 = (float)st_fmad((double)g, -1.901980004, (double)0.11113378405570984f);
    s->pf1.b2 = st_fma(G, -0.8553601503372192f, -1.0226531028747559f);
    s->pf1.a0 = g + 1.1337867975234985f;
    s->pf1.a1 = st_fma(G, -0.015215841121971607f, 0.11113378405570984f);
    s->pf1.a2 = st_fma(G, 0.0072158402763307095f, -1.0226531028747559f);
    s->pf2 = (struct sos){ PF2_B, 0.0f, -PF2_B, g + -1.1011338233947754f,
                           st_fma(G, -0.014370673336088657f, 1.9757364988327026f),
                           st_fma(G, 0.00637067249044776f, -0.8750645518302917f) };
}

/* from the raw Tone t and tt = 1 - 0.01 t */
static void tone_set(struct state *s, float t, float tt)
{
    s->tc = (struct sos){ TONE_B0, TONE_B1, TONE_B2, 1.0385264158248901f - t * 0.01f,
                          st_fma(tt, -1.896031379699707f, -0.01702788472175598f),
                          st_fma(tt, 0.8960314393043518f, -0.017633669078350067f) };
}

/* the second gain section: no x1 term, the numerator summed from b2 x2 */
static inline float pf2_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b0, x, c->b2 * s->x2);
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, (n - d) / c->a0);
}

static const struct clip_rat CLIP = {
    5.704999923706055f, 5.959000110626221f, 2.0840001106262207f, 4.922999858856201f, 9.760000228881836f,
    -2.0309998989105225f,
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = k[0] * 0.00800000037997961f;
    s->tone = 1.0f - k[1] * 0.01f;
    s->volume = st_taper(st_a25, k[2]);
    gain_set(s, k[0], s->gain);
    tone_set(s, k[1], s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float g = io->knobs[0] * 0.00800000037997961f, tt = 1.0f - io->knobs[1] * 0.01f;
    float target = st_taper(st_a25, io->knobs[2]);
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            s->volume = st_glide(s->volume, target);
        return;
    }
    /* stage by stage over the block (Gain and Tone, not glided, before the first sample; Volume's glide once per
       sample): the same samples as sample by sample */
    if (s->gain != g) {
        s->gain = g;
        gain_set(s, io->knobs[0], g);
    }
    if (s->tone != tt) {
        s->tone = tt;
        tone_set(s, io->knobs[1], tt);
    }
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, target);
        vol[i] = s->volume;
        x[i] = pf2_run(&s->pf2, &s->s2, sos_run_div(&s->pf1, &s->s1, p[2 * i]));
    }
    STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip_rat(&CLIP, v));
    stage_block_fir_ord(&s->st, buf, x, n, AMP_FIR_ORDER2);
    sos_block(&CUT, &s->cut, x, n);
    sos_block(&CUT2, &s->cut2, x, n);
    sos_block(&SHEL, &s->shel, x, n);
    sos_block_div(&s->tc, &s->ts, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = (vol[i] * 1.399999976158142f) * x[i];
}

const struct wvfx_desc shark_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x113),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Shark",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
