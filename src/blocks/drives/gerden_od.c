/*
 * Gerden OD (DRIVE › OD): the original's Gerden OD (QME-200 V1.2.2, library entry 3.8, id 0x03000010) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: a 2nd-order gain section on the input (Gain and Voice), a soft clipper on 4 points per sample (1 - e^-x
 * above zero, 1.6 (e^(0.8 x) - 1) below), the anti-aliasing FIR summed onto 1.5 x the input (the clean part), a
 * 1st-order tone section (Tone), x 0.525 Volume. Volume glides 0.005 per block (also while bypassed); Gain, Tone and
 * Voice are read as they are, only while the block is on. The original's Init reads Voice through the pow1_5 table,
 * its Process through pow0_5, so the first block on recomputes the gain section.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 30.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Voice", 0.0f, 100.0f, 1.0f, 60.0f, 3, 0x15, 0, 0, 0, 0 },
};

#define TONE_B -0.06871435046195984f

struct state {
    float drive, tone, voice, level;    /* Gain and Tone as read (0-1), Voice as read (taper), Volume glided */
    struct sos gain;
    struct fos tc;                      /* the tone section, a0 in ta0 */
    float ta0;
    struct sos_state gs;
    struct fos_state ts;
    struct stage st;
};

/* the gain section from the raw Gain t (0-100) and the Voice taper v */
static void gain_set(struct sos *c, float t, float v)
{
    float dd = t * 0.01f, d2 = dd * dd;
    float a = d2 * -0.08032640814781189f * v;
    float b = st_fma(v, st_fma(t, 0.0006211180007085204f, 0.01841169036924839f), a);
    float e = t * 0.000985410064458847f * v;
    float f = v * -0.01809038035571575f;
    float g = st_fma(d2, 0.16065280139446259f, st_fma(t, -0.0016065279487520456f, -0.0003213055897504091f));
    c->b0 = st_fma(t, -0.009797574020922184f, b + -0.02228306047618389f) + d2;
    c->b1 = st_fma(v, g, st_fma(d2, -0.1767181009054184f, st_fma(t, 0.0017713109264150262f,
                                                                  -6.041945016477257e-05f)));
    c->b2 = st_fma(d2, -0.8232818841934204f, st_fma(t, 0.008034523576498032f, (e + a) + 0.021515769883990288f)) + f;
    c->a0 = st_fma(d2, 0.08926977962255478f, st_fma(t, -0.0006902719615027308f, b + -0.02046160027384758f));
    c->a1 = c->b1;
    c->a2 = st_fma(d2, 0.08744832128286362f, st_fma(t, -0.0010727789485827088f, (f + a) + 0.019694309681653976f)) + e;
}

static void tone_set(struct state *s, float tt)
{
    s->tc = (struct fos){ TONE_B, TONE_B, 1.1312860250473022f - tt };
    s->ta0 = tt + -1.2687139511108398f;
}

/* 1 - e^-x above zero, 1.6 e^(0.8 x) - 1.6 below, by x's sign bit, not a compare (at +0 both exponents are 0: e = 1,
   both sides 0); amp.h exp_np */
static inline float clip(float x)
{
    float e = exp_np(pick_sign(x, -x, x * 0.800000011920929f));
    return pick_sign(x, 1.0f - e, st_fma(e, 1.600000023841858f, -1.600000023841858f));
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->drive = k[0] * 0.01f;
    s->tone = k[1] * 0.01f;
    s->level = k[2] * 0.01f;
    s->voice = st_taper(st_pow1_5, k[3]);
    gain_set(&s->gain, k[0], s->voice);
    tone_set(s, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->level = st_glide_by(s->level, io->knobs[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float dd = io->knobs[0] * 0.01f, tt = io->knobs[1] * 0.01f, v = st_taper(st_pow0_5, io->knobs[3]);
    if (s->drive != dd || s->voice != v) {
        s->drive = dd;
        s->voice = v;
        gain_set(&s->gain, io->knobs[0], v);
    }
    if (s->tone != tt) {
        s->tone = tt;
        tone_set(s, tt);
    }
    float level = s->level * 0.5249999761581421f;
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        x[i] = p[2 * i] * 1.5f;                 /* the FIR is summed onto the input x 1.5 */
        y[i] = p[2 * i];
    }
    sos_block_div(&s->gain, &s->gs, y, n);
    STAGE_BLOCK_POINTS(&s->st, y, n, buf, clip(v));
    stage_block_fir_acc(&s->st, buf, y, n, AMP_FIR_ORDER, x);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = level * fos_run_b1_div(&s->tc, s->ta0, &s->ts, y[i]);
}

const struct wvfx_desc gerden_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x108),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Gerden OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
