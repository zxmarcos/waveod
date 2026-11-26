/*
 * Brit SLP (AMP › Brit): the original's Brit SLP amp (QME-200 V1.2.2, library entry 4.33, id 0x07000030) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision but for one term of the presence shelf.
 *
 * Per sample (brit.h has the pieces):
 *   pre-filters   three fixed 2nd-order sections
 *   gain          a 1st-order high pass whose gain is the Gain knob
 *   filters       three more fixed 2nd-order sections
 *   tone stack    3rd order, the Brit 50's polynomials, from Bass, Middle and Treble (no tapers)
 *   low pass      a fixed 1st-order section
 *   stage         one soft clip at +-0.2 (0.2 - 0.2 e^(-10 |x|), no flat part) on 4 points per sample, the FIR
 *   presence      a high shelf (the cookbook's form) moved by Presence
 *   power amp     four fixed 2nd-order sections, x Volume x 120
 * Gain and Volume glide 0.0001 per sample while the block plays; Bass, Middle, Treble and Presence glide 0.004 per
 * block (64 samples), bypassed too. The original's Init takes Gain through the pow6 taper, Process through pow2 (the
 * glide moves from one to the other); its tone stack's sum also computes an envelope it never uses (left out).
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 50.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
};

static const struct sos PRE1[3] = {
    { 1.0798421f, -2.0f, 0.9201908f, 1.0f, -1.8518506f, 0.8518861f },
    { 2.0f, 1.2156135f, 0.0839873f, 1.0f, 0.4996562f, 0.0004182f },
    { 2.0f, 1.8246365f, 0.0010314f, 1.0f, 0.9134151f, 0.0029696f },
};
static const struct sos PRE2[3] = {
    { 1.1012824f, -2.0f, 0.9063875f, 1.0f, -1.7879839f, 0.7985723f },
    { 1.686581f, -2.0f, 0.5453045f, 1.0f, -1.1857636f, 0.3231407f },
    { 2.0f, -0.003607f, 0.0005199f, 1.0f, -0.0017191f, 0.0003756f },
};
static const float GAIN_K = 0.9994849f, GAIN_A1 = -0.9989698f;
static const struct fos LOWPASS = { 0.05397286f, 0.05397286f, -0.89205426f };
static const struct sos POWER[4] = {
    { 0.4403824f, -0.7347618f, 0.2943795f, 1.0f, -1.6652972f, 0.6663875f },
    { 0.6845887f, 0.7347618f, 0.2744752f, 1.0f, 1.3415186f, 0.6412097f },
    { 0.4900223f, -0.0392964f, 0.0064794f, 1.0f, 0.0449056f, 0.0174351f },
    { 0.8250819f, 0.2896389f, 0.0232444f, 1.0f, 0.7006105f, 0.2536617f },
};

struct state {
    float gain, volume;                         /* glided per sample */
    float bass, middle, treble, presence;       /* glided per block */
    struct fos gain_c;
    struct sos pres_c;
    struct sos_state pre1[3], pre2[3], pres, power[4];
    struct fos_state gain_s, lowpass;
    struct stage s1;
    struct tone3 tone;
};

static void set_gain(struct state *s)
{
    s->gain_c = (struct fos){ s->gain * GAIN_K, s->gain * -GAIN_K, GAIN_A1 };
}

/* the high shelf from A = 2^arg (the cookbook's, cos w0 0.3718308, 2 sqrt(A) alpha 1.2214481 sqrt(A)); a1 in
   double with cos w0 to double precision */
static void set_presence(struct state *s, float arg)
{
    float A = st_exp2f(arg);
    float am1 = A + -1.0f, ap1 = A + 1.0f;
    float sq = st_sqrt(A) * 1.2214481f;
    float c = am1 * 0.3718308f;
    float t1 = c + ap1, t2 = ap1 - c;
    float b1 = (A * -2.0f) * st_fma(ap1, 0.3718308f, am1);
    double a1 = st_fmad((double)ap1, -0.3718307983048904, (double)am1);
    s->pres_c = (struct sos){ (t1 + sq) * A, b1, (t1 - sq) * A, t2 + sq, (float)(a1 + a1), t2 - sq };
}

static void set_tone(struct state *s)
{
    tone_brit_f(&s->tone, &BRIT50_TS_F, s->bass, s->middle, s->treble);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->presence = k[BK_PRESENCE] * 0.005f;
    s->gain = st_taper(st_pow6, k[BK_GAIN]);
    s->volume = k[BK_VOLUME] * 0.01f;
    s->bass = k[BK_BASS] * 0.01f;
    s->middle = k[BK_MIDDLE] * 0.01f;
    s->treble = k[BK_TREBLE] * 0.01f;
    set_gain(s);
    set_tone(s);
    set_presence(s, k[BK_PRESENCE] * 0.0069179153f);
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
    const float *k = io->knobs;
    float presence = k[BK_PRESENCE] * 0.005f, volume = k[BK_VOLUME] * 0.01f;
    float bass = k[BK_BASS] * 0.01f, gain = st_taper(st_pow2, k[BK_GAIN]);
    float middle = k[BK_MIDDLE] * 0.01f, treble = k[BK_TREBLE] * 0.01f;

    /* once per block, bypassed too */
    if (s->bass != bass || s->middle != middle || s->treble != treble) {
        s->bass = st_glide_by(s->bass, bass, 0.004f);
        s->middle = st_glide_by(s->middle, middle, 0.004f);
        s->treble = st_glide_by(s->treble, treble, 0.004f);
        set_tone(s);
    }
    if (s->presence != presence) {
        s->presence = st_glide_by(s->presence, presence, 0.004f);
        set_presence(s, s->presence * 1.3835831f);
    }
    if (!io->enabled)
        return;

    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    for (int j = 0; j < 3; j++)
        sos_block(&PRE1[j], &s->pre1[j], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        vol[i] = s->volume;
        if (s->gain != gain)
            set_gain(s);
        x[i] = fos_run(&s->gain_c, &s->gain_s, x[i]);
    }
    for (int j = 0; j < 3; j++)
        sos_block(&PRE2[j], &s->pre2[j], x, n);
    for (int i = 0; i < n; i++)
        x[i] = fos_run(&LOWPASS, &s->lowpass, tone3_run_std(&s->tone, x[i]));
    {
        float buf[STAGE_BLOCK_PTS];
        STAGE_BLOCK_POINTS(&s->s1, x, n, buf, clip_env1(v, 10.0f, 0.2f));    /* +-0.2 (1 - e^(-10 |x|)) */
        stage_block_fir(&s->s1, buf, x, n);
    }
    for (int i = 0; i < n; i++)
        x[i] = sos_run_div_b1(&s->pres_c, &s->pres, x[i]);
    for (int j = 0; j < 4; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 120.0f * x[i];
}

const struct wvfx_desc britslp_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x30),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit SLP",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
