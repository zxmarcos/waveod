/*
 * M-Chorus (MOD › Chorus): the original's M-Chorus (QME-200 V1.2.2, library entry 8.3, id 0x0400000F) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; three chorus voices (L, C, R) on
 * LFO phases 120 degrees apart. Single precision except the output mix.
 *
 * Each voice: a 700-sample delay line read 460 / 410 / 360 + 130 d (0.5 + 0.5 sin(2 pi r n + phase)) samples back
 * (linear interpolation), through a 2nd-order low pass set by Filter (a bilinear design from sin and cos of
 * 0.1425 + 1.2823 (1 - Filter)). Out: x - Mix x + 0.9 Mix (L: C + L + 0.1 R, R: C + 0.1 L + R), the sums in double.
 * Mix, the three depths and the rate glide 0.00022676 of the way to the knobs each sample, the rate from 0; the
 * count n starts over at one period.
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x16, 0, 0, 0 },
    { "Filter", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Depth L", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Depth C", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Depth R", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
};

static const float BASE[3] = { 460.0f, 410.0f, 360.0f };
static const float PHASE[3] = { 0.0f, 2.094395160675049f, 4.188790321350098f };

struct voice {
    MOD_LINE(700) line;
    struct sos_state tone;
    float depth;
};

struct state {
    float mix, rate;
    int n;
    struct sos tc;
    struct voice v[3];                  /* L, C, R */
};

static void tone_set(struct sos *c, float filter)
{
    float w = st_fma(st_fma(-filter, 0.009999999776482582f, 1.0f), 1.2822827100753784f, 0.14247585833072662f);
    float sn = st_sinf(w), cs = st_cosf(w), b1 = 1.0f - cs;
    *c = (struct sos){ b1 * 0.5f, b1, b1 * 0.5f, sn * 0.7072135806083679f + 1.0f, cs * -2.0f,
                       1.0f - sn * 0.7072135806083679f };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    for (int i = 0; i < 3; i++)
        s->v[i].depth = k[3 + i] * 0.01f;
    tone_set(&s->tc, k[2]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate_k(k[1], k[6], io->beat_ms, 2.267600029881578e-05f);
    if (!io->enabled || io->frames == 0)
        return;
    tone_set(&s->tc, k[2]);
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float dd[3];
    for (int i = 0; i < 3; i++)
        dd[i] = (k[3 + i] * 0.01f - s->v[i].depth) * 0.0002267600066261366f;
    /* stage by stage over the block (the same operations on the same values as sample by sample): the phases and
       glides; the three LFOs side by side (st_sinf_nb: the voices' sines overlap); each voice's line; the three tone
       filters side by side, their pasts in registers; the mix */
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float *p = io->io, ph[WVFX_MAX_FRAMES], mx[WVFX_MAX_FRAMES], y[3][WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++) {
        s->mix += dm;
        s->rate += dr;
        mx[i] = s->mix;
        float period = 1.0f / s->rate;
        ph[i] = (s->rate * 6.2831854820251465f) * (float)s->n;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
    }
    for (int i = 0; i < n; i++) {
#pragma clang loop unroll(full)
        for (int j = 0; j < 3; j++)
            y[j][i] = st_fma(st_sinf_nb(ph[i] + PHASE[j]), 0.5f, 0.5f);
    }
    for (int j = 0; j < 3; j++) {
        struct voice *v = &s->v[j];
        for (int i = 0; i < n; i++) {
            v->depth += dd[j];
            y[j][i] = mod_line_run(&v->line, p[2 * i], st_fma(v->depth * 130.0f, y[j][i], BASE[j]));
        }
    }
    {
        const float b0 = s->tc.b0, b1 = s->tc.b1, b2 = s->tc.b2, a0 = s->tc.a0, a1 = s->tc.a1, a2 = s->tc.a2;
        struct sos_state *t0 = &s->v[0].tone, *t1 = &s->v[1].tone, *t2 = &s->v[2].tone;
        float x10 = t0->x1, x20 = t0->x2, y10 = t0->y1, y20 = t0->y2;
        float x11 = t1->x1, x21 = t1->x2, y11 = t1->y1, y21 = t1->y2;
        float x12 = t2->x1, x22 = t2->x2, y12 = t2->y1, y22 = t2->y2;
#define MCH_SOS(v, x1, x2, y1, y2)                                                                                  \
        do {                                                                                                        \
            float in_ = (v), n_ = b0 * in_;                                                                         \
            n_ = st_fma(b1, x1, n_);                                                                                \
            n_ = st_fma(b2, x2, n_);                                                                                \
            float d_ = a1 * y1;                                                                                     \
            d_ = st_fma(a2, y2, d_);                                                                                \
            float o_ = (n_ - d_) / a0;                                                                              \
            x2 = x1;                                                                                                \
            x1 = in_;                                                                                               \
            y2 = y1;                                                                                                \
            y1 = o_;                                                                                                \
            (v) = o_;                                                                                               \
        } while (0)
        for (int i = 0; i < n; i++) {
            MCH_SOS(y[0][i], x10, x20, y10, y20);
            MCH_SOS(y[1][i], x11, x21, y11, y21);
            MCH_SOS(y[2][i], x12, x22, y12, y22);
        }
#undef MCH_SOS
        *t0 = (struct sos_state){ x10, x20, y10, y20 };
        *t1 = (struct sos_state){ x11, x21, y11, y21 };
        *t2 = (struct sos_state){ x12, x22, y12, y22 };
    }
    for (int i = 0; i < n; i++) {
        float x = p[2 * i];
        double dry = (double)st_fma(-mx[i], x, x), wet = (double)mx[i] * 0.9;
        p[2 * i] = (float)st_fmad(wet, st_fmad((double)y[2][i], 0.1, (double)(y[1][i] + y[0][i])), dry);
        p[2 * i + 1] = (float)st_fmad(wet, st_fmad((double)y[0][i], 0.1, (double)y[1][i]) + (double)y[2][i], dry);
    }
}

const struct wvfx_desc mod_m_chorus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x103),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "M-Chorus",
    .category = "Chorus",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
