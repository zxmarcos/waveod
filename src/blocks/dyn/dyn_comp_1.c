/*
 * COMP 1 (DYN): the original's COMP 1 (QME-200 V1.2.2, library entry 0.0, id 0x00000000) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A one-knob compressor, mono (the left input; both outputs the same).
 * Single precision.
 *
 * The level, 20 log10 |x (3 + 97 s)| (s = pow4(Sustain)), sets the gain: 0 dB below -10 dB, above it
 * -9.9 + 0.2 log10 - 20 log10 (the output held near -10 dB); the gain in dB moves toward that by 1 - 0.98353 of the
 * way per sample falling and 1 - 0.99979 rising. x times the gain (2^(dB / 6.02)) goes through two 2nd-order sections
 * and x (5 + 50 s) Volume. Sustain and Volume glide 0.0001 per sample. Bypassed it passes the signal (the knobs glide
 * on).
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Sustain", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
};

#define ATTACK 0.9835294485092163f
#define RELEASE 0.9997923970222473f

static const struct sos POST1 = {
    0.6179965138435364f, -1.143829345703125f, 0.5258499979972839f, 1.0f, -1.8443238735198975f, 0.844621479511261f,
};
static const struct sos POST2 = {
    1.143829345703125f, 0.36929789185523987f, 0.008779199793934822f, 1.0f, 0.008318199776113033f,
    9.800000043469481e-06f,
};

struct state {
    float sustain, volume, gain_db;
    struct sos_state p1, p2;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->sustain = st_taper(st_pow4, k[0]);
    s->volume = k[1] * 0.01f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float vol = io->knobs[1] * 0.01f, sus = st_taper(st_pow4, io->knobs[0]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->sustain = st_glide(s->sustain, sus);
        s->volume = st_glide(s->volume, vol);
        if (!io->enabled)
            continue;
        float x = p[0];
        float l = st_log10f(st_abs(x * st_fma(s->sustain, 97.0f, 3.0f)) + 9.99999991097579e-38f);
        float db = l * 20.0f, knee = st_fma(l, 0.19999998807907104f, -9.899999618530273f);
        float g = (-10.0f > db ? db : knee) - db;
        s->gain_db = st_fma(g <= s->gain_db ? ATTACK : RELEASE, s->gain_db - g, g);
        float y = sos_run(&POST2, &s->p2, sos_run(&POST1, &s->p1, st_exp2f(s->gain_db * 0.16609640419483185f) * x));
        p[0] = p[1] = (st_fma(s->sustain, 50.0f, 5.0f) * s->volume) * y;
    }
}

const struct wvfx_desc dyn_comp_1_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x100),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "COMP 1",
    .category = "Compressor",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
