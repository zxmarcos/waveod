/*
 * Calif IV LD 2 (AMP › Calif): the original's Calif IV LD 2 amp (QME-200 V1.2.2, library entry 4.44, id 0x07000056)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs.
 *
 * The IV LD 1 (calif_ivld1.c) with the IIC+ 3's tone stack in double, Bass and Middle on other tapers (in double) and
 * taking effect at once, x 2.1 at the end. Gain, Volume and Treble glide 0.0001 per sample; Presence, Bass and
 * Middle take effect at once.
 */
#include "calif.h"

static const struct wvfx_param params[] = {
    CALIF_PARAM("Gain", 50.0f, CK_GAIN), CALIF_PARAM("Presence", 50.0f, CK_PRESENCE),
    CALIF_PARAM("Volume", 50.0f, CK_VOLUME), CALIF_PARAM("Bass", 50.0f, CK_BASS),
    CALIF_PARAM("Middle", 50.0f, CK_MIDDLE), CALIF_PARAM("Treble", 50.0f, CK_TREBLE),
};

static const struct sos PRE = { 2465.7332f, -4912.225f, 2446.5518f, 1.0f, -1.9563688f, 0.9569137f };

struct state {
    float gain, volume, treble;                 /* glided */
    float bass, middle, presence;               /* the last ones the filters were computed for */
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[9];
    struct stage s1;
    struct tone3 tone;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct iic_knobs t = ivld2_knobs(k);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    tone_iic3(&s->tone, s->bass, s->middle, s->treble);
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
    struct iic_knobs t = ivld2_knobs(io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, t.gain);
            s->volume = st_glide(s->volume, t.volume);
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
        s->treble = st_glide(s->treble, t.treble);
        if (s->treble != t.treble || s->bass != t.bass || s->middle != t.middle) {
            s->bass = t.bass;
            s->middle = t.middle;
            tone_iic3(&s->tone, s->bass, s->middle, s->treble);
        }
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
        p[2 * i] = p[2 * i + 1] = x[i] * 2.1f;
}

const struct wvfx_desc calif_ivld2_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x56),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif IV LD 2",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
