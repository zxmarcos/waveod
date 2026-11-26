/*
 * Touch Wah (WAH): the original's Touch Wah (QME-200 V1.2.2, library entry 2.4, id 0x0100000F; its code calls it the
 * Toucher) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). An envelope wah, mono (the left
 * input; both outputs the same). Single precision.
 *
 * The envelope follows Sense |x| (x 0.8, Mode 1: x 1.6) by at most 0.000227 per sample either way; held in 0 .. 1
 * (e), shaped to c = 0.2 e below 0.5, 1.8 e - 0.8 from it, it sweeps a band pass (RBJ, x 7.08) from the low end
 * (250 Hz, Mode 1: 100) to the top (1600 Hz + 10 Range, Mode 1: 500 + 10 Range) with alpha = sin w / (2 + 2 Q
 * (0.09 - 0.072 c)) (Q the raw knob). Out x + Mix (y - x); Mix glides 0.0001 per sample, bypassed too. Mode counts
 * as 1 only at exactly 1. Bypassed it passes the signal.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Sense", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Range", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Q", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Mode", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x49, 0x00, 0, 0, 0 },
};

struct state {
    float mix, env;
    float x1, x2, y1, y2;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ .mix = k[3] * 0.01f };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    int m1 = k[4] == 1.0f;
    float mix = k[3] * 0.01f, q = k[2];
    float top = st_fma(k[1], 10.0f, m1 ? 500.0f : 1600.0f), low = m1 ? 100.0f : 250.0f;
    float sense = k[0] * (m1 ? 1.5999999046325684f : 0.7999999523162842f);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix = st_glide(s->mix, mix);
        if (!io->enabled)
            continue;
        float x = p[0], a = sense * st_abs(x);
        if (s->env < a) {
            s->env = s->env + 0.00022675737272948027f;
            if (s->env >= a)
                s->env = a;
        } else {
            s->env = s->env + -0.00022675737272948027f;
            if (s->env <= a)
                s->env = a;
        }
        float e = s->env < 1.0f ? s->env : 1.0f;                                          /* VMINNM, VMAXNM */
        e = e > 0.0f ? e : 0.0f;
        float c = 0.5f > e ? e * 0.20000000298023224f : st_fma(e, 1.7999999523162842f, -0.800000011920929f);
        float w = st_fma(c, top - low, low) * 0.00014247585204429924f;
        float alpha = st_sinf(w) / st_fma(st_fma(c, -0.07199999690055847f, 0.08999999612569809f) * q, 2.0f, 2.0f);
        float cw = st_cosf(w);
        float y = st_fma(alpha * 7.079500198364258f, x, (alpha * -7.079500198364258f) * s->x2);
        y = st_fma(cw, s->y1 + s->y1, y);
        y = st_fma(alpha - 1.0f, s->y2, y) / (alpha + 1.0f);
        s->x2 = s->x1;
        s->x1 = x;
        s->y2 = s->y1;
        s->y1 = y;
        p[0] = p[1] = st_fma(y - x, s->mix, x);
    }
}

const struct wvfx_desc wah_touch_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_WAH, 0x104),
    .menu = WVFX_WAH,
    .nparams = sizeof params / sizeof params[0],
    .name = "Touch Wah",
    .category = "Wah",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
