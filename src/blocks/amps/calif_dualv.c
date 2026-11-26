/*
 * Calif Dual V (AMP › Calif): the original's Calif Dual V amp (QME-200 V1.2.2, library entry 4.46, id 0x07000068) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces):
 *   pre-filter    a fixed 2nd-order section, x Gain (pow2_5), a 3rd-order section following the Gain, a low pass
 *   stage 1       soft clip at +-25 (25 - 25 (1 - |x| / 1024)^1024) on 4 points per sample, the FIR, x 0.1
 *   tone stack    the classic one, from Bass (pow3), Middle and Treble, x Volume x 0.8
 *   stage 2       soft clip at +-3, k 1 / 512 below zero and 1 / 1024 above (fused), the same 4 points and FIR
 *   presence      a high shelf of 10^(0.4 p) and a section of 10^(-p / 20)
 *   power amp     five fixed 2nd-order sections, x 0.13977599
 * Gain, Volume, Middle and Treble glide 0.0001 per sample; Bass and Presence take effect at once. The tone and
 * the Gain's section's coefficients stop one glide step short of the knobs.
 */
#include "calif.h"

static const struct wvfx_param params[] = {
    CALIF_PARAM("Gain", 50.0f, CK_GAIN), CALIF_PARAM("Presence", 50.0f, CK_PRESENCE),
    CALIF_PARAM("Volume", 50.0f, CK_VOLUME), CALIF_PARAM("Bass", 50.0f, CK_BASS),
    CALIF_PARAM("Middle", 50.0f, CK_MIDDLE), CALIF_PARAM("Treble", 50.0f, CK_TREBLE),
};

#define TS(F) { \
    F(0.01771808), F(0.000442952), F(0.01547534), F(-0.0003868835), \
    { F(0.01736307), F(-0.04890412), F(0.04582842), F(-0.01428737) }, \
    { F(0.02614062), F(-0.07612695), F(0.07385938), F(-0.02387305) }, \
    { F(0.6813673), F(-1.945197), F(1.846292), F(-0.5824625) }, \
    { F(-0.01703418), F(0.04862993), F(-0.04615731), F(0.01456156) }, \
    { F(1.108421), F(-3.106178), F(2.891579), F(-0.8938217) }, \
    { F(0.02895461), F(-0.07867671), F(0.07104787), F(-0.02132081) }, \
    { F(-0.009904502), F(0.02836346), F(-0.02690401), F(0.008445053) } }
static const struct brit_ts TONE = TS(ST_F);

static const struct sos POWER[5] = {
    { 0.0110298f, -0.0208578f, 0.0098499f, 1.0f, -1.9830484f, 0.9832869f },
    { 1.7659975f, -3.5047934f, 1.738884f, 1.0f, -1.9960556f, 0.996068f },
    { 2.0264673f, -3.5047934f, 1.4807941f, 1.0f, -1.481333f, 0.5033294f },
    { 3.5047934f, -0.3522748f, 0.7531878f, 1.0f, 0.7461886f, 0.5952405f },
    { 3.5047934f, 3.1917987f, 0.3306191f, 1.0f, -0.3662435f, 0.0108067f },
};

struct state {
    float gain, volume, middle, treble;         /* glided */
    float bass, presence;                       /* the last ones the filters were computed for */
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[5];
    struct stage s1, s2;
    struct tone3 pf2, tone;
};

static float clip2(float x)
{
    return clip_pow_asym(x, 3.0f, 0.001953125f, 0.0009765625f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct iic_knobs t = dual_knobs(k);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    dual_pf2_set(&s->pf2, s->gain);
    tone_brit_f(&s->tone, &TONE, s->bass, s->middle, s->treble);
    dual_presence_set(&s->pres_c, k[CK_PRESENCE] * 0.013287712f, k[CK_PRESENCE] * -0.001660964f);
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
    struct iic_knobs t = dual_knobs(io->knobs);
    float *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->volume = st_glide(s->volume, t.volume);
            s->middle = st_glide(s->middle, t.middle);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    if (s->presence != t.presence) {
        s->presence = t.presence;
        dual_presence_set(&s->pres_c, s->presence * 1.3287712f, s->presence * -0.1660964f);
    }
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    sos_block(&DUAL_PRE, &s->pre, y, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        if (s->gain != t.gain)
            dual_pf2_set(&s->pf2, s->gain);
        y[i] = tone3_run_std(&s->pf2, y[i] * s->gain);
    }
    sos_block_div(&DUAL_LOWPASS, &s->lowpass, y, n);
    stage_pow_block(&s->s1, y, n, 25.0f, 0.0009765625f);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            tone_brit_f(&s->tone, &TONE, s->bass, s->middle, s->treble);
        }
        y[i] = s->volume * 0.8f * tone3_run_b1b0(&s->tone, y[i] * 0.1f);
    }
    stage_clip_block(&s->s2, y, n, clip2);
    sos_block(&s->pres_c, &s->pres, y, n);
    for (int j = 0; j < 5; j++)
        sos_block(&POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 0.13977599f;
}

const struct wvfx_desc calif_dualv_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x68),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif Dual V",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
