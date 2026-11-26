/*
 * Brit 45+ (AMP › Brit): the original's Brit 45+ amp (QME-200 V1.2.2, library entry 4.28, id 0x0700002B) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The Brit 45's bright channel: the same pre-filters,
 * stages, presence and power amp, with a 3rd-order gain stage whose coefficients follow Gain (and its square), two
 * pre-EQ sections in place of the low pass, the tone stack in double, x 0.4 at the end. Mono: the left input, the
 * result on both outputs.
 *
 * Only Volume and Treble glide (0.0001 per sample); Gain, Bass, Middle and Presence take effect at once.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain", 45.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
};

struct state {
    float volume, treble;                       /* glided */
    float gain, bass, middle, presence;         /* the last ones the filters were computed for */
    struct sos pres_c;
    struct sos_state pre[3], pre_eq1, pre_eq2, pres, power[4];
    struct stage s1, s2;
    struct tone3 gain_s, tone;
};

static void set_gain(struct state *s)
{
    brit45_brt_gain(&s->gain_s, s->gain);
}

static void set_presence(struct state *s)
{
    presence45_set_d(&s->pres_c, s->presence);
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
    tone_brit_d(&s->tone, &BRIT45_TS_D, s->bass, s->middle, s->treble);
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
        y[i] = tone3_run_std(&s->gain_s, y[i]);
    sos_block(&BRIT45_BRT_EQ1, &s->pre_eq1, y, n);
    sos_block(&BRIT45_BRT_EQ2, &s->pre_eq2, y, n);
    for (int i = 0; i < n; i++)
        y[i] = st_scale(y[i], 0.13);
    stage40_block(&s->s1, y, n);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            s->middle = t.middle;
            tone_brit_d(&s->tone, &BRIT45_TS_D, s->bass, s->middle, s->treble);
        }
        y[i] = tone3_run(&s->tone, st_scale(y[i], 0.1));
        vol[i] = s->volume;
    }
    stage15_block(&s->s2, y, n);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div_b1(&s->pres_c, &s->pres, y[i]);
    for (int j = 0; j < 4; j++)
        sos_block(&BRIT45_POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 0.4f * vol[i];
}

const struct wvfx_desc brit45p_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x2B),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 45+",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
