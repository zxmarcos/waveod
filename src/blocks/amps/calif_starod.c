/*
 * Calif Star OD (AMP › Calif): the original's Calif Star OD amp (QME-200 V1.2.2, library entry 4.14, id 0x0700004A)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces; the Star CL has the same tone stack, gain and presence):
 *   pre-filter    a fixed 2nd-order section
 *   drive         a 1st-order section from Input (pow3_5), x 30
 *   tone stack    3rd order from Bass (pow4_5), Middle and Treble (pow3)
 *   gain          a 1st-order section from Gain (pow1_5), x 25
 *   presence      a 2nd-order section from Presence (pow3) and Volume (pow2_5), both held in 0.01..0.99
 *   stage         a soft clip 0.1 - 0.1 e^(-k |x|) whose k = 5 + 17 env follows the signal's envelope, x M =
 *                 max(1 / (1 + 3 k), 0.04); on 4 points per sample, the FIR, x 86.54099
 * No power amp. Input, Gain, Presence, Volume, Middle and Treble glide 0.0001 per sample; Bass takes effect at once.
 * The filters' coefficients stop one glide step short of the knobs.
 */
#include "calif.h"

enum { SK_INPUT, SK_GAIN };

static const struct wvfx_param params[] = {
    CALIF_PARAM("Input", 50.0f, 0), CALIF_PARAM("Gain", 50.0f, 1), CALIF_PARAM("Presence", 50.0f, 2),
    CALIF_PARAM("Volume", 50.0f, 3), CALIF_PARAM("Bass", 50.0f, 4), CALIF_PARAM("Middle", 50.0f, 5),
    CALIF_PARAM("Treble", 50.0f, 6),
};

static const struct sos PRE = { 61.19266891479492f, -114.80339813232422f, 53.621150970458984f, 1.0f,
                                -1.874851107597351f, 0.8751769065856934f };

struct state {
    float drive, gain, presence, volume, middle, treble;    /* glided */
    float bass;                                             /* the last one the tone stack was computed for */
    struct fos drive_c, gain_c;
    float drive_a0, gain_a0;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pre, pres;
    struct fos_state ds, gs;
    struct env_state env;
    struct stage st;
};

/* the drive section from the glided Input d */
static void drive_set(struct fos *c, float *a0, float d)
{
    c->b0 = d * 0.17958690226078033f;
    c->b1 = d * -0.17958690226078033f;
    *a0 = d + 0.18184509873390198f;
    c->a1 = st_fma(d, -0.9917814135551453f, -0.17732860147953033f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct star_knobs t = star_knobs(k, SK_GAIN, st_pow1_5, 1.0f);
    *s = (struct state){ 0 };
    s->drive = st_taper(st_pow3_5, k[SK_INPUT]);
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    drive_set(&s->drive_c, &s->drive_a0, s->drive);
    star_tone_set(&s->tone, t.bass, t.middle, t.treble, t.middle_raw * 2.243566996185109e-06f,
                  t.middle_raw * 1.7280649444728624e-06f, 1);
    star_gain_set(&s->gain_c, &s->gain_a0, t.gain, t.gain, -1.1764819622039795f, 0.987517774105072f);
    star_presence_set(&s->pres_c, t.volume, t.presence);
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
    struct star_knobs t = star_knobs(io->knobs, SK_GAIN, st_pow1_5, 1.0f);
    float drive = st_taper(st_pow3_5, io->knobs[SK_INPUT]);
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->presence = st_glide(s->presence, t.presence);
            s->drive = st_glide(s->drive, drive);
            s->volume = st_glide(s->volume, t.volume);
            s->middle = st_glide(s->middle, t.middle);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE, &s->pre, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        s->presence = st_glide(s->presence, t.presence);
        s->drive = st_glide(s->drive, drive);
        s->volume = st_glide(s->volume, t.volume);
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->drive != drive)
            drive_set(&s->drive_c, &s->drive_a0, s->drive);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            star_tone_set(&s->tone, s->bass, s->middle, s->treble, s->middle * 0.0002243566996185109f,
                          s->middle * 0.00017280649626627564f, 0);
        }
        if (s->gain != t.gain)
            star_gain_set(&s->gain_c, &s->gain_a0, s->gain, s->gain, -1.1764819622039795f, 0.987517774105072f);
        if (s->volume != t.volume || s->presence != t.presence)
            star_presence_set(&s->pres_c, s->volume, s->presence);
        float v = fos_run_div(&s->drive_c, s->drive_a0, &s->ds, x[i]) * 30.0f;
        v = tone3_run_std(&s->tone, v);
        v = fos_run_div(&s->gain_c, s->gain_a0, &s->gs, v) * 25.0f;
        v = sos_run_div(&s->pres_c, &s->pres, v);
        x[i] = v;
        k[i] = st_fma(star_env_run(&s->env, v), 17.0f, 5.0f);
    }
    stage_env_block(&s->st, x, k, n, 0.04f, 0.1f);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 86.5409927368164f;
}

const struct wvfx_desc calif_starod_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x4A),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif Star OD",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
