/*
 * Ring Mod (FX): the original's Ring Mod (QME-200 V1.2.2, library entry 1.6, id 0x0100002F) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A ring modulator, mono (the left input; both outputs the same).
 * Single precision.
 *
 * The carrier cos(2 pi f j) (armcc's cosf), f = (2000 F^3 + 50 Fine) / 44100 + 0.0017 cycles per sample (F = B25(Freq), Fine through
 * B25 from its middle: -50 .. 50 at points 0 .. 255), j the sample count wrapped at 1 / f and rescaled when f
 * changes (the phase kept); x times it through a 1st-order low pass by Tone (y = t c x - (t - 1) y1, t = the B taper),
 * mixed into x by Mix (B25; x + Mix (y - x)), Mix gliding 0.0001 per sample. The Init sets f from Freq alone (as the
 * original). Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Freq", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Fine", -50.0f, 50.0f, 1.0f, 0.0f, 2, 0x5e, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    float mix, freq, j, y1;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    float t = st_taper(st_b25, k[1]);
    s->mix = st_taper(st_b25, k[0]);
    s->freq = st_fma(t * 2.2675736545352265e-05f, st_fma(t * t, 2000.0f, 50.0f), 0.001700680237263441f);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!io->enabled)
        return;
    float t = st_taper(st_b25, k[1]);
    float fine = st_b25[(int)st_fma(k[2], 2.549999952316284f, 127.5f)];
    float f = st_fma(st_fma(fine, 50.0f, ((t * t) * 2000.0f) * t), 2.2675736545352265e-05f, 0.001700680237263441f);
    float period = 1.0f / f, w = f * 6.2831854820251465f;
    float mix = st_taper(st_b25, k[0]), tone = st_taper(st_b, k[3]), tone_1 = tone + -1.0f;
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float x = p[0];
        if (s->freq != f) {
            s->j = (s->j * s->freq) * period;
            s->freq = f;
        }
        float c = st_cosf(w * s->j);
        float j = s->j + 1.0f;
        s->y1 = st_fma(c * x, tone, -(s->y1 * tone_1));                                      /* VFNMS */
        s->j = j - (j > period ? period : 0.0f);
        s->mix = st_glide(s->mix, mix);
        p[0] = p[1] = st_fma(s->mix, s->y1 - x, x);
    }
}

const struct wvfx_desc fx_ring_mod_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x106),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Ring Mod",
    .category = "Special",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
