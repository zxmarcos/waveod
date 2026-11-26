/*
 * The original's five-band EQs Guitar EQ 1, Bass EQ 1 and Bass EQ 2 (docs/stock_port.md): one path, the model's
 * bands. Mono (the left input; both outputs the same), single precision.
 *
 * x through five peak sections in series (b1 = a1 the band's fixed -2 cos w0; A = 2^(0.01993 knob), knob -50 .. 50:
 * A 0.5 .. 2; m = c A, d = c / A: b0 = 1 + m, b2 = 1 - m, a0 = 1 + d, a2 = 1 - d; divided by a0), x 2 Volume.
 * A band is recomputed at the first sample after its knob changed. Volume glides 0.005 per process call, bypassed
 * too. Bypassed it passes the signal.
 */
#pragma once

#include "../amps/amp.h"

struct eq5_model {
    float c[5], a1[5];
};

struct eq5 {
    float volume, knob[5];
    struct sos s[5];
    struct sos_state st[5];
};

static inline void eq5_band(const struct eq5_model *m, struct eq5 *e, int i, float knob)
{
    float a = st_exp2f(knob * 0.019931567832827568f), g = a * m->c[i], d = m->c[i] / a;
    e->s[i] = (struct sos){ g + 1.0f, m->a1[i], 1.0f - g, d + 1.0f, m->a1[i], 1.0f - d };
}

static inline void eq5_init(const struct eq5_model *m, struct eq5 *e, const float *k)
{
    *e = (struct eq5){ .volume = k[5] * 0.01f };
    for (int i = 0; i < 5; i++) {
        e->knob[i] = k[i] * 0.01f;
        eq5_band(m, e, i, k[i]);
    }
}

static inline void eq5_process(const struct eq5_model *m, struct eq5 *e, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    e->volume = st_glide_by(e->volume, k[5] * 0.01f, 0.004999999888241291f);
    if (!io->frames || !io->enabled)
        return;
    for (int i = 0; i < 5; i++)
        if (e->knob[i] != k[i] * 0.01f) {
            e->knob[i] = k[i] * 0.01f;
            eq5_band(m, e, i, k[i]);
        }
    float g = e->volume + e->volume, *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    for (int j = 0; j < 5; j++)                     /* the block's coefficients: section by section */
        sos_block_div(&e->s[j], &e->st[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * g;
}

#define EQ5_BLOCK(sym, idn, nm, model, n0, n1, n2, n3, n4)                             \
    static const struct wvfx_param params[] = {                                        \
        { n0, -50.0f, 50.0f, 1.0f, 0.0f, 0, 0x5d, 0x00, 0, 0, 0 },                     \
        { n1, -50.0f, 50.0f, 1.0f, 0.0f, 1, 0x5d, 0x00, 0, 0, 0 },                     \
        { n2, -50.0f, 50.0f, 1.0f, 0.0f, 2, 0x5d, 0x00, 0, 0, 0 },                     \
        { n3, -50.0f, 50.0f, 1.0f, 0.0f, 3, 0x5d, 0x00, 0, 0, 0 },                     \
        { n4, -50.0f, 50.0f, 1.0f, 0.0f, 4, 0x5d, 0x00, 0, 0, 0 },                     \
        { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },               \
    };                                                                                 \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    {                                                                                  \
        (void)buffer;                                                                  \
        (void)env;                                                                     \
        eq5_init(&(model), state, k);                                                  \
        return 0;                                                                      \
    }                                                                                  \
    static void process(void *state, const struct wvfx_io *io) { eq5_process(&(model), state, io); } \
    const struct wvfx_desc sym = {                                                     \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(WVFX_EQ, idn), .menu = WVFX_EQ, \
        .nparams = sizeof params / sizeof params[0], .name = nm, .category = "EQ", .params = params, \
        .state_size = sizeof(struct eq5), .buffer_size = 0, .init = init, .process = process, .poll = 0, \
    }
