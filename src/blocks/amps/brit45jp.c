/*
 * Brit 45JP (AMP › Brit): the original's Brit 45JP amp (QME-200 V1.2.2, library entry 4.29, id 0x0700002C) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The Brit 45's two channels "jumped" after the
 * shared pre-filters: the normal one (Gain 1: the 45's gain stage and low pass) and the bright one (Gain 2: the
 * 45+'s gain stage and pre-EQ), summed, x 0.13; then the 45s' stages, the tone stack in double, presence and power
 * amp, x 0.4 at the end. Mono: the left input, the result on both outputs.
 *
 * Gain 1 is half the pow3 taper. Only Volume and Treble glide (0.0001 per sample); the rest take effect at once.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain 1", 50.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
    BRIT_PARAM("Gain 2", 50.0f, BK_GAIN2),
};

struct state {
    float volume, treble;                       /* glided */
    float gain1, gain2, bass, middle, presence; /* the last ones the filters were computed for */
    struct fos nrm_gain_c;
    struct sos pres_c;
    struct sos_state pre[3], nrm_lowpass, brt_eq1, brt_eq2, pres, power[4];
    struct fos_state nrm_gain_s;
    struct stage s1, s2;
    struct tone3 brt_gain, tone;
};

/* the normal channel's gain stage, its numerator in double */
static void set_gain1(struct state *s)
{
    float b[2];
    static const double K[2] = { 0.9994334, -0.9994334 };
    gain_set_d(b, 2, s->gain1, K);
    s->nrm_gain_c = (struct fos){ b[0], b[1], BRIT45_NRM_GAIN_A1 };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct brit_knobs t = brit_knobs_t(k, st_pow3, st_pow3);
    *s = (struct state){ 0 };
    s->gain1 = t.gain * 0.5f;
    s->gain2 = t.gain2;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    set_gain1(s);
    brit45_brt_gain(&s->brt_gain, s->gain2);
    tone_brit_d(&s->tone, &BRIT45_TS_D, s->bass, s->middle, s->treble);
    presence45_set_d(&s->pres_c, s->presence);
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
    float gain1 = t.gain * 0.5f;
    float *p = io->io, x[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->volume = st_glide(s->volume, t.volume);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    if (s->gain1 != gain1) {
        s->gain1 = gain1;
        set_gain1(s);
    }
    if (s->gain2 != t.gain2) {
        s->gain2 = t.gain2;
        brit45_brt_gain(&s->brt_gain, s->gain2);
    }
    if (s->presence != t.presence) {
        s->presence = t.presence;
        presence45_set_d(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    for (int j = 0; j < 3; j++)
        sos_block(&BRIT45_PRE[j], &s->pre[j], x, n);
    /* the bright channel into y, the normal one added */
    for (int i = 0; i < n; i++)
        y[i] = tone3_run_std(&s->brt_gain, x[i]);
    sos_block(&BRIT45_BRT_EQ1, &s->brt_eq1, y, n);
    sos_block(&BRIT45_BRT_EQ2, &s->brt_eq2, y, n);
    for (int i = 0; i < n; i++) {
        float nr = sos_run_div(&BRIT45_NRM_LOWPASS, &s->nrm_lowpass, fos_run(&s->nrm_gain_c, &s->nrm_gain_s, x[i]));
        y[i] = st_scale(nr + y[i], 0.13);
    }
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

const struct wvfx_desc brit45jp_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x2C),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 45JP",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
