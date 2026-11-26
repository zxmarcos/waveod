/*
 * Bog XT Red (AMP › Bog): the original's Bog XT Red amp (QME-200 V1.2.2, library entry 4.18, id 0x0700006E) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, bog.h have the pieces):
 *   pre-filter    two fixed 2nd-order sections
 *   gain          a 2nd-order section from Gain (pow2), a fixed 1st-order boost, a fixed peak, a fixed low pass
 *   stage         soft clip at +-0.6 (0.6 - 0.6 (1 - |x| / 51.2)^1024 above zero, 3 / 128 below) on 4 points per
 *                 sample, the FIR
 *   tone stack    the XT's, from Bass, Treble (pow3) and Middle (linear), x Volume
 *   presence      a 2nd-order section from Presence (pow2; 0 counts as 0.01)
 *   power amp     seven fixed 2nd-order sections, x 3.432
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample, Presence takes effect at once; the coefficients
 * stop one glide step short of the knobs. Init holds Presence at 0.01 below the taper's point 5.
 */
#include "bog.h"

enum { K_GAIN, K_PRESENCE, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

static const struct wvfx_param params[] = {
    BOG_PARAM("Gain", 50.0f, K_GAIN), BOG_PARAM("Presence", 50.0f, K_PRESENCE), BOG_PARAM("Volume", 50.0f, K_VOLUME),
    BOG_PARAM("Bass", 50.0f, K_BASS), BOG_PARAM("Middle", 50.0f, K_MIDDLE), BOG_PARAM("Treble", 50.0f, K_TREBLE),
};

static const struct fos BOOST = { 0.955999493598938f, -0.8679984211921692f, -0.8239979147911072f };

/* the gain section's factors (gain2_set) */
static const float GAIN[7] = { -1.1511720418930054f, -0.8488283753395081f, -0.9951754212379456f, -0.15672560036182404f,
                               -0.0014586920151486993f, -1.0048249959945679f, 0.1552668958902359f };

static const struct sos POWER[7] = {
    { 0.6293501257896423f, -1.2552050352096558f, 0.6259818077087402f, 1.0f, -1.9965946674346924f,
      0.9968100190162659f },
    { 0.6919012069702148f, -1.2552050352096558f, 0.5639318823814392f, 1.0f, -1.9898749589920044f,
      0.9899314045906067f },
    { 0.6600173115730286f, -1.2552050352096558f, 0.5954958200454712f, 1.0f, -1.3790010213851929f,
      0.4127880036830902f },
    { 1.2552050352096558f, 0.06519600003957748f, 0.04711860045790672f, 1.0f, -0.7625532150268555f,
      0.5483999848365784f },
    { 1.2552050352096558f, -1.1941200494766235f, 0.7442759871482849f, 1.0f, -0.6311331987380981f,
      0.030364299193024635f },
    { 1.2552050352096558f, 0.28298819065093994f, 0.32264238595962524f, 1.0f, 1.1395312547683716f,
      0.4719349145889282f },
    { 1.1460351943969727f, 1.2552050352096558f, 0.37086451053619385f, 1.0f, 0.5674557089805603f,
      0.31125208735466003f },
};

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the knob the presence section was computed for */
    struct sos gain_c;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pre[2], gs, peak, lowpass, pres, power[7];
    struct fos_state boost;
    struct stage st;
};

static float clip(float x)
{
    return clip_pow_asym(x, 0.6000000238418579f, 0.0234375f, 0.01953125f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2, k[K_GAIN]);
    s->presence = (int)(k[K_PRESENCE] * 2.55f) < 5 ? 0.01f : st_taper(st_pow2, k[K_PRESENCE]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow3, k[K_BASS]);
    s->middle = k[K_MIDDLE] * 0.01f;
    s->treble = st_taper(st_pow3, k[K_TREBLE]);
    gain2_set(&s->gain_c, s->gain, s->gain, GAIN);
    bog_tone_set(&s->tone, &BOG_XT_TS, s->bass, s->middle, s->treble);
    bog_xt_presence_set(&s->pres_c, s->presence);
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
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]);
    float presence = st_taper(st_pow2, io->knobs[K_PRESENCE]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow3, io->knobs[K_BASS]);
    float middle = io->knobs[K_MIDDLE] * 0.01f;
    float treble = st_taper(st_pow3, io->knobs[K_TREBLE]);
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
        bog_xt_presence_set(&s->pres_c, presence == 0.0f ? 0.01f : presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&BOG_XT_PRE[0], &s->pre[0], x, n);
    sos_block(&BOG_XT_PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            gain2_set(&s->gain_c, s->gain, s->gain, GAIN);
        x[i] = fos_run(&BOOST, &s->boost, sos_run_div(&s->gain_c, &s->gs, x[i]));
    }
    sos_block_div(&BOG_XT_PEAK, &s->peak, x, n);
    sos_block_div(&BOG_XT_LOWPASS, &s->lowpass, x, n);
    stage_clip_block(&s->st, x, n, clip);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle)
            bog_tone_set(&s->tone, &BOG_XT_TS, s->bass, s->middle, s->treble);
        x[i] = tone3_run_b1b0(&s->tone, x[i]) * s->volume;
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 7; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 3.432000160217285f;
}

const struct wvfx_desc bog_xtred_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x6E),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bog XT Red",
    .category = "Bog",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
