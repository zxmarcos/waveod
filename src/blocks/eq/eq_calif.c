/*
 * Calif EQ (EQ): the original's Calif EQ (QME-200 V1.2.2, library entry 7.4, id 0x0100003C; its code calls it the VEQ)
 * in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A five-band graphic EQ after the Calif
 * amps' one, mono (the left input; both outputs the same). Single precision, two Qs in double.
 *
 * x through five peak sections in series (v = 0.5 + knob / 100, knob -50 .. 50; A = 2^(g v - g / 2) with g per band
 * (+-6, 3.6, 4, 3.2, 4 dB at the ends); m = c A, d = c / A: b0 = 1 + m, b2 = 1 - m, a0 = 1 + d, a2 = 1 - d, b1 = a1;
 * divided by a0). 80 Hz and 240 Hz: c and a1 fixed; 750 Hz and 6.6 kHz: the frequency follows v (w0 + w1 v) and
 * c = sin w / 2 Q with Q = 0.727 - 0.245 v and 0.923 - 0.241 v (in double), a1 = -2 cos w; 2.2 kHz: the same with
 * c = 1.131 sin w. A band is recomputed at the first sample after its knob changed. No level control. Bypassed it
 * passes the signal.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "80Hz", -50.0f, 50.0f, 1.0f, 0.0f, 0, 0x5d, 0x00, 0, 0, 0 },
    { "240Hz", -50.0f, 50.0f, 1.0f, 0.0f, 1, 0x5d, 0x00, 0, 0, 0 },
    { "750Hz", -50.0f, 50.0f, 1.0f, 0.0f, 2, 0x5d, 0x00, 0, 0, 0 },
    { "2.2kHz", -50.0f, 50.0f, 1.0f, 0.0f, 3, 0x5d, 0x00, 0, 0, 0 },
    { "6.6kHz", -50.0f, 50.0f, 1.0f, 0.0f, 4, 0x5d, 0x00, 0, 0, 0 },
};

/* A's exponent g v + o per band */
static const float GAIN_G[5] = {
    2.491446018218994f, 1.4948676824569702f, 1.660964012145996f, 1.3287712335586548f, 1.660964012145996f,
};
static const float GAIN_O[5] = {
    -1.245723009109497f, -0.6643856167793274f, -0.830482006072998f, -0.6643856167793274f, -0.830482006072998f,
};

struct state {
    float v[5];
    struct sos s[5];
    struct sos_state st[5];
};

static struct sos peak(float a, float c, float a1)
{
    float m = a * c, d = c / a;
    return (struct sos){ m + 1.0f, a1, 1.0f - m, d + 1.0f, a1, 1.0f - d };
}

static void band_set(struct state *s, int i, float v)
{
    float a = st_exp2f(st_fma(v, GAIN_G[i], GAIN_O[i]));
    switch (i) {
    case 0:
        s->s[0] = peak(a, 0.010223928838968277f, -1.9999804496765137f);
        break;
    case 1:
        s->s[1] = peak(a, 0.0370025672018528f, -1.9972217082977295f);
        break;
    case 2: {
        float w = st_fma(v, 0.0069813174195587635f, 0.09460397064685822f);
        float q = (float)st_fmad((double)v, -0.245, 0.727);
        float sn = st_sinf(w), cs = st_cosf(w);
        float alpha = sn / (q + q);
        s->s[2] = (struct sos){ alpha * a + 1.0f, cs * -2.0f, 1.0f - alpha * a, alpha / a + 1.0f, cs * -2.0f,
                                1.0f - alpha / a };
        break;
    }
    case 3: {
        float w = st_fma(v, 0.04530732333660126f, 0.20602008700370789f);
        float sn = st_sinf(w), cs = st_cosf(w);
        s->s[3] = peak(a, sn * 1.1312217712402344f, cs * -2.0f);
        break;
    }
    default: {
        float w = st_fma(v, 0.05015150085091591f, 0.8333412408828735f);
        float q = (float)st_fmad((double)v, -0.241, 0.923);
        float sn = st_sinf(w), cs = st_cosf(w);
        float alpha = sn / (q + q);
        s->s[4] = (struct sos){ alpha * a + 1.0f, cs * -2.0f, 1.0f - alpha * a, alpha / a + 1.0f, cs * -2.0f,
                                1.0f - alpha / a };
        break;
    }
    }
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    for (int i = 0; i < 5; i++) {
        s->v[i] = st_fma(k[i], 0.01f, 0.5f);
        band_set(s, i, s->v[i]);
    }
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!io->frames || !io->enabled)
        return;
    for (int i = 0; i < 5; i++) {
        float v = st_fma(k[i], 0.01f, 0.5f);
        if (s->v[i] != v) {
            s->v[i] = v;
            band_set(s, i, v);
        }
    }
    float *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    for (int j = 0; j < 5; j++)                     /* the block's coefficients: section by section */
        sos_block_div(&s->s[j], &s->st[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i];
}

const struct wvfx_desc eq_calif_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_EQ, 0x104),
    .menu = WVFX_EQ,
    .nparams = sizeof params / sizeof params[0],
    .name = "Calif EQ",
    .category = "EQ",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
