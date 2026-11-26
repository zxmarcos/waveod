/*
 * Brit 50 (AMP › Brit): the original's Brit 50 amp (QME-200 V1.2.2, library entry 4.30, id 0x0700002D) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 *
 * Per sample (brit.h has the pieces):
 *   pre-filters   two fixed 2nd-order sections, then x 0.15
 *   gain          a 1st-order high pass whose gain is the Gain knob (pow3 taper)
 *   "low pass"    a 2nd-order section the original gives its denominator as numerator too: it passes the signal
 *                 (to rounding), kept for the bits
 *   stage 1       soft clip at +-50 on 4 points per sample and the anti-aliasing FIR, x 0.02
 *   tone stack    3rd order, from Bass (pow2), Middle and Treble
 *   stage 2       soft clip at +-2, the same 4 points and FIR
 *   presence      a shelf moved by Presence (pow2)
 *   power amp     six fixed 2nd-order sections, x 0.96, x Volume
 * Gain, Volume, Middle and Treble glide 0.0001 per sample; the gain and tone coefficients follow the glide while it
 * moves and stop one step short of the knob (the original recomputes them only while the glided value differs from
 * the knob).
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 40.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
};

static const double GAIN_K[2] = { 0.9994849, -0.9994849 };

struct state {
    float gain, volume, middle, treble;         /* glided */
    float bass, presence;                       /* the last ones the filters were computed for */
    struct fos gain_c;
    struct sos pres_c;
    struct sos_state pre1, pre2, lowpass, pres, power[6];
    struct fos_state gain_s;
    struct stage s1, s2;
    struct tone3 tone;
};

static void set_gain(struct state *s)
{
    float b[2];
    gain_set_d(b, 2, s->gain, GAIN_K);
    s->gain_c = (struct fos){ b[0], b[1], BRIT50_NRM_GAIN_A1 };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct brit_knobs t = brit_knobs(k, st_pow3);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    set_gain(s);
    tone_brit_d(&s->tone, &BRIT50_TS_D, s->bass, s->middle, s->treble);
    presence_set(&s->pres_c, s->presence);
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
    struct brit_knobs t = brit_knobs(io->knobs, st_pow3);
    float *p = io->io, y[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
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
        presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    sos_block(&BRIT50_NRM_PRE1, &s->pre1, y, n);
    sos_block(&BRIT50_NRM_PRE2, &s->pre2, y, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        if (s->gain != t.gain)
            set_gain(s);
        y[i] = sos_run_div(&BRIT50_NRM_LOWPASS, &s->lowpass, fos_run(&s->gain_c, &s->gain_s, st_scale(y[i], 0.15)));
    }
    stage50_block(&s->s1, y, n);
    for (int i = 0; i < n; i++) {
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            tone_brit_d(&s->tone, &BRIT50_TS_D, s->bass, s->middle, s->treble);
        }
        y[i] = tone3_run(&s->tone, st_scale(y[i], 0.02));
    }
    stage2_block(&s->s2, y, n);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        vol[i] = s->volume;
        y[i] = sos_run_div_b1(&s->pres_c, &s->pres, y[i]);
    }
    for (int j = 0; j < 6; j++)
        sos_block(&BRIT50_POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 0.96f * vol[i];
}

const struct wvfx_desc brit50_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x2D),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 50",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
