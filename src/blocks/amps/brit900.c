/*
 * Brit 900 (AMP › Brit): the original's Brit 900 amp (QME-200 V1.2.2, library entry 4.35, id 0x0700004E) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Another design than the other Brits:
 *
 *   gain          x Gain^2 x 26 (Gain through the A25 taper)
 *   pre-filter    a fixed 2nd-order section
 *   clipper       8x oversampled: a 40-tap low pass split in 8 phases of 5 taps interpolates 8 points per sample,
 *                 each through 7.5224 x / (|8 x| + 0.1677), the same low pass decimates them back
 *   EQ            a fixed 3rd-order section
 *   tone stack    3rd order, its own polynomials in Bass (A25), Middle and Treble (1 - B25), up to t^2 m^2 and
 *                 m b t^2, in double from single-precision products
 *   presence      a 2nd-order section moved by Presence (1 - Cp), its coefficients in double
 *   out           x Volume (A25) x 8
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample; the tone coefficients stop one glide step short of
 * the knobs. Presence takes effect at once.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 50.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
};

static const struct sos PRE = { 4.933197f, -1.5136632f, -3.3815813f, 1.0f, -1.5136632f, 0.55161554f };

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the last one the filter was computed for */
    struct sos pres_c;
    struct sos_state pre, pres;
    struct os8 nl;                              /* the 8x clipper (amp.h: its low pass is OS8_FIR) */
    struct tone3 eq, tone;
};

/* the presence section from p = 1 - Cp(knob), in double */
static void set_presence(struct state *s)
{
    double p = s->presence, a = p * 0.9993705149, b = p * 1.99874103;
    s->pres_c = (struct sos){
        (float)(1.035669467 - a), (float)(b + -2.063216547), (float)(1.027547079 - a),
        (float)(p + -1.014829978), (float)(2.020227753 - b), (float)st_fmad(p, 0.9987410298, -1.005408007),
    };
}

/* the tone stack: polynomials summed in the original's order, in double, the products of knobs in single */
static void set_tone(struct state *s)
{
    float bf = s->bass, mf = s->middle, tf = s->treble;
    float tbf = tf * bf, tmf = tf * mf, mbf = mf * bf, m2f = mf * mf, mbtf = mbf * tf, m2tf = m2f * tf;
    double B = bf, M = mf, T = tf;
    double tb = tbf, tm = tmf, mb = mbf, m2 = m2f, m2t = m2tf, t2 = (float)(tf * tf), mbt = mbtf;
    double m2t2 = (float)(m2tf * tf), tmt = (float)(tmf * tf), tbt = (float)(tbf * tf), mbt2 = (float)(mbtf * tf);

    double q4 = st_fmad(tm, 0.011806504146855341, tb * 0.5366592794025156);
    double t557 = T * 0.0005574621276159578, t549 = T * 0.0005488237503525344;
    double t2_434 = t2 * 0.00043355170026922017, t2_430 = t2 * 0.0004295082045288943;
    double m2t2_368 = m2t2 * 0.0036842991663945746, m2t2_1105 = m2t2 * 0.011052897499183725;
    double mbt2_167 = mbt2 * 0.16746814392702614, mbt2_502 = mbt2 * 0.5024044317810784;

    double n0 = st_fmad(M, -0.021535056870512315, (q4 + -0.001022405257103643) - B) + t557;
    n0 = st_fmad(m2, 0.009933151182020018, st_fmad(mb, 0.45150687191000083, n0));

    double n1 = st_fmad(B, 2.8943189025951184, 0.0009320331273073598);
    n1 = st_fmad(mb, -1.2743844336605246, st_fmad(M, 0.06329180648013778, n1));
    n1 = st_fmad(m2, -0.02803645754053154, n1) - ((t549 + tb * 1.6099778382075467) + tm * 0.03541951244056603);

    double n2 = (tm * 0.03541951244056603 + tb * 1.6099778382075467) + 0.001022405257103643;
    n2 = st_fmad(M, -0.061897572433932097, st_fmad(B, -2.7923922411111097, n2)) - t557;
    n2 = st_fmad(m2, 0.02627346153500303, st_fmad(mb, 1.1942482515910469, n2));

    double n3 = st_fmad(M, 0.020140822824306633, st_fmad(B, 0.8980733385159912, -0.0009320331273073598)) + t549;
    n3 = st_fmad(m2, -0.008170155176491506, st_fmad(mb, -0.371370689840523, n3)) - q4;

    double d0 = st_fmad(B, -2.1669551056997314, -0.002217462997291723);
    d0 = st_fmad(T, -0.0004121064830585223, st_fmad(M, -0.04672133784630063, d0));
    d0 = st_fmad(m2, 0.020331832024562076, st_fmad(mb, 0.9241741829346398, d0));
    d0 = st_fmad(tm, -0.008685217181801869, st_fmad(tb, -0.4029543696979434, d0));
    d0 = st_fmad(m2t, 0.0038415891530494332, d0) + t2_434;
    d0 = st_fmad(tbt, 0.42259335938482945, st_fmad(mbt, 0.17461768877497424, d0));
    d0 = st_fmad(tmt, 0.009124441925506625, d0) - (mbt2_167 + m2t2_368);

    double d1 = st_fmad(B, 5.954384434699435, 0.0016890316568707472);
    d1 = st_fmad(T, 0.0005031803876223164, st_fmad(M, 0.13025187136675548, d1));
    d1 = st_fmad(m2, -0.05653655204145135, st_fmad(mb, -2.5698432746114253, d1));
    d1 = st_fmad(tm, 0.02819307576235542, st_fmad(tb, 1.291377471227712, d1));
    d1 = st_fmad(m2t, -0.012315477235756956, d1) - t2_430;
    d1 = st_fmad(mbt, -0.5597944198071343, d1) + m2t2_1105;
    d1 = st_fmad(tbt, -1.25993407901996, st_fmad(tmt, -0.0275459377574795, d1)) + mbt2_502;

    double d2 = st_fmad(B, -5.41783340717335, 0.002212698767972652);
    d2 = st_fmad(mb, 2.3673360640674557, st_fmad(M, -0.1201439207625882, d2));
    d2 = st_fmad(m2, 0.05208139340948402, st_fmad(T, 0.00041299322624819716, d2));
    d2 = st_fmad(tm, -0.030368353981980623, st_fmad(tb, -1.3722079559285334, d2));
    d2 = st_fmad(mbt, 0.5957357732893459, st_fmad(m2t, 0.01310618701236561, d2)) - t2_434;
    d2 = st_fmad(tmt, 0.02771854973843912, st_fmad(tbt, 1.2520880798854317, d2)) - (m2t2_1105 + mbt2_502);

    double d3 = st_fmad(B, 1.6303960168719227, -0.0016937958861898185);
    d3 = st_fmad(mb, -0.7216669723906701, st_fmad(M, 0.036613387242133345, d3));
    d3 = st_fmad(m2, -0.015876673392594742, st_fmad(T, -0.0005022936444326416, d3));
    d3 = st_fmad(tm, 0.010860495401427073, st_fmad(tb, 0.4837848543987649, d3));
    d3 = st_fmad(m2t, -0.004632298929658088, d3) + t2_430;
    d3 = st_fmad(tmt, -0.009297053906466249, st_fmad(mbt, -0.21055904225718586, d3));
    d3 = (st_fmad(tbt, -0.4147473602503012, d3) + m2t2_368) + mbt2_167;

    struct tone3 *o = &s->tone;
    o->b[0] = (float)n0; o->b[1] = (float)n1; o->b[2] = (float)n2; o->b[3] = (float)n3;
    o->a[0] = (float)d0; o->a[1] = (float)d1; o->a[2] = (float)d2; o->a[3] = (float)d3;
}

/* the 8x clipper's curve (amp.h, os8_block: 8 points per sample interpolated by the 40-tap low pass split in 8
   phases, the same low pass decimating them back; the original's sums, in its order) */
static float clip8(float v)
{
    return v * 7.5224f / (st_abs(v * 8.0f) + 0.1677f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[BK_GAIN]);
    s->presence = 1.0f - st_taper(st_cp, k[BK_PRESENCE]);
    s->volume = st_taper(st_a25, k[BK_VOLUME]);
    s->bass = st_taper(st_a25, k[BK_BASS]);
    s->middle = 1.0f - st_taper(st_b25, k[BK_MIDDLE]);
    s->treble = 1.0f - st_taper(st_b25, k[BK_TREBLE]);
    s->eq = (struct tone3){ { 0.70747066f, -2.01385f, 1.9075177f, -0.6011383f },
                            { 1.0f, -2.7758846f, 2.5547285f, -0.77883947f } };
    set_tone(s);
    set_presence(s);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide once per sample in the stage that uses it;
 * Presence, not glided, before the block's first sample).
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float gain = st_taper(st_a25, k[BK_GAIN]), presence = 1.0f - st_taper(st_cp, k[BK_PRESENCE]);
    float volume = st_taper(st_a25, k[BK_VOLUME]), bass = st_taper(st_a25, k[BK_BASS]);
    float middle = 1.0f - st_taper(st_b25, k[BK_MIDDLE]), treble = 1.0f - st_taper(st_b25, k[BK_TREBLE]);
    float *p = io->io, x[WVFX_MAX_FRAMES];
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
    if (s->presence != presence) {
        s->presence = presence;
        set_presence(s);
    }
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        x[i] = s->gain * s->gain * 26.0f * p[2 * i];
    }
    sos_block(&PRE, &s->pre, x, n);
    os8_block(&s->nl, x, n, clip8);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->bass != bass || s->middle != middle || s->treble != treble)
            set_tone(s);
        float v = tone3_run_std(&s->eq, x[i]);       /* a0 = 1: the original does not divide */
        v = tone3_run_std(&s->tone, v);
        p[2 * i] = p[2 * i + 1] = s->volume * 8.0f * sos_run_div(&s->pres_c, &s->pres, v);
    }
}

const struct wvfx_desc brit900_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x4E),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 900",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
