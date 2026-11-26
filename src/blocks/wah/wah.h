/*
 * The original's pedal wahs Voks, Cry and Bass Wah (docs/stock_port.md): one path, the model's constants. Mono (the
 * left input; both outputs the same), single precision.
 *
 * x through a fixed 2nd-order high pass (divided by its a0), then a band pass (RBJ, x g in its numerator) at
 * w = w0 + w1 p rad with alpha = sin w / (2 + 2 Q (9 + k p)), p = Position x the range (Range x 0.004 below 50,
 * 0.016 Range - 0.6 from 50: 0 .. 0.2 .. 1); out (band pass + dry x) Volume. The Rack Wah adds five fixed
 * sections after the band pass with EQ 1 (its knob's value exactly 1) and takes the dry part from the high pass,
 * out 2 (band pass + dry h) Volume. Q, Volume and Position glide 0.0001 per sample, bypassed too. Bypassed it
 * passes the signal.
 */
#pragma once

#include "../amps/amp.h"

struct wah_model {
    struct sos highpass;
    float w0, w1, k, g, dry;
    const struct sos *eq;               /* 5 sections after the band pass when knob 4 (EQ) is 1, or 0 */
    int dry_hp;                         /* the dry part from the high pass, the sum x 2 (Rack Wah) */
};

#define WAH_PARAMS                                                                     \
    { "Range", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },                    \
    { "Q", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },                        \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },                   \
    { "Position", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 }

struct wah {
    float q, volume, position;
    struct sos_state hp, eq[5];
    float x1, x2, y1, y2;
};

static inline void wah_init(struct wah *s, const float *k)
{
    *s = (struct wah){ .q = k[1] * 0.01f, .volume = k[2] * 0.01f, .position = k[3] * 0.01f };
}

static inline void wah_process(const struct wah_model *m, struct wah *s, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    float q = k[1] * 0.01f, vol = k[2] * 0.01f, pos = k[3] * 0.01f;
    float range = 0.5f > k[0] * 0.01f ? k[0] * 0.004000000189989805f : st_fma(k[0], 0.01600000075995922f, -0.6f);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->q = st_glide(s->q, q);
        s->volume = st_glide(s->volume, vol);
        s->position = st_glide(s->position, pos);
        if (!io->enabled)
            continue;
        float x = p[0], h = sos_run_div(&m->highpass, &s->hp, x);
        float pr = s->position * range;
        float w = st_fma(pr, m->w1, m->w0);
        float alpha = st_sinf(w) / st_fma(st_fma(pr, m->k, 9.0f) * s->q, 2.0f, 2.0f);
        float c = st_cosf(w);
        float y = st_fma(s->y2, alpha - 1.0f, st_fma(s->y1, c + c, st_fma(s->x2, alpha * -m->g, h * (alpha * m->g))));
        y = y / (alpha + 1.0f);
        s->x2 = s->x1;
        s->x1 = h;
        s->y2 = s->y1;
        s->y1 = y;
        if (m->eq && k[4] == 1.0f)
            for (int j = 0; j < 5; j++)
                y = sos_run(&m->eq[j], &s->eq[j], y);
        if (m->dry_hp) {
            float o = st_fma(h, m->dry, y);
            p[0] = p[1] = s->volume * (o + o);
        } else {
            p[0] = p[1] = st_fma(x, m->dry, y) * s->volume;
        }
    }
}

#define WAH_BLOCK(sym, idn, nm, model)                                                 \
    static const struct wvfx_param params[] = { WAH_PARAMS };                          \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    {                                                                                  \
        (void)buffer;                                                                  \
        (void)env;                                                                     \
        wah_init(state, k);                                                            \
        return 0;                                                                      \
    }                                                                                  \
    static void process(void *state, const struct wvfx_io *io) { wah_process(&(model), state, io); } \
    const struct wvfx_desc sym = {                                                     \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(WVFX_WAH, idn), .menu = WVFX_WAH, \
        .nparams = sizeof params / sizeof params[0], .name = nm, .category = "Wah", .params = params, \
        .state_size = sizeof(struct wah), .buffer_size = 0, .init = init, .process = process, .poll = 0, \
    }
