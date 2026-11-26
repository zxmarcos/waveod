/*
 * Supero 2 CL (AMP › Supero): the original's Supero 2 CL amp (QME-200 V1.2.2, library entry 4.5, id 0x0700000F) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The tone stack's coefficients in double, the rest in single precision. The shared pieces are in supero.h;
 * this model: one channel (supero.h), x Volume x 9.6 (6.72 once Gain passes 0.0591).
 * The original runs a second channel fed with 0 (always +0 out) and adds it to the first; only its effect is kept:
 * + 0 turns a -0 result into +0.
 */
#include "supero.h"

enum { K_GAIN, K_TONE, K_VOLUME };

static const struct wvfx_param params[] = {
    SUPERO_KNOBD("Gain", K_GAIN, 30.0f), SUPERO_KNOB("Tone", K_TONE), SUPERO_KNOB("Volume", K_VOLUME),
};

struct state {
    float gain, tone, volume;   /* glided */
    struct fos_state pf[2];
    struct os8 nl;
    struct tone3 eq;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[K_GAIN]);
    s->tone = 1.0f - st_taper(st_a25, k[K_TONE]);
    s->volume = k[K_VOLUME] * 0.01f;
    supero_tone_set_d(&s->eq, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_a25, io->knobs[K_GAIN]);
    float tone = 1.0f - st_taper(st_a25, io->knobs[K_TONE]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES], lv[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->tone = st_glide(s->tone, tone);
            s->volume = st_glide(s->volume, volume);
        }
        return;
    }
    /* stage by stage over the block (each glide once per sample where it is used): the same samples */
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        float level = (double)s->gain > 0.0591 ? 6.720000267028809f : 9.600000381469727f;
        lv[i] = level * s->volume;
        float v = p[2 * i] * 5.0f * s->gain;
        v = fos_run(&SUPERO_PF[0], &s->pf[0], v);
        x[i] = fos_run(&SUPERO_PF[1], &s->pf[1], v);
    }
    supero_nl_block(&s->nl, x, n);
    for (int i = 0; i < n; i++) {
        s->tone = st_glide(s->tone, tone);
        if (s->tone != tone)
            supero_tone_set_d(&s->eq, s->tone);
        p[2 * i] = p[2 * i + 1] = lv[i] * (0.0f + tone3_run_std(&s->eq, x[i]));
    }
}

const struct wvfx_desc supero_cl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x0F),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Supero 2 CL",
    .category = "Supero",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
