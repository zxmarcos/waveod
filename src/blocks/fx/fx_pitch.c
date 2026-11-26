/*
 * Pitch (FX): the original's Pitch (QME-200 V1.2.2, library entry 1.3, id 0x01000024; its code calls it the
 * PBend) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A pitch bender, mono (the left
 * input; both outputs the same). Single precision with fixed-point phases.
 *
 * x into a 4096-sample line read by two taps half a period apart (shift.h), each phase stepping
 * 256 (lo + Range (hi - lo)) per sample (lo, hi = SHIFT_SEMI at Low Pitch -12 .. 0 and Hi Pitch 0 .. 12, their whole
 * values; Range / 100 bends between them), the reads windowed by the sine table and summed (x 0.00586 / 32768), then
 * a 1st-order low pass. Out: Dry x + 0.7 Wet y. The knobs act at once (Range moves the pitch at once too). Bypassed
 * it passes the signal.
 */
#include "../amps/amp.h"
#include "shift.h"

static const struct wvfx_param params[] = {
    { "Hi Pitch", 0.0f, 12.0f, 1.0f, 12.0f, 0, 0x54, 0x00, 0, 0, 0 },
    { "Low Pitch", -12.0f, 0.0f, 1.0f, 0.0f, 1, 0x55, 0x00, 0, 0, 0 },
    { "Wet", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Dry", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Range", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
};

/* the low pass: b0 = b1, a1 */
static const float LP_B = 0.5347844362258911f, LP_A1 = 0.06956888735294342f;

struct state {
    int w, ph[2];
    float x1, y1;
    float *line;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    (void)k;
    *s = (struct state){ .ph = { 0, 0x80000 }, .line = buffer };
    __builtin_memset(s->line, 0, SHIFT_LINE * sizeof(float));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!io->enabled)
        return;
    float hi = SHIFT_SEMI[(int)k[0] + 24], lo = SHIFT_SEMI[(int)k[1] + 24];
    float wet = k[2] * 0.00699999975040555f, dry = k[3] * 0.01f, range = k[4] * 0.01f;
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float l = lo * 256.0f;
        float d = st_fma(st_fma(hi, 256.0f, -l), range, l);                                   /* VFNMS, VFMA */
        float x = p[0];
        s->line[s->w] = x;
        int nw = s->w + 1 >= SHIFT_LINE ? 0 : s->w + 1;
        s->w = nw;
        s->ph[0] = shift_step(s->ph[0], d);
        s->ph[1] = shift_step(s->ph[1], d);
        float t0 = shift_read_b(s->line, nw, s->ph[0]), t1 = shift_read_b(s->line, nw, s->ph[1]);
        float u = st_fma(t1, (float)shift_window(s->ph[1]), t0 * (float)shift_window(s->ph[0]));
        u = (u * 0.005859375f) * 3.0517578125e-05f;
        float y = st_fms(LP_A1, s->y1, st_fma(u, LP_B, LP_B * s->x1));                         /* VFMS */
        s->x1 = u;
        s->y1 = y;
        p[0] = p[1] = st_fma(wet, y, x * dry);
    }
}

const struct wvfx_desc fx_pitch_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x103),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Pitch",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = SHIFT_LINE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
