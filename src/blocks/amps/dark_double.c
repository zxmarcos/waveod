/*
 * Dark Double (AMP › Dark): the original's Dark Double amp (QME-200 V1.2.2, library entry 4.3, id 0x07000004) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h has the pieces):
 *   pre-filter    x 1.1, three fixed 2nd-order sections
 *   tone stack    3rd order from Bass (pow2), Middle and Treble (linear)
 *   gain          Bright off: x Gain (pow1_5); on: a 1st-order section from Gain (divided by its a0)
 *   pre amp       two fixed 2nd-order sections
 *   stage         the envelope-driven clip 0.15 - 0.15 e^(-k |x|), k = 0.5 + 3.5 env, x max(1 / (1 + 3 k), 0.13),
 *                 on 4 points per sample, the FIR
 *   output        x Volume x 45.56
 * All five knobs glide 0.0001 per sample; the gain and tone coefficients stop one glide step short of them. Init
 * computes the tone stack from the knobs before their x 0.01 (the factors x 0.01 instead), a rounding apart from
 * Process's until a tone knob moves. The Bright section's state holds while Bright is off.
 */
#include "amp.h"

enum { K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE, K_BRIGHT };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }
#define KNOBD(name, k, d) { name, 0.0f, 100.0f, 1.0f, d, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOBD("Gain", K_GAIN, 35.0f), KNOB("Volume", K_VOLUME), KNOB("Bass", K_BASS), KNOBD("Middle", K_MIDDLE, 40.0f),
    KNOBD("Treble", K_TREBLE, 60.0f),
    { "Bright", 0.0f, 1.0f, 1.0f, 1.0f, K_BRIGHT, 0, 0, 0, "Off\0On\0", 0 },
};

static const struct sos PF[3] = {
    { 2.537264823913574f, -4.772617340087891f, 2.2356116771698f, 1.0f, -1.8807189464569092f, 0.8809137940406799f },
    { 4.772617340087891f, 0.9470009803771973f, 0.0023503999691456556f, 1.0f, 0.10583219677209854f,
      0.00035149999894201756f },
    { 4.772617340087891f, 3.816075563430786f, 0.0003141000051982701f, 1.0f, 0.7919657230377197f,
      0.000381499994546175f },
};

static const struct sos PREAMP[2] = {
    { 0.7428470849990845f, -1.3755500316619873f, 0.6356393098831177f, 1.0f, -1.9585975408554077f,
      0.9602729082107544f },
    { 1.3755500316619873f, -1.3339767456054688f, 0.053095098584890366f, 1.0f, -0.922248899936676f,
      0.08987770229578018f },
};

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    struct tone3 tone;
    struct fos bright_c;
    float bright_a0;
    struct sos_state pf[3], pre[2];
    struct fos_state bs;
    struct env_state env;
    struct stage st;
};

/*
 * The tone stack from Bass b, Middle m and Treble t (tb = t b, mb = m b):
 *   n0 = t 0.00142 + b 0.00427 + (m 0.000224 + mb 0.158) + tb
 *   n1 = (mb -0.438 + b -0.00339) - (m 0.000173 + t 0.00142 + 3 tb)
 *   n2 = mb 0.402 - (b 0.00427 + m 0.000224 + t 0.00142) + 3 tb
 *   n3 = (m 0.000173 + mb -0.122) + b 0.00339 + t 0.00142 - tb
 *   d0 = b 1.186 + 0.00168 + (m 0.000224 + mb 0.158)     d1 = b -3.185 - 0.00116 - m 0.000173 + mb -0.438
 *   d2 = b 2.814 - 0.00168 - m 0.000224 + mb 0.402       d3 = (m 0.000173 + mb -0.122) + 0.00116 + b -0.815
 * Process sums them as below; Init (tone_set_init) takes Middle and Treble before their x 0.01 and sums some terms
 * in another order.
 */
static void tone_set(struct tone3 *o, float b, float m, float t)
{
    float tb = t * b, mb = m * b, tb3 = tb * 3.0f;
    float m1 = m * 0.0002243566996185109f, m2 = m * 0.00017280649626627564f;
    float t1 = t * 0.0014195339754223824f, t2 = t * 0.0014173459494486451f;
    float bk = b * 0.004269454162567854f, mb1 = mb * -0.4381859004497528f, mb2 = mb * 0.4018141031265259f;
    float mm0 = st_fma(mb, 0.1581858992576599f, m1);       /* the m terms of n0 and d0 */
    float mm3 = st_fma(mb, -0.12181410193443298f, m2);     /* the m terms of n3 and d3 */
    o->b[0] = ((t1 + bk) + mm0) + tb;
    o->b[1] = st_fma(b, -0.0033920500427484512f, mb1) - ((m2 + t2) + tb3);
    o->b[2] = (mb2 - ((bk + m1) + t1)) + tb3;
    o->b[3] = (st_fma(b, 0.0033920500427484512f, mm3) + t2) - tb;
    o->a[0] = st_fma(b, 1.185675024986267f, 0.0016770459478721023f) + mm0;
    o->a[1] = (st_fma(b, -3.184798002243042f, -0.0011594370007514954f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.8143250942230225f, -0.0016762519953772426f) - m1) + mb2;
    o->a[3] = st_fma(b, -0.8152021169662476f, mm3 + 0.001160230953246355f);
}

/* Init's: mk and tk are the Middle and Treble knobs (0-100), m and t the same x 0.01 */
static void tone_set_init(struct tone3 *o, float b, float mk, float tk)
{
    float m = mk * 0.01f, t = tk * 0.01f;
    float tb = t * b, mb = m * b, tb3 = tb * 3.0f;
    float m1 = mk * 2.243566996185109e-06f, m2 = mk * 1.7280649444728624e-06f, t1 = tk * 1.419533964508446e-05f;
    float bk = b * 0.004269454162567854f, mb1 = mb * -0.4381859004497528f, mb2 = mb * 0.4018141031265259f;
    float mm0 = st_fma(mb, 0.1581858992576599f, m1);
    float mt = st_fma(tk, 1.4173459021549206e-05f, m2);    /* m 0.000173 + t 0.00142, fused */
    float mb3 = mb * -0.12181410193443298f;
    o->b[0] = ((t1 + bk) + mm0) + tb;
    o->b[1] = st_fma(b, -0.0033920500427484512f, mb1) - (mt + tb3);
    o->b[2] = (mb2 - ((bk + m1) + t1)) + tb3;
    o->b[3] = (st_fma(b, 0.0033920500427484512f, mt) + mb3) - tb;
    o->a[0] = st_fma(b, 1.185675024986267f, 0.0016770459478721023f) + mm0;
    o->a[1] = (st_fma(b, -3.184798002243042f, -0.0011594370007514954f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.8143250942230225f, -0.0016762519953772426f) - m1) + mb2;
    o->a[3] = st_fma(b, -0.8152021169662476f, (mb3 + m2) + 0.001160230953246355f);
}

/* the Bright section from the glided Gain g: (g^2 - 1.094 g, 0.906 g - g^2) / (-0.0945 - g + g^2, g - 0.0945 - g^2) */
static void bright_set(struct fos *c, float *a0, float g)
{
    float g2 = g * g;
    c->b0 = st_fma(g, -1.0944819450378418f, g2);
    c->b1 = st_fma(g, 0.9055178165435791f, -g2);
    *a0 = (-0.09448224306106567f - g) + g2;
    c->a1 = (g + -0.09448224306106567f) - g2;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow1_5, k[K_GAIN]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow2, k[K_BASS]);
    s->middle = k[K_MIDDLE] * 0.01f;
    s->treble = k[K_TREBLE] * 0.01f;
    tone_set_init(&s->tone, s->bass, k[K_MIDDLE], k[K_TREBLE]);
    bright_set(&s->bright_c, &s->bright_a0, s->gain);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow1_5, io->knobs[K_GAIN]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow2, io->knobs[K_BASS]);
    float middle = io->knobs[K_MIDDLE] * 0.01f;
    float treble = io->knobs[K_TREBLE] * 0.01f;
    float bright = io->knobs[K_BRIGHT];
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->bass = st_glide(s->bass, bass);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    for (int j = 0; j < 3; j++)
        sos_block(&PF[j], &s->pf[j], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->gain != gain)
            bright_set(&s->bright_c, &s->bright_a0, s->gain);
        if (s->bass != bass || s->middle != middle || s->treble != treble)
            tone_set(&s->tone, s->bass, s->middle, s->treble);
        float v = tone3_run_std(&s->tone, x[i]);
        x[i] = bright == 0.0f ? v * s->gain : fos_run_div(&s->bright_c, s->bright_a0, &s->bs, v);
    }
    sos_block(&PREAMP[0], &s->pre[0], x, n);
    sos_block(&PREAMP[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++)
        k[i] = st_fma(amp_env_run(&s->env, x[i]), 3.5f, 0.5f);
    stage_env_block(&s->st, x, k, n, 0.13f, 0.15f);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        p[2 * i] = p[2 * i + 1] = s->volume * 45.56182098388672f * x[i];
    }
}

const struct wvfx_desc dark_double_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x04),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dark Double",
    .category = "Dark",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
