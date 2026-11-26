/*
 * Bog SV CL (AMP › Bog): the original's Bog SV CL amp (QME-200 V1.2.2, library entry 4.15, id 0x0700001A) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, bog.h have the pieces):
 *   pre-filter    x 1.1, two fixed 2nd-order sections
 *   tone stack    3rd order from Bass and Treble (pow3)
 *   gain          Bright off: x Gain (pow2); on: a 1st-order section from Gain; x 5
 *   pre amp       three fixed 2nd-order sections, x Volume
 *   stage         the envelope-driven clip 0.05 - 0.05 e^(-k |x|), k = 1 + env, x max(1 / (1 + 3 k), 0.1), on 4
 *                 points per sample, the FIR
 *   presence      a 2nd-order section from Presence (pow3)
 *   power amp     three fixed 2nd-order sections, x 119.52
 * Gain, Volume, Bass and Treble glide 0.0001 per sample, Presence takes effect at once; the coefficients stop one
 * glide step short of the knobs. Turning Bright clears the gain section's state.
 */
#include "bog.h"

enum { K_GAIN, K_PRESENCE, K_VOLUME, K_BASS, K_TREBLE, K_BRIGHT };

static const struct wvfx_param params[] = {
    BOG_PARAM("Gain", 30.0f, K_GAIN), BOG_PARAM("Presence", 50.0f, K_PRESENCE), BOG_PARAM("Volume", 50.0f, K_VOLUME),
    BOG_PARAM("Bass", 50.0f, K_BASS), BOG_PARAM("Treble", 50.0f, K_TREBLE),
    { "Bright", 0.0f, 1.0f, 1.0f, 0.0f, K_BRIGHT, 0, 0, 0, "Off\0On\0", 0 },
};

static const struct sos PRE[2] = {
    { 7.67387056350708f, -8.255764961242676f, 0.7065004110336304f, 1.0f, -1.130622148513794f, 0.1543916016817093f },
    { 8.255764961242676f, 4.354709625244141f, 9.800000043469481e-06f, 1.0f, 0.4710494875907898f,
      1.2999999853491317e-05f },
};

static const struct sos PREAMP[3] = {
    { 0.8493912816047668f, -1.1552813053131104f, 0.30599960684776306f, 1.0f, -1.2806158065795898f,
      0.2861480116844177f },
    { 1.1552813053131104f, -0.47466689348220825f, 0.0725288987159729f, 1.0f, -0.671873927116394f,
      0.29123610258102417f },
    { 1.1552813053131104f, 0.936119794845581f, 0.020260000601410866f, 1.0f, 0.9394590854644775f,
      0.1411719024181366f },
};

static const struct sos POWER[3] = {
    { 0.766695499420166f, -1.5165369510650635f, 0.7501087188720703f, 1.0f, -1.986356496810913f, 0.9866508841514587f },
    { 1.5165369510650635f, -1.4673292636871338f, 0.13257010281085968f, 1.0f, -1.0681705474853516f,
      0.23304720222949982f },
    { 1.5165369510650635f, -0.7325053811073303f, 0.33978429436683655f, 1.0f, 0.03395229950547218f,
      0.2648923993110657f },
};

struct state {
    float gain, volume, bass, treble;   /* glided */
    float presence, bright;             /* the knobs the presence section and the gain state were set for */
    struct tone3 tone;
    struct fos gain_c;
    float gain_a0;
    struct sos pres_c;
    struct sos_state pre[2], preamp[3], pres, power[3];
    struct fos_state gs;
    struct env_state env;
    struct stage st;
};

/* the gain section from the glided Gain g */
static void model_gain_set(struct fos *c, float *a0, float g)
{
    float g2 = g * g;
    c->b0 = st_fma(g, -1.1133790016174316f, g2);
    c->b1 = st_fma(g, 0.8866212964057922f, -g2);
    *a0 = (-0.11337870359420776f - g) + g2;
    c->a1 = (g + -0.11337870359420776f) - g2;
}

/* the tone stack from Bass b and Treble t */
static void tone_set(struct tone3 *o, float b, float t)
{
    float tb = t * b, tb3 = tb * 3.0f, t1 = t * 0.001613107044249773f, t2 = t * 0.0016106199473142624f;
    o->b[0] = (st_fma(b, 0.11946389824151993f, 0.0001851420965977013f) + t1) + tb;
    o->b[1] = st_fma(b, -0.31614941358566284f, -0.00013371379463933408f) - (t2 + tb3);
    o->b[2] = (st_fma(b, 0.2761724889278412f, -0.0001851420965977013f) - t1) + tb3;
    o->b[3] = (st_fma(b, -0.07948697358369827f, 0.00013371379463933408f) - tb) + t2;
    o->a[0] = st_fma(b, 1.3537169694900513f, 0.0021761239040642977f);
    o->a[1] = st_fma(b, -3.550403118133545f, -0.0013658760581165552f);
    o->a[2] = st_fma(b, 3.041918992996216f, -0.002174959983676672f);
    o->a[3] = st_fma(b, -0.8452335000038147f, 0.001367039978504181f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2, k[K_GAIN]);
    s->presence = st_taper(st_pow3, k[K_PRESENCE]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow3, k[K_BASS]);
    s->treble = st_taper(st_pow3, k[K_TREBLE]);
    s->bright = k[K_BRIGHT];
    tone_set(&s->tone, s->bass, s->treble);
    model_gain_set(&s->gain_c, &s->gain_a0, s->gain);
    bog_presence_set(&s->pres_c, s->presence);
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
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]);
    float presence = st_taper(st_pow3, io->knobs[K_PRESENCE]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow3, io->knobs[K_BASS]);
    float treble = st_taper(st_pow3, io->knobs[K_TREBLE]);
    float bright = io->knobs[K_BRIGHT];
    if (bright != s->bright) {
        s->bright = bright;
        s->gs = (struct fos_state){ 0 };
    }
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->bass = st_glide(s->bass, bass);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    if (s->presence != presence) {
        s->presence = presence;
        bog_presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    sos_block(&PRE[0], &s->pre[0], x, n);
    sos_block(&PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->bass = st_glide(s->bass, bass);
        s->treble = st_glide(s->treble, treble);
        if (s->gain != gain)
            model_gain_set(&s->gain_c, &s->gain_a0, s->gain);
        if (s->treble != treble || s->bass != bass)
            tone_set(&s->tone, s->bass, s->treble);
        float v = tone3_run_std(&s->tone, x[i]);
        v = s->bright == 0.0f ? v * s->gain : fos_run_div(&s->gain_c, s->gain_a0, &s->gs, v);
        x[i] = v * 5.0f;
    }
    for (int j = 0; j < 3; j++)
        sos_block(&PREAMP[j], &s->preamp[j], x, n);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        x[i] = x[i] * s->volume;
        k[i] = bog_env_run(&s->env, x[i]) + 1.0f;
    }
    stage_env_block(&s->st, x, k, n, 0.1f, 0.05f);
    for (int i = 0; i < n; i++)
        x[i] = sos_run_div_b1(&s->pres_c, &s->pres, x[i]);
    for (int j = 0; j < 3; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 119.52000427246094f;
}

const struct wvfx_desc bog_svcl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x1A),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bog SV CL",
    .category = "Bog",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
