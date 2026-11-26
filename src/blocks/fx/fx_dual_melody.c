/*
 * Dual Melody (FX): the original's Dual Melody (QME-200 V1.2.2, library entry 1.2, id 0x01000023; its code calls it
 * the Harmony) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Two pitch-shifted voices,
 * mono (the left input; both outputs the same). Single precision with fixed-point phases.
 *
 * Each voice (Hi Pitch 0 .. 24 semitones, Low Pitch -24 .. 0, their whole values) writes x into its own 4096-sample
 * line read by two taps half a period apart (shift.h), each phase stepping 256 SHIFT_SEMI[pitch] per sample, the
 * reads windowed by the sine table and summed (/ 256 each, x 1.5 / 32768), then a 1st-order low pass. Out: Dry x +
 * 0.7 (Hi Vol hi + Low Vol lo). The knobs act at once. Knob word 3 is unused. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "shift.h"

static const struct wvfx_param params[] = {
    { "Hi Pitch", 0.0f, 24.0f, 1.0f, 12.0f, 0, 0x53, 0x00, 0, 0, 0 },
    { "Low Pitch", -24.0f, 0.0f, 1.0f, -12.0f, 1, 0x83, 0x00, 0, 0, 0 },
    { "Dry", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Hi Vol", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Low Vol", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
};

static const struct fos LOWPASS = { 0.5347844362258911f, 0.5347844362258911f, 0.06956888735294342f };

struct voice {
    int w, ph[2];
    struct fos_state lp;
    float *line;
};

struct state {
    struct voice v[2];
};

static float voice_run(struct voice *v, float x, int semi)
{
    float d = SHIFT_SEMI[semi + 24] * 256.0f;
    v->line[v->w] = x;
    int nw = v->w + 1 >= SHIFT_LINE ? 0 : v->w + 1;
    v->w = nw;
    v->ph[0] = shift_step(v->ph[0], d);
    v->ph[1] = shift_step(v->ph[1], d);
    float t0 = shift_read(v->line, nw, v->ph[0]) * 0.00390625f, t1 = shift_read(v->line, nw, v->ph[1]) * 0.00390625f;
    float y = st_fma(t1, (float)shift_window(v->ph[1]), t0 * (float)shift_window(v->ph[0]));
    return fos_run(&LOWPASS, &v->lp, (y * 1.5f) / 32768.0f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    (void)k;
    float *b = buffer;
    *s = (struct state){ { { .ph = { 0, 0x80000 }, .line = b }, { .ph = { 0, 0x80000 }, .line = b + SHIFT_LINE } } };
    __builtin_memset(b, 0, 2 * SHIFT_LINE * sizeof(float));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!io->enabled)
        return;
    float dry = k[2] * 0.01f, hv = k[4] * 0.01f, lv = k[5] * 0.01f;
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float x = p[0];
        float hi = voice_run(&s->v[0], x, (int)k[0]);
        float lo = voice_run(&s->v[1], x, (int)k[1]);
        p[0] = p[1] = st_fma(st_fma(lv, lo, hv * hi), 0.699999988079071f, dry * x);
    }
}

const struct wvfx_desc fx_dual_melody_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x102),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dual Melody",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 2 * SHIFT_LINE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
