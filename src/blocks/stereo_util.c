/*
 * Stereo Util (VOL menu, waveod's): which channels go on, their polarity, the stereo width, the balance and the
 * level, all one 2 x 2 matrix on (L, R) moving over one block when a knob or the on / off switch changes, so
 * nothing clicks. Bypassed, the matrix is the identity.
 *
 *   channels   Stereo / Mono (L + R) / 2 / Left on both / Right on both / Swap
 *   polarity   Normal / Invert L / Invert R / Invert both
 *   width      0 (mono) .. 100 % (as it is) .. 200 % (the sides doubled), mid / side
 *   balance    -100 (left only) .. 0 .. +100 (right only)
 *   gain       -24 .. +12 dB
 *
 * The order: channels, polarity, width, balance, gain.
 */
#include "sdk/wvfx.h"

enum { K_GAIN, K_BALANCE, K_WIDTH, K_POLARITY, K_CHANNELS };

static const struct wvfx_param params[] = {
    { "Gain", -24.0f, 12.0f, 0.5f, 0.0f, K_GAIN, 0, 0, 0, 0, "%.1f dB" },
    { "Balance", -100.0f, 100.0f, 1.0f, 0.0f, K_BALANCE, 0x75, 0, 0, 0, 0 },     /* the original's pan text */
    { "Width", 0.0f, 200.0f, 5.0f, 100.0f, K_WIDTH, 0, 0, 0, 0, "%.0f%%" },
    { "Polarity", 0.0f, 3.0f, 1.0f, 0.0f, K_POLARITY, 0, 0, 0, "Normal\0Invert L\0Invert R\0Invert L+R\0", 0 },
    { "Channels", 0.0f, 4.0f, 1.0f, 0.0f, K_CHANNELS, 0, 0, 0, "Stereo\0Mono\0Left\0Right\0Swap\0", 0 },
};

struct state {
    float m[4];                     /* l' = m0 l + m1 r, r' = m2 l + m3 r: where the last block ended */
};

static void mul(float out[4], const float a[4], const float b[4])
{
    float r[4] = {
        a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
        a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
    };
    for (int i = 0; i < 4; i++)
        out[i] = r[i];
}

static int option(float v, int n)
{
    int i = (int)(v + 0.5f);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

/* the matrix the knobs ask for */
static void target(const float *k, int enabled, float m[4])
{
    static const float channels[5][4] = {
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.5f, 0.5f, 0.5f, 0.5f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 1.0f, 0.0f },
    };
    if (!enabled) {
        m[0] = m[3] = 1.0f;
        m[1] = m[2] = 0.0f;
        return;
    }
    const float *c = channels[option(k[K_CHANNELS], 5)];
    int pol = option(k[K_POLARITY], 4);
    float pol_m[4] = { pol & 1 ? -1.0f : 1.0f, 0.0f, 0.0f, pol & 2 ? -1.0f : 1.0f };
    float w = wvfx_clamp(k[K_WIDTH], 0.0f, 200.0f) * 0.01f;
    float width[4] = { 0.5f * (1.0f + w), 0.5f * (1.0f - w), 0.5f * (1.0f - w), 0.5f * (1.0f + w) };
    float b = wvfx_clamp(k[K_BALANCE], -100.0f, 100.0f) * 0.01f;
    float g = wvfx_db(wvfx_clamp(k[K_GAIN], -24.0f, 12.0f));
    float out[4] = { g * (b > 0.0f ? 1.0f - b : 1.0f), 0.0f, 0.0f, g * (b < 0.0f ? 1.0f + b : 1.0f) };
    float t[4];
    mul(t, pol_m, c);
    mul(t, width, t);
    mul(m, out, t);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    (void)buffer;
    (void)env;
    struct state *s = state;
    target(knobs, 1, s->m);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float to[4], d[4], m[4];
    target(io->knobs, io->enabled, to);
    int same = 1;
    for (int i = 0; i < 4; i++) {
        d[i] = wvfx_ramp_step(s->m[i], to[i], io->frames);
        m[i] = s->m[i];
        same &= d[i] == 0.0f;
    }
    if (same && m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f && m[3] == 1.0f)
        return;                                     /* identity: nothing to do */
    float *x = io->io;
    for (int f = 0; f < io->frames; f++, x += 2) {
        for (int i = 0; i < 4; i++)
            m[i] += d[i];
        float l = x[0], r = x[1];
        x[0] = m[0] * l + m[1] * r;
        x[1] = m[2] * l + m[3] * r;
    }
    for (int i = 0; i < 4; i++)
        s->m[i] = to[i];
}

/* a copy as a module beside the built-in (a test of the library): tools/wvfx.py build -D STEREO_UTIL_COPY=1 */
#ifdef STEREO_UTIL_COPY
#define ID   WVFX_ID(WVFX_VOL, 0x101)
#define NAME "Stereo Util M"
#else
#define ID   WVFX_ID(WVFX_VOL, 1)
#define NAME "Stereo Util"
#endif

const struct wvfx_desc stereo_util_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = ID,
    .menu = WVFX_VOL,
    .nparams = sizeof params / sizeof params[0],
    .flags = 0,
    .name = NAME,
    .category = "Utility",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
