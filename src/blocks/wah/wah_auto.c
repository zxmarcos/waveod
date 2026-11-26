/*
 * Auto Wah (WAH): the original's Auto Wah (QME-200 V1.2.2, library entry 2.5, id 0x01000015; its code calls it the
 * Crier) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). An LFO wah, mono (the left input;
 * both outputs the same). Single precision.
 *
 * A triangle 0 .. 1 (stepping +-r per sample, r = Rate / 44100, or with Sync on 2 / the tempo division in
 * samples (the Rate's whole value 0 .. 10; others 1 beat)) sweeps a bilinear band pass (t = tan(pi f / 44100), q = 1 + 0.09 Q:
 * (t (x - x2) + 2q (1 - t^2) y1 + (t - q - q t^2) y2) / (t + q + q t^2)) from f = 200 + 3 Low Hz up by Depth
 * (800 - 3 Low + 40 High) Hz; out 5 Volume y. The rate, Depth and Volume move 0.00022676 of the way from the
 * block's start each sample; the sweep takes Depth at the knob (the original glides a copy it does not read).
 * Bypassed it passes the signal.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x16, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Low", 0.0f, 100.0f, 1.0f, 25.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Q", 0.0f, 100.0f, 1.0f, 70.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "High", 0.0f, 100.0f, 1.0f, 60.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
};

static const float SYNC_BEATS[11] = {
    4.0f, 2.0f, 3.0f, 1.3333332538604736f, 1.0f, 1.5f, 0.6666666865348816f, 0.5f, 0.75f, 0.3333333134651184f, 0.25f,
};

struct state {
    float depth, volume, rate, sign, tri;
    float x1, x2, y1, y2;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ .depth = k[0] * 0.01f, .volume = k[2] * 0.01f, .sign = 1.0f };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate;
    if ((int)k[6] == 1) {
        unsigned d = (unsigned)(int)k[1];
        rate = 2.0f / (((float)(io->beat_ms & 0xffffu) * 44.099998474121094f) * (d > 10 ? 1.0f : SYNC_BEATS[d]));
    } else {
        rate = k[1] * 2.2675736545352265e-05f;
    }
    if (!io->frames || !io->enabled)
        return;
    float depth = k[0] * 0.01f, low3 = k[3] * 3.0f, vol = k[2] * 0.01f;
    float q = st_fma(k[4], 0.08999999612569809f, 1.0f), q2 = q + q;
    float span = st_fma(k[5], 40.0f, 800.0f - low3) * depth, base = low3 + 200.0f;
    float dd = (depth - s->depth) * 0.0002267600066261366f, dr = (rate - s->rate) * 0.0002267600066261366f;
    float dv = (vol - s->volume) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        s->rate = s->rate + dr;
        s->depth = s->depth + dd;
        s->volume = s->volume + dv;
        float x = p[0];
        s->tri = st_fma(s->sign, s->rate, s->tri);
        if (s->tri >= 1.0f) {
            s->tri = 1.0f;
            s->sign = -1.0f;
        } else if (!(s->tri >= 0.0f)) {
            s->tri = 0.0f;
            s->sign = 1.0f;
        }
        float t = st_tanf(st_fma(span, s->tri, base) * 7.123792602214962e-05f), t2 = t * t, qt2 = t2 * q;
        float y = ((t - q) - qt2) * s->y2;
        y = st_fma(st_fma(-t2, q2, q2), s->y1, y);
        y = st_fma(t, x - s->x2, y);
        y = y / ((t + q) + qt2);
        s->x2 = s->x1;
        s->x1 = x;
        s->y2 = s->y1;
        s->y1 = y;
        p[0] = p[1] = (s->volume * 5.0f) * y;
    }
}

const struct wvfx_desc wah_auto_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_WAH, 0x105),
    .menu = WVFX_WAH,
    .nparams = sizeof params / sizeof params[0],
    .name = "Auto Wah",
    .category = "Wah",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
