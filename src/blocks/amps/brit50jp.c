/*
 * Brit 50JP (AMP › Brit): the original's Brit 50JP amp (QME-200 V1.2.2, library entry 4.32, id 0x0700002F) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The Brit 50's two channels "jumped": the normal one
 * (Gain 1) and the bright one (Gain 2) side by side, summed into stage 1; the tone stack, presence and power amp
 * after them, x 0.64512 at the end. Mono: the left input, the result on both outputs.
 *
 * Unlike the Brit 50 and 50+, everything runs in single precision (the tone stack too, its sums in its own order),
 * and the original's quirks are kept:
 *   - Gain 1 is half the pow3 taper; Init sets the normal gain stage from the full taper x 0.49974245, the glide
 *     from then on from the half x 0.9994849 (the same value, rounded another way);
 *   - Volume jumps to the knob while the block plays (it glides only while bypassed);
 *   - the gain and tone coefficients stop one glide step short of the knob, as in the Brit 50.
 */
#include "brit.h"

static const struct wvfx_param params[] = {
    BRIT_PARAM("Gain 1", 40.0f, BK_GAIN), BRIT_PARAM("Presence", 50.0f, BK_PRESENCE),
    BRIT_PARAM("Volume", 50.0f, BK_VOLUME), BRIT_PARAM("Bass", 50.0f, BK_BASS),
    BRIT_PARAM("Middle", 50.0f, BK_MIDDLE), BRIT_PARAM("Treble", 50.0f, BK_TREBLE),
    BRIT_PARAM("Gain 2", 50.0f, BK_GAIN2),
};

struct state {
    float gain1, gain2, volume, middle, treble; /* glided */
    float bass, presence;                       /* the last ones the filters were computed for */
    struct fos nrm_gain_c;
    struct sos brt_gain_c, pres_c;
    struct sos_state nrm_pre1, nrm_pre2, nrm_lowpass, brt_pre1, brt_pre2, brt_gain_s, brt_eq1, brt_eq2;
    struct sos_state pres, power[6];
    struct fos_state nrm_gain_s;
    struct stage s1, s2;
    struct tone3 tone;
};

static void set_nrm_gain(struct state *s, float g, float k)
{
    s->nrm_gain_c = (struct fos){ g * k, g * -k, BRIT50_NRM_GAIN_A1 };
}

static void set_brt_gain(struct state *s)
{
    float g = s->gain2;
    s->brt_gain_c = (struct sos){ g * 0.9948728f, g * -1.892606f, g * 0.8977333f, 1.0f,
                                  BRIT50_BRT_GAIN_A1, BRIT50_BRT_GAIN_A2 };
}

/* the Brit 50 tone stack in single precision */
static void set_tone(struct state *s)
{
    tone_brit_f(&s->tone, &BRIT50_TS_F, s->bass, s->middle, s->treble);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct brit_knobs t = brit_knobs(k, st_pow3);
    *s = (struct state){ 0 };
    s->gain1 = t.gain * 0.5f;
    s->gain2 = t.gain2;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    set_nrm_gain(s, t.gain, 0.49974245f);
    set_brt_gain(s);
    set_tone(s);
    presence_set_f(&s->pres_c, s->presence);
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
    float gain1 = t.gain * 0.5f;
    float *p = io->io, x[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain1 = st_glide(s->gain1, gain1);
            s->gain2 = st_glide(s->gain2, t.gain2);
            s->volume = st_glide(s->volume, t.volume);
            s->middle = st_glide(s->middle, t.middle);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    /* playing, the original sets Volume to the knob after its glide every sample: the knob */
    s->volume = t.volume;
    if (s->presence != t.presence) {
        s->presence = t.presence;
        presence_set_f(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    /* the normal channel into y */
    for (int i = 0; i < n; i++)
        y[i] = x[i];
    sos_block(&BRIT50_NRM_PRE1, &s->nrm_pre1, y, n);
    sos_block(&BRIT50_NRM_PRE2, &s->nrm_pre2, y, n);
    for (int i = 0; i < n; i++) {
        s->gain1 = st_glide(s->gain1, gain1);
        if (s->gain1 != gain1)
            set_nrm_gain(s, s->gain1, 0.9994849f);
        y[i] = fos_run(&s->nrm_gain_c, &s->nrm_gain_s, y[i] * 0.15f);
    }
    sos_block_div(&BRIT50_NRM_LOWPASS, &s->nrm_lowpass, y, n);
    /* the bright channel into x */
    sos_block(&BRIT50_BRT_PRE1, &s->brt_pre1, x, n);
    sos_block(&BRIT50_BRT_PRE2, &s->brt_pre2, x, n);
    for (int i = 0; i < n; i++) {
        s->gain2 = st_glide(s->gain2, t.gain2);
        if (s->gain2 != t.gain2)
            set_brt_gain(s);
        x[i] = sos_run(&s->brt_gain_c, &s->brt_gain_s, x[i] * 0.15f);
    }
    sos_block_div(&BRIT50_BRT_EQ1, &s->brt_eq1, x, n);
    sos_block_div(&BRIT50_BRT_EQ2, &s->brt_eq2, x, n);
    for (int i = 0; i < n; i++)
        y[i] = x[i] + y[i];
    stage50_block(&s->s1, y, n);
    for (int i = 0; i < n; i++) {
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            set_tone(s);
        }
        y[i] = tone3_run_b1b0(&s->tone, y[i] * 0.02f);
    }
    stage2_block(&s->s2, y, n);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div_b1(&s->pres_c, &s->pres, y[i]);
    for (int j = 0; j < 6; j++)
        sos_block(&BRIT50_POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * 0.64511997f * s->volume;
}

const struct wvfx_desc brit50jp_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x2F),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Brit 50JP",
    .category = "Brit",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
