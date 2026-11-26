/*
 * Brit 45 (AMP › Brit): the original's Brit 45 amp (QME-200 V1.2.2, library entry 4.27, id 0x0700002A) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (brit.h has the pieces):
 *   pre-filters   three fixed 2nd-order sections
 *   gain          a 1st-order high pass whose gain is the Gain knob (pow3 taper)
 *   low pass      a fixed 2nd-order section, then x 0.13
 *   stage 1       soft clip at +-40 (40 - 40 e^(-0.2 |x|)) on 4 points per sample, the FIR, x 0.1
 *   tone stack    3rd order, from Bass (pow3), Middle and Treble
 *   stage 2       soft clip at +-1.5 (1.5 - 1.5 (1 - 7 |x| / 1024)^1024), the same 4 points and FIR
 *   presence      a shelf moved by Presence (pow2)
 *   power amp     four fixed 2nd-order sections, then Volume x 1.3979349
 * Only Volume and Treble glide (0.0001 per sample); Gain, Bass, Middle and Presence take effect at once. The tone
 * coefficients stop one Treble glide step short of the knob, as in the Brit 50.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 25.0f, BK_GAIN), BRIT_PARAM("Presence", 65.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 45.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 65.0f, BK_TREBLE),
};

struct state {
    float volume, treble;                       /* glided */
    float gain, bass, middle, presence;         /* the last ones the filters were computed for */
    struct fos gain_c;
    struct sos pres_c;
    struct sos_state pre[3], lowpass, pres, power[4];
    struct fos_state gain_s;
    struct stage s1, s2;
    struct tone3 tone;
};

/* the presence shelf from k = 2^(knob x 1.8851943) */
static void set_presence(struct state *s)
{
    presence45_shelf(&s->pres_c, st_exp2f(s->presence * 1.8851943f));
}

static void set_gain(struct state *s)
{
    s->gain_c = (struct fos){ s->gain * BRIT45_NRM_GAIN_K, s->gain * -BRIT45_NRM_GAIN_K, BRIT45_NRM_GAIN_A1 };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct brit_knobs t = brit_knobs_t(k, st_pow3, st_pow3);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    set_gain(s);
    tone_brit_f(&s->tone, &BRIT45_TS_F, s->bass, s->middle, s->treble);
    set_presence(s);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (Gain and Presence can only change before the block's first sample; the tone stack and the
 * volume follow their glides sample by sample), and keeps its state in registers over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    struct brit_knobs t = brit_knobs_t(io->knobs, st_pow3, st_pow3);
    float *p = io->io, y[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->volume = st_glide(s->volume, t.volume);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    if (s->gain != t.gain) {
        s->gain = t.gain;
        set_gain(s);
    }
    if (s->presence != t.presence) {
        s->presence = t.presence;
        set_presence(s);
    }
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    for (int j = 0; j < 3; j++)
        sos_block(&BRIT45_PRE[j], &s->pre[j], y, n);
    for (int i = 0; i < n; i++)
        y[i] = fos_run(&s->gain_c, &s->gain_s, y[i]);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div(&BRIT45_NRM_LOWPASS, &s->lowpass, y[i]) * 0.13f;
    stage40_block(&s->s1, y, n);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            s->middle = t.middle;
            tone_brit_f(&s->tone, &BRIT45_TS_F, s->bass, s->middle, s->treble);
        }
        y[i] = tone3_run_b1b0(&s->tone, y[i] * 0.1f);
        vol[i] = s->volume;
    }
    stage15_block(&s->s2, y, n);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div_b1(&s->pres_c, &s->pres, y[i]);
    for (int j = 0; j < 4; j++)
        sos_block(&BRIT45_POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 1.3979349f * y[i];
}

const struct wvfx_desc brit45_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x2A),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 45",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
