/*
 * Octaver (FX): the original's Octaver (QME-200 V1.2.2, library entry 1.1, id 0x01000021; its code calls it the
 * Clean Octa) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). An octave down and an octave
 * up, mono (the left input; both outputs the same). Single precision with fixed-point phases.
 *
 * x goes into a 4096-sample line read by five taps at 20-bit fixed-point phases (shift.h), each wrapped into
 * 0 .. 2^20 and windowed by the sine table: three a third of a period apart stepping +128 per sample (the delay
 * grows half a sample per sample: an octave down) and two half a period apart stepping -256 (an octave up). Each tap
 * reads between two samples by its 8 low phase bits. Out: Dry x + Low Oct (sum of the three) / 2^23 / 100 + High
 * Oct the 1st-order low pass of (sum of the two) / 2^22.4 (x 1.788e-7). Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "shift.h"

static const struct wvfx_param params[] = {
    { "Low Oct", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "High Oct", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Dry", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
};

/* the octave up's low pass: b0 = b1, a1 */
static const float LP_B = 0.5347844362258911f, LP_A1 = 0.06956888735294342f;

struct state {
    int w, down[3], up[2];              /* the write position, the taps' phases */
    float x1, y1;                       /* the low pass */
    float *line;                        /* the block's buffer */
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    (void)k;
    *s = (struct state){ .down = { 0, 0x55555, 0xaaaaa }, .up = { 0, 0x80000 }, .line = buffer };
    __builtin_memset(s->line, 0, SHIFT_LINE * sizeof(float));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!io->enabled)
        return;
    float low = k[0] * 1.19209286886246e-09f, high = k[1] * 0.01f, dry = k[2] * 0.01f;
    float *p = io->io, *line = s->line;
    /* the state in locals for the block (the line's stores could otherwise alias it, every sample) */
    int w = s->w, d0 = s->down[0], d1 = s->down[1], d2 = s->down[2], u0 = s->up[0], u1 = s->up[1];
    float x1 = s->x1, y1 = s->y1;
    const int n = io->frames;
    for (int i = 0; i < n; i++, p += 2) {
        float x = p[0];
        line[w] = x;
        w = w + 1 >= SHIFT_LINE ? 0 : w + 1;
        d0 = shift_step(d0, 128.0f);
        d1 = shift_step(d1, 128.0f);
        d2 = shift_step(d2, 128.0f);
        u0 = shift_step(u0, -256.0f);
        u1 = shift_step(u1, -256.0f);
        float t0 = shift_read_b(line, w, d0), t1 = shift_read_b(line, w, d1), t2 = shift_read_b(line, w, d2);
        float t3 = shift_read_b(line, w, u0), t4 = shift_read_b(line, w, u1);
        float down = st_fma(t2, (float)shift_window(d2), st_fma(t1, (float)shift_window(d1), t0 * (float)shift_window(d0)));
        float up = st_fma(t4, (float)shift_window(u1), t3 * (float)shift_window(u0));
        up = up * 1.7881393432617188e-07f;
        float y = st_fms(LP_A1, y1, st_fma(up, LP_B, LP_B * x1));                               /* VFMS */
        x1 = up;
        y1 = y;
        p[0] = p[1] = st_fma(y, high, st_fma(low, down, x * dry));
    }
    s->w = w;
    s->down[0] = d0;
    s->down[1] = d1;
    s->down[2] = d2;
    s->up[0] = u0;
    s->up[1] = u1;
    s->x1 = x1;
    s->y1 = y1;
}

const struct wvfx_desc fx_octaver_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x101),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Octaver",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = SHIFT_LINE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
