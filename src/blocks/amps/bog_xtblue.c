/*
 * Bog XT Blue (AMP › Bog): the original's Bog XT Blue amp (QME-200 V1.2.2, library entry 4.17, id 0x07000043) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, bog.h have the pieces):
 *   pre-filter    two fixed 2nd-order sections
 *   gain          a 1st-order section from Gain (pow3), a fixed 1st-order boost, a fixed peak, a fixed low pass
 *   stage         soft clip at +-1.5 (1.5 - 1.5 (1 - |x| / 256)^1024 above zero, 3 / 512 below) on 4 points per
 *                 sample, the FIR
 *   tone stack    the XT's, from Bass, Treble (pow3) and Middle (linear), x Volume
 *   presence      a 2nd-order section from Presence (pow2; 0 counts as 0.01)
 *   power amp     six fixed 2nd-order sections, x 2.90736
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample, Presence takes effect at once; the coefficients
 * stop one glide step short of the knobs. Init holds Presence at 0.01 below the taper's point 5.
 */
#include "bog.h"

enum { K_GAIN, K_PRESENCE, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

static const struct wvfx_param params[] = {
    BOG_PARAM("Gain", 50.0f, K_GAIN), BOG_PARAM("Presence", 50.0f, K_PRESENCE), BOG_PARAM("Volume", 50.0f, K_VOLUME),
    BOG_PARAM("Bass", 50.0f, K_BASS), BOG_PARAM("Middle", 50.0f, K_MIDDLE), BOG_PARAM("Treble", 50.0f, K_TREBLE),
};

static const struct fos BOOST = { 0.3024907112121582f, -0.27464601397514343f, -0.8050872087478638f };

static const struct sos POWER[6] = {
    { 0.6578605771064758f, -1.295771598815918f, 0.6380202174186707f, 1.0f, -1.9795454740524292f,
      0.9797543883323669f },
    { 0.6855280995368958f, -1.295771598815918f, 0.6196836233139038f, 1.0f, -1.8308242559432983f,
      0.8473684191703796f },
    { 1.1444668769836426f, -1.295771598815918f, 0.33371278643608093f, 1.0f, -0.92982417345047f,
      0.10501450300216675f },
    { 1.295771598815918f, 0.015107899904251099f, 0.44052159786224365f, 1.0f, -0.009378300048410892f,
      0.0030036999378353357f },
    { 1.295771598815918f, -0.5103836059570312f, 0.06328000128269196f, 1.0f, 1.0182732343673706f,
      0.1963467001914978f },
    { 0.7774487733840942f, 1.295771598815918f, 0.5487715005874634f, 1.0f, 0.07546170055866241f,
      0.0036510000936686993f },
};

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the knob the presence section was computed for */
    struct fos gain_c;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pre[2], peak, lowpass, pres, power[6];
    struct fos_state gs, boost;
    struct stage st;
};

static void model_gain_set(struct fos *c, float g)
{
    c->b0 = g * 0.9951984882354736f;
    c->b1 = g * -0.9951984882354736f;
    c->a1 = -0.9903970956802368f;
}

static float clip(float x)
{
    return clip_pow_asym(x, 1.5f, 0.005859375f, 0.00390625f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow3, k[K_GAIN]);
    s->presence = (int)(k[K_PRESENCE] * 2.55f) < 5 ? 0.01f : st_taper(st_pow2, k[K_PRESENCE]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow3, k[K_BASS]);
    s->middle = k[K_MIDDLE] * 0.01f;
    s->treble = st_taper(st_pow3, k[K_TREBLE]);
    model_gain_set(&s->gain_c, s->gain);
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
    float gain = st_taper(st_pow3, io->knobs[K_GAIN]);
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
            model_gain_set(&s->gain_c, s->gain);
        x[i] = fos_run(&BOOST, &s->boost, fos_run(&s->gain_c, &s->gs, x[i]));
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
    for (int j = 0; j < 6; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 2.907360076904297f;
}

const struct wvfx_desc bog_xtblue_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x43),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bog XT Blue",
    .category = "Bog",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
