/*
 * Timmy OD (DRIVE › OD): the original's Timmy OD (QME-200 V1.2.2, library entry 3.9, id 0x0300001E) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: a fixed 1st-order pre-filter, a 3rd-order gain section (Gain and Bass), a soft clipper on 4 points per
 * sample, s A (1 - e^(-k x)) with A and k each side picked by Mode, with the anti-aliasing FIR, a 1st-order treble
 * section (Treble), a 1st-order level section (Volume), three fixed 2nd-order sections, x 1.68. Gain (pow2_5), Bass
 * and Treble (pow0_5, all x 0.95) and Volume (x 0.0095) glide 0.0001 per sample, only while the block is on; the
 * sections follow (Gain/Bass and Treble a step short, as the original's do).
 *
 * The clipper by Mode: 0 gives s 1, A +-0.4, k 4; 1 gives s 1.2, A +-0.3, k 2; any other value mixes them (s 1.2,
 * A 0.4 above / -0.2 below, k 2 above / 4 below), as the original's table lookups do.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Mode", 0.0f, 1.0f, 1.0f, 1.0f, 4, 0x15, 0, 0, 0, 0 },
};

static const struct fos PF = { 0.9995272159576416f, -0.9995272159576416f, -0.9990543723106384f };
static const struct sos POST[3] = {
    { 0.6436691284179688f, -1.2393299341201782f, 0.5992311835289001f, 1.0f, -1.9345253705978394f,
      0.9390833973884583f },
    { 1.2393299341201782f, -0.8204746842384338f, 0.18848490715026855f, 1.0f, -0.48570579290390015f,
      0.08384410291910172f },
    { 1.2393299341201782f, 0.9662429094314575f, 0.6943762898445129f, 1.0f, 0.8884888887405396f,
      0.5887839198112488f },
};
#define TREBLE_B -0.022675739601254463f
#define LEVEL_A -0.9977350234985352f

struct state {
    float gain, bass, treble, level;    /* glided */
    struct sos3 gc;
    struct fos tc, lc;                  /* treble (a0 in ta0), level */
    float ta0;
    struct fos_state pf, ts, ls;
    struct sos3_state gs;
    struct sos_state post[3];
    struct stage st;
};

/* the gain section from Gain g and Bass b */
static void gain_set(struct sos3 *c, float g, float b)
{
    float g2 = g * g;
    c->b0 = st_fma(st_fma(g, -0.9905499219894409f, st_fma(g2, 0.9940094947814941f, -0.006751285865902901f)), b,
                   st_fma(g, 0.9965211153030396f, 0.0067903888411819935f) - g2);
    c->b1 = st_fma(st_fma(g2, -1.0500969886779785f, st_fma(g, 1.0471969842910767f, 0.006374320015311241f)), b,
                   st_fma(g2, 1.0444450378417969f, st_fma(g, -1.04156494140625f, -0.006336207035928965f)));
    c->b2 = st_fma(st_fma(g2, -0.8818355202674866f, st_fma(g, 0.8783758878707886f, 0.006381111219525337f)), b,
                   st_fma(g2, 0.8878260254859924f, st_fma(g, -0.8843473196029663f, -0.00641995994374156f)));
    c->b3 = st_fma(st_fma(g, -0.9350231289863586f, st_fma(g2, 0.937922477722168f, -0.00600414490327239f)), b,
                   st_fma(g2, -0.9322708249092102f, st_fma(g, 0.9293904900550842f, 0.0059662871062755585f)));
    c->a0 = st_fma(st_fma(g, -0.027054360136389732f, st_fma(g2, 0.030513999983668327f, -0.003571749897673726f)), b,
                   st_fma(g2, -0.03068397007882595f, st_fma(g, 0.027205070480704308f, 0.00359164597466588f)));
    c->a1 = st_fma(st_fma(g2, -0.08660099655389786f, st_fma(g, 0.08370157331228256f, 0.0031947840470820665f)), b,
                   st_fma(g2, 0.08676984906196594f, st_fma(g, -0.08388981968164444f, -0.0031758788973093033f)));
    c->a2 = st_fma(st_fma(g, -0.08511964231729507f, st_fma(g2, 0.08166000247001648f, 0.0032015759497880936f)), b,
                   st_fma(g2, -0.08149003237485886f, st_fma(g, 0.0849686786532402f, -0.0032212170772254467f)));
    c->a3 = st_fma(st_fma(b, -0.0255730003118515f, 0.025404149666428566f), g2,
                   st_fma(st_fma(b, 0.028472429141402245f, -0.02828443981707096f), g,
                          st_fma(b, -0.002824610099196434f, 0.0028059598989784718f)));
}

static void treble_set(struct state *s, float t)
{
    s->tc = (struct fos){ TREBLE_B, TREBLE_B, 1.007323980331421f - t };
    s->ta0 = t + -1.0526759624481201f;
}

/* the clipper's constants by Mode */
struct clip_k { float s, Ap, An, kp, kn; };

static struct clip_k clip_k(float mode)
{
    if (mode == 0.0f)
        return (struct clip_k){ 1.0f, 0.4000000059604645f, -0.4000000059604645f, -4.0f, 4.0f };
    int one = mode == 1.0f;
    return (struct clip_k){ 1.2000000476837158f, one ? 0.30000001192092896f : 0.4000000059604645f,
                            one ? -0.30000001192092896f : -0.20000000298023224f, -2.0f, one ? 2.0f : 4.0f };
}

/* m - m e, e = e^(kp x) above zero (kp < 0), e^(kn x) below: e^(-k |x|) with k = -kp or kn (amp.h, exp_negabs),
   the side by x's sign bit (at +0 e = 1 and both sides give 0) */
static inline float clip(const struct clip_k *c, float x)
{
    float e = exp_negabs(x, pick_sign(x, -c->kp, c->kn));
    float m = c->s * pick_sign(x, c->Ap, c->An);
    return st_fma(-e, m, m);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2_5, k[0]) * 0.949999988079071f;
    s->level = k[1] * 0.009499999694526196f;
    s->bass = st_taper(st_pow0_5, k[2]) * 0.949999988079071f;
    s->treble = st_taper(st_pow0_5, k[3]) * 0.949999988079071f;
    gain_set(&s->gc, s->gain, s->bass);
    treble_set(s, s->treble);
    s->lc = (struct fos){ k[1] * 0.018978482112288475f, k[1] * -0.018978482112288475f, LEVEL_A };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    if (io->frames == 0 || !io->enabled)
        return;
    float lt = io->knobs[1] * 0.009499999694526196f;
    float bt = st_taper(st_pow0_5, io->knobs[2]) * 0.949999988079071f;
    float tt = st_taper(st_pow0_5, io->knobs[3]) * 0.949999988079071f;
    float gt = st_taper(st_pow2_5, io->knobs[0]) * 0.949999988079071f;
    struct clip_k ck = clip_k(io->knobs[4]);
    /* stage by stage over the block: Gain's and Bass's glides before the stage, Treble's and Level's after it (each
       once per sample, its section following it); the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gt);
        s->bass = st_glide(s->bass, bt);
        if (s->bass != bt || s->gain != gt)
            gain_set(&s->gc, s->gain, s->bass);
        x[i] = sos3_run_div(&s->gc, &s->gs, fos_run(&PF, &s->pf, p[2 * i]));
    }
    STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip(&ck, v));
    stage_block_fir(&s->st, buf, x, n);
    for (int i = 0; i < n; i++) {
        s->treble = st_glide(s->treble, tt);
        if (s->treble != tt)
            treble_set(s, s->treble);
        if (s->level != lt) {
            s->level = st_glide(s->level, lt);
            s->lc = (struct fos){ s->level * 1.9977350234985352f, s->level * -1.9977350234985352f, LEVEL_A };
        }
        x[i] = fos_run(&s->lc, &s->ls, fos_run_b1_div(&s->tc, s->ta0, &s->ts, x[i]));
    }
    for (int k = 0; k < 3; k++)
        sos_block(&POST[k], &s->post[k], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 1.6800000667572021f;
}

const struct wvfx_desc timmy_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x109),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Timmy OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
