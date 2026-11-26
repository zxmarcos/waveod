/*
 * Calif IIC+ 3 (AMP › Calif): the original's Calif IIC+ 3 amp (QME-200 V1.2.2, library entry 4.42, id 0x0700003B)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The IIC+ 1's path (calif_iic1.c) with its own tone stack, computed in double like the bright filter and
 * the Treble's taper, x 0.6 at the end.
 *
 * Gain, Volume and Treble glide 0.0001 per sample; Bass and Middle take effect at once. The tone and bright
 * coefficients stop one glide step short of the knobs.
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
    struct fos bright_c;
    float bright_a0;
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[6];
    struct fos_state bright;
    struct stage s1;
    struct tone3 tone;
};

/* the knobs, the Treble's taper in double */
static struct iic_knobs knobs(const float *k)
{
    struct iic_knobs t = iic_knobs(k);
    t.treble = (float)st_fmad((double)st_taper(st_pow3, k[CK_TREBLE]), 0.99, 0.01);
    return t;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    struct iic_knobs t = knobs(k);
    *s = (struct state){ 0 };
    s->gain = t.gain;
    s->presence = t.presence;
    s->volume = t.volume;
    s->bass = t.bass;
    s->middle = t.middle;
    s->treble = t.treble;
    tone_iic3(&s->tone, s->bass, s->middle, s->treble);
    iic_bright_set_d(&s->bright_c, &s->bright_a0, s->gain);
    iic_presence_set(&s->pres_c, s->presence);
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
    struct iic_knobs t = knobs(io->knobs);
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
    if (s->presence != t.presence_raw) {
        s->presence = t.presence;
        iic_presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE, &s->pre, x, n);
    sos_block_div(&IIC_LOWPASS, &s->lowpass, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, t.gain);
        s->treble = st_glide(s->treble, t.treble);
        if (s->bass != t.bass || s->middle != t.middle || s->treble != t.treble) {
            s->bass = t.bass;
            s->middle = t.middle;
            tone_iic3(&s->tone, s->bass, s->middle, s->treble);
        }
        if (s->gain != t.gain)
            iic_bright_set_d(&s->bright_c, &s->bright_a0, s->gain);
        x[i] = fos_run_div(&s->bright_c, s->bright_a0, &s->bright, tone3_run_std(&s->tone, x[i]));
    }
    stage_clip_block(&s->s1, x, n, iic_clip);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, t.volume);
        x[i] = x[i] * s->volume;
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 6; j++)
        sos_block(&IIC_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.6f;
}

const struct wvfx_desc calif_iic3_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x3B),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif IIC+ 3",
    .category = "Calif",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
