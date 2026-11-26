/*
 * Calif Star CL (AMP › Calif): the original's Calif Star CL amp (QME-200 V1.2.2, library entry 4.13, id 0x07000019)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces):
 *   pre-filter    a fixed 2nd-order section
 *   tone stack    3rd order from Bass (pow4_5), Middle and Treble (pow3)
 *   gain          a 1st-order section from Gain (pow3 x 0.7), x 15
 *   presence      a 2nd-order section from Presence (pow3) and Volume (pow2_5), both held in 0.01..0.99
 *   stage         a soft clip whose drive follows the signal: an envelope (|x| through a fixed low pass) and Gain
 *                 set k = 5 + 5 Gain + 15 env, the clip is M C (1 - e^(-k |x|)) with C = 0.2 - 0.1 Gain and
 *                 M = max(1 / (1 + 3 k), 0.4); on 4 points per sample, the FIR
 *   power amp     three fixed 2nd-order sections, x 9.425052
 * Gain, Presence, Volume, Middle and Treble glide 0.0001 per sample; Bass takes effect at once. The filters'
 * coefficients stop one glide step short of the knobs.
 */
#include "calif.h"

static const struct wvfx_param params[] = {
    CALIF_PARAM("Gain", 40.0f, CK_GAIN), CALIF_PARAM("Presence", 50.0f, CK_PRESENCE),
    CALIF_PARAM("Volume", 50.0f, CK_VOLUME), CALIF_PARAM("Bass", 50.0f, CK_BASS),
    CALIF_PARAM("Middle", 50.0f, CK_MIDDLE), CALIF_PARAM("Treble", 50.0f, CK_TREBLE),
};

static const struct sos PRE = { 61.19266891479492f, -114.80339813232422f, 53.621150970458984f, 1.0f,
                                -1.874851107597351f, 0.8751769065856934f };

static const struct sos POWER[3] = {
    { 0.6973140835762024f, -1.3899519443511963f, 0.6928051710128784f, 1.0f, -1.9945094585418701f, 0.9945964813232422f },
    { 0.821639895439148f, -1.6368035078048706f, 0.8152151703834534f, 1.0f, -1.9943263530731201f, 0.9945417046546936f },
    { 1.6368035078048706f, -0.14749589562416077f, 0.013811499811708927f, 1.0f, -0.08207479864358902f,
      0.0063461000099778175f },
};

struct state {
    float gain, presence, volume, middle, treble;   /* glided */
    float bass;                                     /* the last one the tone stack was computed for */
    struct tone3 tone;
    struct fos gain_c;
    float gain_a0;
    struct sos pres_c;
    struct sos_state pre, pres, power[3];
    struct fos_state gs;
    struct env_state env;
    struct stage st;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct star_knobs t = star_knobs(k, CK_GAIN, st_pow3, 0.7f);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    star_tone_set(&s->tone, t.bass, t.middle, t.treble, t.middle_raw * 2.243566996185109e-06f,
                  t.middle_raw * 1.7280649444728624e-06f, 1);
    star_gain_set(&s->gain_c, &s->gain_a0, t.gain, t.gain_raw, -0.8235373497009277f, 0.691262423992157f);
    star_presence_set(&s->pres_c, t.volume, t.presence);
    return 0;
}

/*
 * The chain of the header stage by stage over the block (each glide once per sample where it is used): the
 * pre-filter, then per sample the glides, the coefficients following them, the tone stack, the gain section x 15
 * and the presence; the stage (star_cl_stage_block, the sample's Gain); the power amp. The same samples as sample by
 * sample.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    struct star_knobs t = star_knobs(io->knobs, CK_GAIN, st_pow3, 0.7f);
    float *p = io->io, x[WVFX_MAX_FRAMES], g[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->presence = st_glide(s->presence, t.presence);
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
        s->volume = st_glide(s->volume, t.volume);
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            star_tone_set(&s->tone, s->bass, s->middle, s->treble, s->middle * 0.0002243566996185109f,
                          s->middle * 0.00017280649626627564f, 0);
        }
        if (s->gain != t.gain)
            star_gain_set(&s->gain_c, &s->gain_a0, s->gain, s->gain, -1.1764819622039795f, 0.987517774105072f);
        if (s->volume != t.volume || s->presence != t.presence)
            star_presence_set(&s->pres_c, s->volume, s->presence);
        float v = tone3_run_std(&s->tone, x[i]);
        v = fos_run_div(&s->gain_c, s->gain_a0, &s->gs, v) * 15.0f;
        x[i] = sos_run_div(&s->pres_c, &s->pres, v);
        g[i] = s->gain;
    }
    star_cl_stage_block(&s->st, &s->env, x, g, n);
    for (int j = 0; j < 3; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 9.42505168914795f;
}

const struct wvfx_desc calif_starcl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x19),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif Star CL",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
