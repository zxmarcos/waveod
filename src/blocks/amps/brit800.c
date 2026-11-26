/*
 * Brit 800 (AMP › Brit): the original's Brit 800 amp (QME-200 V1.2.2, library entry 4.34, id 0x07000035) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (brit.h has the pieces):
 *   pre-filters   two fixed 2nd-order sections
 *   gain          a 3rd-order stage whose coefficients follow Gain (pow2 x 0.99) and its square
 *   pre-EQ        a fixed 2nd-order section
 *   stage         one asymmetric soft clip, +2.5 / -3 (L - L (1 - 15 |x| / 1024)^1024 each side), on 4 points per
 *                 sample, the anti-aliasing FIR, x 0.65
 *   tone stack    3rd order, its own polynomials in Bass (pow2), Middle and Treble (with Treble^2 terms)
 *   Volume        here, before the presence
 *   presence      a shelf moved by Presence (pow2), x 0.7225
 * Gain, Volume and Treble glide 0.0001 per sample; the gain and tone coefficients stop one glide step short of the
 * knob. The original's Init computes the gain coefficients from the plain taper with its factors x 0.99 folded in,
 * Process from the taper x 0.99: the same values, rounded two ways, both kept.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 50.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
};

static const struct sos PRE1 = { 4.2770586f, -8.406497f, 4.130721f, 1.0f, -1.9200433f, 0.9216857f };
static const struct sos PRE2 = { 8.406497f, 8.386827f, 1.3602307f, 1.0f, 0.9281393f, 0.1109842f };
static const struct sos PRE_EQ = { 1.009f, -1.9998f, 0.991f, 1.0051f, -1.9998f, 0.9949f };

/* the gain stage: bi = g^2 qi + g ni, ai = g^2 qi + g di + ki (q0 = 1); N, D for g = the taper x 0.99 (Process),
   N0, D0 for g = the plain taper (Init) */
static const float GAIN_Q[4] = { 1.0f, -2.90236f, 2.80472f, -0.9023599f };
static const float GAIN_K[4] = { -0.03506465f, 0.03337474f, 0.03506351f, -0.03337588f };
static const float GAIN_N[4] = { -1.011338f, 2.912591f, -2.793382f, 0.892129f };
static const float GAIN_D[4] = { -0.9765392f, 2.878949f, -2.828181f, 0.9257703f };
static const float GAIN_N0[4] = { -1.0012246f, 2.883465f, -2.765448f, 0.88320774f };
static const float GAIN_D0[4] = { -0.9667738f, 2.8501594f, -2.7998993f, 0.9165126f };

struct state {
    float gain, volume, treble;                 /* glided (gain = the taper x 0.99) */
    float bass, middle, presence;               /* the last ones the filters were computed for */
    struct sos pres_c;
    struct sos_state pre1, pre2, pre_eq, pres;
    struct stage s1;
    struct tone3 gain_s, tone;
};

static void gain_coefs(struct tone3 *s, float g, float g2, const float *n, const float *d)
{
    for (int i = 0; i < 4; i++) {
        float q = i ? g2 * GAIN_Q[i] : g2;
        s->b[i] = st_fma(g, n[i], q);
        s->a[i] = st_fma(g, d[i], GAIN_K[i]) + q;
    }
}

/* the tone stack: polynomials in b, m, t with t^2 terms, summed in the original's order (most sums fused) */
static void set_tone(struct state *s)
{
    struct tone3 *o = &s->tone;
    float b = s->bass, m = s->middle, t = s->treble;
    float m2 = m * m, t2 = t * t, t2m2 = t2 * m2, mb = m * b, tb = t * b, tm = t * m;
    float bn0 = b * 0.02205095f, bn1 = b * 0.01855298f, tb3 = tb * 3.0f;
    float tm22 = tm * 0.022f, tm66 = tm * 0.066f;

    float n0 = st_fma(t, 0.02303876f, bn0 + 0.000485121f) + tb;
    n0 = st_fma(m2, -0.01850923f, n0) - tm22;
    n0 = st_fma(st_fma(b, 0.8413287f, 0.01889048f), m, n0);

    float n1 = st_fma(t, -0.06702267f, -0.0004081656f - (bn1 + tb3)) + tm66;
    n1 = st_fma(m2, 0.05224257f, n1);
    n1 = st_fma(st_fma(b, -2.374662f, -0.05254846f), m, n1);

    float n2 = st_fma(t, 0.06496124f, (-0.000485121f - bn0) + tb3);
    n2 = st_fma(m2, -0.04895743f, n2) - tm66;
    n2 = st_fma(st_fma(b, 2.225338f, 0.04857619f), m, n2);

    float n3 = (st_fma(t, -0.02097733f, bn1 + 0.0004081656f) - tb) + tm22;
    n3 = st_fma(m2, 0.0152241f, n3);
    n3 = st_fma(st_fma(b, -0.6920046f, -0.0149182f), m, n3);

    /* d0, d2: (m, m^2, t^2 m^2, m b t^2) + (m, b) t^2 + (t, t m) b + (m, m^2) t */
    /* the t^2 m^2 and m b t^2 terms: one pair in d0 and d3, the other in d1 and d2 */
    float t2m2_03 = t2m2 * 0.003226667f, mbt2_03 = mb * 0.1466667f * t2;
    float t2m2_12 = t2m2 * 0.00968f, mbt2_12 = mb * 0.44f * t2;
    float d0 = (st_fma(m2, -0.01905469f, st_fma(m, -0.004976377f, 0.02607546f)) + t2m2_03) - mbt2_03;
    d0 = st_fma(st_fma(b, -0.2234357f, st_fma(m, 0.001537748f, -0.005144114f)), t2, d0);
    d0 = st_fma(st_fma(st_fma(t, 0.1404052f, 0.8661223f), m, st_fma(t, 0.2468968f, 1.132854f)), b, d0);
    d0 = st_fma(st_fma(m2, -0.003088914f, st_fma(m, -0.002197921f, 0.005685316f)), t, d0);

    float d2 = (st_fma(m2, -0.05034798f, st_fma(m, -0.01362096f, 0.06192815f)) + t2m2_12) - mbt2_12;
    d2 = st_fma(st_fma(b, -0.6565643f, st_fma(m, 0.004915586f, -0.01421589f)), t2, d2);
    d2 = st_fma(st_fma(st_fma(t, 0.3582615f, 2.288544f), m, st_fma(t, 0.6331032f, 2.867149f)), b, d2);
    d2 = st_fma(st_fma(m2, -0.007881753f, st_fma(m, -0.006191412f, 0.01367546f)), t, d2);

    /* d1, d3: (b, t^2 m^2, m b t^2) + b t^2 + b t + t m^2 + (b, t^2, b t) m */
    float d1 = (st_fma(b, -3.129111f, -0.06974743f) - t2m2_12) + mbt2_12;
    d1 = st_fma(st_fma(b, 0.6634357f, 0.01482057f), t2, d1);
    d1 = st_fma(st_fma(b, -0.6861594f, -0.01529505f), t, d1);
    d1 = st_fma(st_fma(t, 0.008574247f, 0.05375437f), m2, d1);
    d1 = st_fma(st_fma(st_fma(b, -0.3897385f, 0.006409164f), t, st_fma(t2, -0.004764414f, st_fma(b, -2.44338f, 0.01435205f))), m, d1);

    float d3 = (st_fma(b, -0.8708859f, -0.01824896f) - t2m2_03) + mbt2_03;
    d3 = st_fma(st_fma(b, 0.2165643f, 0.004539427f), t2, d3);
    d3 = st_fma(st_fma(b, -0.1938406f, -0.004064175f), t, d3);
    d3 = st_fma(st_fma(t, 0.002396419f, 0.0156483f), m2, d3);
    d3 = st_fma(st_fma(st_fma(b, -0.1089282f, 0.00198017f), t, st_fma(t2, -0.001688919f, st_fma(b, -0.7112864f, 0.004245286f))), m, d3);

    o->b[0] = n0; o->b[1] = n1; o->b[2] = n2; o->b[3] = n3;
    o->a[0] = d0; o->a[1] = d1; o->a[2] = d2; o->a[3] = d3;
}

/* the presence shelf from k = 2^(knob x 0.24914461): b = 1 +- 1.6489257 k, a = 1 +- 1.6489257 / k */
static void set_presence(struct state *s)
{
    float k = st_exp2f(s->presence * 0.24914461f);
    float p = k * 1.6489257f, q = 1.6489257f / k;
    s->pres_c = (struct sos){ p + 1.0f, -0.29103848f, 1.0f - p, q + 1.0f, -0.29103848f, 1.0f - q };
}

/* the clipper: +2.5 - 2.5 p above zero, p 3 - 3 below, p = (1 - 15 |x| / 1024)^1024; flat beyond +2.5 / -3 */
static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct brit_knobs t = brit_knobs(k, st_pow2);
    *s = (struct state){ 0 };
    s->gain = t.gain * 0.99f;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    gain_coefs(&s->gain_s, t.gain, s->gain * s->gain, GAIN_N0, GAIN_D0);
    set_tone(s);
    set_presence(s);
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
    struct brit_knobs t = brit_knobs(io->knobs, st_pow2);
    float gain = t.gain * 0.99f;
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, t.volume);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    if (s->presence != t.presence) {
        s->presence = t.presence;
        set_presence(s);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE1, &s->pre1, x, n);
    sos_block(&PRE2, &s->pre2, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            gain_coefs(&s->gain_s, s->gain, s->gain * s->gain, GAIN_N, GAIN_D);
        x[i] = tone3_run_std(&s->gain_s, x[i]);
    }
    sos_block_div(&PRE_EQ, &s->pre_eq, x, n);
    {
        float buf[STAGE_BLOCK_PTS];
        STAGE_BLOCK_POINTS(&s->s1, x, n, buf, clip_pow2(v, 2.5f, 3.0f, 0.0146484375f, 0.0146484375f)); /* +2.5 / -3 */
        stage_block_fir(&s->s1, buf, x, n);
    }
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        s->treble = st_glide(s->treble, t.treble);
        if (s->bass != t.bass || s->middle != t.middle || s->treble != t.treble) {
            s->bass = t.bass;
            s->middle = t.middle;
            set_tone(s);
        }
        x[i] = tone3_run_b1b0(&s->tone, x[i] * 0.65f) * s->volume;
    }
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = sos_run_div(&s->pres_c, &s->pres, x[i]) * 0.7225f;
}

const struct wvfx_desc brit800_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x35),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 800",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
