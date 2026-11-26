/*
 * Voks 30N (AMP › Voks): the original's Voks 30N amp (QME-200 V1.2.2, library entry 4.8, id 0x07000011) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (amp.h, voks.h have the pieces):
 *   pre-filter    x 1.1, two fixed 2nd-order sections
 *   gain          Bright off: a 1st-order section from Gain (pow2 x 0.8); on: a 2nd-order one; x Volume
 *   boost         a 2nd-order section from Tone Cut (pow3)
 *   stage         the envelope-driven clip 0.1 - 0.1 e^(-k |x|), k = 0.3 + 1.5 env, x max(1 / (1 + 3 k), 0.17), on
 *                 4 points per sample, the FIR
 *   tone cut      a 2nd-order section from Tone Cut
 *   power amp     three fixed 2nd-order sections, x 48.787197
 * Gain, Tone Cut and Volume glide 0.0001 per sample; the coefficients stop one glide step short of the knobs.
 * Turning Bright clears both gain sections' state.
 */
#include "voks.h"

enum { K_GAIN, K_TONECUT, K_VOLUME, K_BRIGHT };

static const struct wvfx_param params[] = {
    VOKS_PARAM("Gain", 30.0f, K_GAIN), VOKS_PARAM("Tone Cut", 50.0f, K_TONECUT), VOKS_PARAM("Volume", 50.0f, K_VOLUME),
    { "Bright", 0.0f, 1.0f, 1.0f, 0.0f, K_BRIGHT, 0, 0, 0, "Off\0On\0", 0 },
};

static const struct sos POWER[3] = {
    { 0.6760959029197693f, -1.3474762439727783f, 0.6714524030685425f, 1.0f, -1.9961987733840942f,
      0.9963130950927734f },
    { 1.3474762439727783f, -1.3353482484817505f, 0.09363730251789093f, 1.0f, -0.9334179162979126f,
      0.02411000058054924f },
    { 1.0947363376617432f, 1.3474762439727783f, 0.6300860047340393f, 1.0f, 1.2444020509719849f,
      0.6411805748939514f },
};

#define BOOST_C1 -8.30482006072998f
#define BOOST_C0 2.491446018218994f
#define TONECUT_C1 -1.0796266794204712f

struct state {
    float gain, tonecut, volume;        /* glided */
    float bright;                       /* the knob the gain sections' state was kept for */
    struct voks_gain gain_c;
    struct sos boost_c, tonecut_c;
    struct sos_state pre[2], brt, boost, tc, power[3];
    struct fos_state nrm_s;
    struct env_state env;
    struct stage st;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    float raw = st_taper(st_pow2, k[K_GAIN]);
    *s = (struct state){ 0 };
    s->gain = raw * 0.8f;
    s->tonecut = st_taper(st_pow3, k[K_TONECUT]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bright = k[K_BRIGHT];
    voks_gain_set(&s->gain_c, s->gain, raw, VOKS_GAIN_INIT);
    voks_boost_set(&s->boost_c, s->tonecut, BOOST_C1, BOOST_C0);
    voks_tonecut_set(&s->tonecut_c, s->tonecut, TONECUT_C1);
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
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]) * 0.8f;
    float tonecut = st_taper(st_pow3, io->knobs[K_TONECUT]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bright = io->knobs[K_BRIGHT];
    if (bright != s->bright) {
        s->bright = bright;
        s->nrm_s = (struct fos_state){ 0 };
        s->brt = (struct sos_state){ 0 };
    }
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES], tc[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->tonecut = st_glide(s->tonecut, tonecut);
            s->volume = st_glide(s->volume, volume);
        }
        return;
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    sos_block(&VOKS_PRE[0], &s->pre[0], x, n);
    sos_block(&VOKS_PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->tonecut = st_glide(s->tonecut, tonecut);
        s->volume = st_glide(s->volume, volume);
        tc[i] = s->tonecut;
        if (s->gain != gain)
            voks_gain_set(&s->gain_c, s->gain, s->gain, VOKS_GAIN_PROCESS);
        if (s->tonecut != tonecut)
            voks_boost_set(&s->boost_c, s->tonecut, BOOST_C1, BOOST_C0);
        float v;
        if (bright == 0.0f)
            v = fos_run_div(&s->gain_c.nrm, s->gain_c.nrm_a0, &s->nrm_s, x[i]);
        else
            v = sos_run_div(&s->gain_c.brt, &s->brt, x[i]);
        v = sos_run(&s->boost_c, &s->boost, v * s->volume);
        x[i] = v;
        k[i] = st_fma(voks_env_run(&s->env, v), 1.5f, 0.3f);
    }
    stage_env_block(&s->st, x, k, n, 0.17f, 0.1f);
    for (int i = 0; i < n; i++) {
        if (tc[i] != tonecut)                   /* the Tone Cut's section follows the glide as the boost did */
            voks_tonecut_set(&s->tonecut_c, tc[i], TONECUT_C1);
        x[i] = sos_run_div_b1(&s->tonecut_c, &s->tc, x[i]);
    }
    for (int j = 0; j < 3; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 48.78719711303711f;
}

const struct wvfx_desc voks_30n_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x11),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Voks 30N",
    .category = "Voks",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
