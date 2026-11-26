/*
 * Supero 2 OD (AMP › Supero): the original's Supero 2 OD amp (QME-200 V1.2.2, library entry 4.6, id 0x07000028) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. The shared pieces are in supero.h; this model: two channels (supero.h) from
 * the same input, Gain 1 / Tone 1 (A25) and Gain 2 / Tone 2 (A25 / B25), summed, x Volume x 2.976 while both Gains
 * stay at or below 0.0591, x 2.083 otherwise.
 * Init sets each tone stack's d0 as 1.043 - the taper (Process: Tone + 0.043), a rounding apart until Tone moves.
 */
#include "supero.h"

enum { K_GAIN1, K_TONE1, K_GAIN2, K_TONE2, K_VOLUME };

static const struct wvfx_param params[] = {
    SUPERO_KNOB("Gain 1", K_GAIN1), SUPERO_KNOB("Tone 1", K_TONE1), SUPERO_KNOB("Gain 2", K_GAIN2),
    SUPERO_KNOB("Tone 2", K_TONE2), SUPERO_KNOB("Volume", K_VOLUME),
};

struct channel {
    float gain, tone;           /* glided */
    struct fos_state pf[2];
    struct os8 nl;
    struct tone3 eq;
};

struct state {
    struct channel ch[2];
    float volume;               /* glided */
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    float a1 = st_taper(st_a25, k[K_TONE1]), a2 = st_taper(st_b25, k[K_TONE2]);
    s->ch[0].gain = st_taper(st_a25, k[K_GAIN1]);
    s->ch[0].tone = 1.0f - a1;
    s->ch[1].gain = st_taper(st_a25, k[K_GAIN2]);
    s->ch[1].tone = 1.0f - a2;
    s->volume = k[K_VOLUME] * 0.01f;
    supero_tone_set(&s->ch[0].eq, s->ch[0].tone);
    supero_tone_set(&s->ch[1].eq, s->ch[1].tone);
    s->ch[0].eq.a[0] = 1.0430083274841309f - a1;
    s->ch[1].eq.a[0] = 1.0430083274841309f - a2;
    return 0;
}

/*
 * Per channel stage by stage over the block (each glide advancing once per sample where it is used): x 5 x Gain and
 * the pre-filters, the oversampled stage (os8_block), the tone stack; then the sum. The same samples as sample by
 * sample.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain[2] = { st_taper(st_a25, io->knobs[K_GAIN1]), st_taper(st_a25, io->knobs[K_GAIN2]) };
    float tone[2] = { 1.0f - st_taper(st_a25, io->knobs[K_TONE1]), 1.0f - st_taper(st_b25, io->knobs[K_TONE2]) };
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float *p = io->io, x[2][WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < 2; c++) {
                s->ch[c].gain = st_glide(s->ch[c].gain, gain[c]);
                s->ch[c].tone = st_glide(s->ch[c].tone, tone[c]);
            }
            s->volume = st_glide(s->volume, volume);
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 2; c++)
            s->ch[c].gain = st_glide(s->ch[c].gain, gain[c]);
        s->volume = st_glide(s->volume, volume);
        int low = s->ch[0].gain <= 0.0591f && s->ch[1].gain <= 0.0591f;
        k[i] = (low ? 2.975656270980835f : 2.0829594135284424f) * s->volume;
        for (int c = 0; c < 2; c++) {
            struct channel *ch = &s->ch[c];
            float v = p[2 * i] * 5.0f * ch->gain;
            v = fos_run(&SUPERO_PF[0], &ch->pf[0], v);
            x[c][i] = fos_run(&SUPERO_PF[1], &ch->pf[1], v);
        }
    }
    for (int c = 0; c < 2; c++) {
        struct channel *ch = &s->ch[c];
        supero_nl_block(&ch->nl, x[c], n);
        for (int i = 0; i < n; i++) {
            ch->tone = st_glide(ch->tone, tone[c]);
            if (ch->tone != tone[c])
                supero_tone_set(&ch->eq, ch->tone);
            x[c][i] = tone3_run_std(&ch->eq, x[c][i]);
        }
    }
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = k[i] * (x[1][i] + x[0][i]);
}

const struct wvfx_desc supero_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x28),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Supero 2 OD",
    .category = "Supero",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
