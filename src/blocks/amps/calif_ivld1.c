/*
 * Calif IV LD 1 (AMP › Calif): the original's Calif IV LD 1 amp (QME-200 V1.2.2, library entry 4.43, id 0x07000055)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, calif.h have the pieces):
 *   pre-filter    a fixed 2nd-order section, a fixed low pass
 *   tone stack    the IIC+'s, from Bass and Middle (pow3) and Treble (pow3 x 0.95 + 0.05)
 *   gain          x Gain (pow2)
 *   stage         soft clip at +-1.5 (k 1 / 128 above zero, 0.00634765625 below) on 4 points per sample, the FIR,
 *                 x Volume x 5
 *   presence      a high shelf from Presence (linear) times a fixed section
 *   power amp     nine fixed 2nd-order sections, x 2
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample; the tone coefficients stop one glide step short of
 * the knobs. Presence takes effect at once.
 */
#include "calif.h"

static const struct wvfx_param params[] = {
    CALIF_PARAM("Gain", 50.0f, CK_GAIN), CALIF_PARAM("Presence", 50.0f, CK_PRESENCE),
    CALIF_PARAM("Volume", 50.0f, CK_VOLUME), CALIF_PARAM("Bass", 50.0f, CK_BASS),
    CALIF_PARAM("Middle", 50.0f, CK_MIDDLE), CALIF_PARAM("Treble", 50.0f, CK_TREBLE),
};

static const struct sos PRE = { 2465.7332f, -4912.225f, 2446.5518f, 1.0f, -1.9563688f, 0.9569137f };

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the last one the filter was computed for */
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[9];
    struct stage s1;
    struct tone3 tone;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct iic_knobs t = ivld_knobs(k);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    tone_iic(&s->tone, s->bass, s->middle, s->treble);
    ivld_presence_set(&s->pres_c, k[CK_PRESENCE] * 0.016609639f);
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
    struct iic_knobs t = ivld_knobs(io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->volume = st_glide(s->volume, t.volume);
            s->bass = st_glide(s->bass, t.bass);
            s->middle = st_glide(s->middle, t.middle);
            s->treble = st_glide(s->treble, t.treble);
        }
        return;
    }
    if (s->presence != t.presence) {
        s->presence = t.presence;
        ivld_presence_set(&s->pres_c, s->presence * 1.660964f);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE, &s->pre, x, n);
    sos_block_div(&IVLD_LOWPASS, &s->lowpass, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        s->bass = st_glide(s->bass, t.bass);
        s->middle = st_glide(s->middle, t.middle);
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle)
            tone_iic(&s->tone, s->bass, s->middle, s->treble);
        x[i] = tone3_run_std(&s->tone, x[i]) * s->gain;
    }
    stage_clip_block(&s->s1, x, n, ivld_clip);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        x[i] = sos_run_b1(&s->pres_c, &s->pres, s->volume * 5.0f * x[i]);
    }
    for (int j = 0; j < 9; j++)
        sos_block(&IVLD_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] + x[i];
}

const struct wvfx_desc calif_ivld1_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x55),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif IV LD 1",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
