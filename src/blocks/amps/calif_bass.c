/*
 * Calif Bass (AMP › Calif): the original's Calif Bass amp (QME-200 V1.2.2, library entry 4.58, id 0x07000077) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces):
 *   pre-filter    a fixed 2nd-order section
 *   gain          a 1st-order section from Gain (pow2_5)
 *   peak          a fixed 2nd-order section, x 0.1
 *   stage         soft clip 40 - 40 e^(-|x| / 4), odd, not held at +-40, on 4 points per sample, the FIR
 *   tone stack    3rd order from Bass (pow2), Middle and Treble (linear), x Volume x 0.4
 *   power amp     six fixed 2nd-order sections, x 0.5740927
 * Gain, Volume and Treble glide 0.0001 per sample; Bass and Middle take effect at once. The filters' coefficients
 * stop one glide step short of the knobs.
 */
#include "calif.h"

enum { K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

#define PARAM(name, knob) { name, 0.0f, 100.0f, 1.0f, 50.0f, knob, 0x15, 0, 0, 0, 0 }
static const struct wvfx_param params[] = {
    PARAM("Gain", K_GAIN), PARAM("Volume", K_VOLUME), PARAM("Bass", K_BASS), PARAM("Middle", K_MIDDLE),
    PARAM("Treble", K_TREBLE),
};

static const struct sos PRE = { 1941.625244140625f, -3880.860107421875f, 1939.2362060546875f, 1.0f,
                                -1.9901165962219238f, 0.9901446104049683f };

static const struct sos PEAK = { 1.0062999725341797f, -2.0f, 0.9937000274658203f, 1.0045000314712524f, -2.0f,
                                 0.9955000281333923f };

static const struct sos POWER[6] = {
    { 0.6443336009979248f, -1.285225749015808f, 0.6409416794776917f, 1.0f, -1.9976845979690552f, 0.9977682828903198f },
    { 0.7104644179344177f, -1.285225749015808f, 0.5748234987258911f, 1.0f, -1.4748514890670776f,
      0.47875121235847473f },
    { 0.6568663120269775f, -1.285225749015808f, 0.6289175748825073f, 1.0f, -1.7646156549453735f, 0.7647920846939087f },
    { 1.285225749015808f, -0.813409686088562f, 0.09583680331707001f, 1.0f, -1.0038894414901733f, 0.0630050003528595f },
    { 0.9784265160560608f, -1.285225749015808f, 0.49328839778900146f, 1.0f, -1.3330315351486206f,
      0.5722206234931946f },
    { 1.285225749015808f, 0.4723738133907318f, 0.00284620001912117f, 1.0f, 0.06284759938716888f,
      4.289999924367294e-05f },
};

struct knobs { float gain, volume, bass, middle, middle_raw, treble, treble_raw; };

static struct knobs knobs(const float *k)
{
    struct knobs t;
    t.gain = st_taper(st_pow2_5, k[K_GAIN]);
    t.volume = k[K_VOLUME] * 0.01f;
    t.bass = st_taper(st_pow2, k[K_BASS]);
    t.middle_raw = k[K_MIDDLE];
    t.middle = k[K_MIDDLE] * 0.01f;
    t.treble_raw = k[K_TREBLE];
    t.treble = k[K_TREBLE] * 0.01f;
    return t;
}

struct state {
    float gain, volume, treble;         /* glided */
    float bass, middle;                 /* the last ones the tone stack was computed for */
    struct fos gain_c;
    float gain_a0;
    struct tone3 tone;
    struct sos_state pre, peak, power[6];
    struct fos_state gs;
    struct stage st;
};

/* the gain section from the glided Gain g */
static void bass_gain_set(struct fos *c, float *a0, float g)
{
    float g2 = g * g;
    c->b0 = g * -0.4699999988079071f;
    c->b1 = g * 0.4699999988079071f;
    *a0 = st_fma(g, -1.0002410411834717f, -1.9704749584197998f) + g2;
    c->a1 = st_fma(g, 0.999758780002594f, 1.9695249795913696f - g2);
}

/* the tone stack from Bass b, Middle m (mr the knob) and Treble t, as Process computes it */
static void tone_set(struct tone3 *o, float b, float m, float mr, float t)
{
    float mb = m * b, tb = t * b, tb3 = tb * 3.0f;
    float m1 = mr * 1.8310030327484128e-06f, m2 = mr * 1.5732518932054518e-06f, b1 = b * 0.0019463920034468174f;
    float t1 = t * 0.0014195339754223824f, t2 = t * 0.0014173459494486451f;
    float mb1 = mb * -0.36911851167678833f, mb2 = mb * 0.3508814871311188f;
    float u = st_fma(mb, 0.12911850214004517f, m1), u3 = st_fma(mb, -0.1108814999461174f, m2);
    o->b[0] = ((t1 + b1) + u) + tb;
    o->b[1] = st_fma(b, -0.0017396219773218036f, mb1) - ((m2 + t2) + tb3);
    o->b[2] = (mb2 - ((b1 + m1) + t1)) + tb3;
    o->b[3] = (st_fma(b, 0.0017396219773218036f, u3) + t2) - tb;
    o->a[0] = st_fma(b, 1.092648983001709f, 0.0015482900198549032f) + u;
    o->a[1] = (st_fma(b, -3.0924429893493652f, -0.001288390951231122f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.907351016998291f, -0.0015478930436074734f) - m1) + mb2;
    o->a[3] = st_fma(b, -0.9075574278831482f, u3 + 0.0012887880438938737f);
}

/* the same as Init computes it: Treble's terms from the knob (tr) and Middle's -0.1108815 term grouped otherwise */
static void tone_init(struct tone3 *o, float b, float m, float mr, float t, float tr)
{
    float mb = m * b, tb = t * b, tb3 = tb * 3.0f;
    float m1 = mr * 1.8310030327484128e-06f, m2 = mr * 1.5732518932054518e-06f, b1 = b * 0.0019463920034468174f;
    float t1 = tr * 1.419533964508446e-05f, w = st_fma(tr, 1.4173459021549206e-05f, m2);
    float mb1 = mb * -0.36911851167678833f, mb2 = mb * 0.3508814871311188f, mb3 = mb * -0.1108814999461174f;
    float u = st_fma(mb, 0.12911850214004517f, m1);
    o->b[0] = ((t1 + b1) + u) + tb;
    o->b[1] = st_fma(b, -0.0017396219773218036f, mb1) - (w + tb3);
    o->b[2] = (mb2 - ((b1 + m1) + t1)) + tb3;
    o->b[3] = (st_fma(b, 0.0017396219773218036f, w) + mb3) - tb;
    o->a[0] = st_fma(b, 1.092648983001709f, 0.0015482900198549032f) + u;
    o->a[1] = (st_fma(b, -3.0924429893493652f, -0.001288390951231122f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.907351016998291f, -0.0015478930436074734f) - m1) + mb2;
    o->a[3] = st_fma(b, -0.9075574278831482f, (mb3 + m2) + 0.0012887880438938737f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct knobs t = knobs(k);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->volume = t.volume;
    s->treble = t.treble;
    s->bass = t.bass;
    s->middle = t.middle;
    bass_gain_set(&s->gain_c, &s->gain_a0, s->gain);
    tone_init(&s->tone, t.bass, t.middle, t.middle_raw, t.treble, t.treble_raw);
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
    struct knobs t = knobs(io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->volume = st_glide(s->volume, t.volume);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE, &s->pre, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        if (s->gain != t.gain)
            bass_gain_set(&s->gain_c, &s->gain_a0, s->gain);
        x[i] = fos_run_div(&s->gain_c, s->gain_a0, &s->gs, x[i]);
    }
    sos_block_div(&PEAK, &s->peak, x, n);
    for (int i = 0; i < n; i++)
        x[i] = x[i] * 0.1f;
    {
        float buf[STAGE_BLOCK_PTS];
        STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip_exp_open(v, 40.0f, 0.25f)); /* 40 - 40 e^(-|x| / 4), odd */
        stage_block_fir_ord(&s->st, buf, x, n, AMP_FIR_ORDER2);
    }
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        s->treble = st_glide(s->treble, t.treble);
        if (s->bass != t.bass || s->middle != t.middle || s->treble != t.treble) {
            s->bass = t.bass;
            s->middle = t.middle;
            tone_set(&s->tone, s->bass, s->middle, t.middle_raw, s->treble);
        }
        x[i] = s->volume * 0.4f * tone3_run_b1b0(&s->tone, x[i]);
    }
    for (int j = 0; j < 6; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.5740926861763f;
}

const struct wvfx_desc calif_bass_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x77),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif Bass",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
