/*
 * Calif Dual M (AMP › Calif): the original's Calif Dual M amp (QME-200 V1.2.2, library entry 4.47, id 0x07000069) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces):
 *   pre-filter    a fixed 2nd-order section, x Gain (pow2_5), a 3rd-order section following the Gain, a low pass
 *   stage 1       soft clip at +-20 (20 - 20 (1 - 3 |x| / 2048)^1024) on 4 points per sample, the FIR, x 0.1
 *   tone stack    the classic one, from Bass (pow3), Middle and Treble, x Volume x 0.8
 *   stage 2       soft clip at +-3 (3 - 3 (1 - |x| / 256)^1024), the same 4 points and FIR
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
    F(0.01742849), F(0.0004357122), F(0.01576493), F(-0.0003941233), \
    { F(0.01703656), F(-0.04859208), F(0.04615493), F(-0.01459941) }, \
    { F(0.02614062), F(-0.07612695), F(0.07385938), F(-0.02387305) }, \
    { F(0.6685962), F(-1.932426), F(1.859063), F(-0.5952336) }, \
    { F(-0.01671491), F(0.04831065), F(-0.04647658), F(0.01488084) }, \
    { F(1.084122), F(-3.082458), F(2.915878), F(-0.9175417) }, \
    { F(0.02831974), F(-0.07811142), F(0.07168208), F(-0.02188675) }, \
    { F(-0.009630778), F(0.02807525), F(-0.02717773), F(0.008733256) } }
static const struct brit_ts TONE = TS(ST_F);

static const struct sos POWER[5] = {
    { 0.6856151f, -1.3508667f, 0.6655459f, 1.0f, -1.9921937f, 0.9924503f },
    { 0.740451f, -1.3508667f, 0.6111739f, 1.0f, -1.8438501f, 0.8449786f },
    { 0.7400095f, -1.3508667f, 0.618928f, 1.0f, -1.6505494f, 0.6630856f },
    { 1.3508667f, -0.2702498f, 0.3697105f, 1.0f, -0.0697104f, 0.0661073f },
    { 1.3353673f, 1.3508667f, 0.7463793f, 1.0f, 1.193802f, 0.5987216f },
};

struct state {
    float gain, volume, middle, treble;         /* glided */
    float bass, presence;                       /* the last ones the filters were computed for */
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[5];
    struct stage s1, s2;
    struct tone3 pf2, tone;
};

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
    stage_pow_block(&s->s1, y, n, 20.0f, 0.00146484375f);
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
    stage_pow_block(&s->s2, y, n, 3.0f, 0.00390625f);
    sos_block(&s->pres_c, &s->pres, y, n);
    for (int j = 0; j < 5; j++)
        sos_block(&POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 0.13977599f;
}

const struct wvfx_desc calif_dualm_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x69),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif Dual M",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
