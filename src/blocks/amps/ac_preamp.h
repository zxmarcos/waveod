/*
 * The original's acoustic preamps AC Preamp and AC Preamp 2 (docs/stock_port.md): one path; the models differ in the
 * process's EQ frequency and Q only. Mono: the left input, the result on both outputs. Single precision, the EQ's Q in
 * double.
 *
 * Per sample:
 *   tone 1      a shelf by Balance b: A = 2^(1.661 - 2.076 b^4), m = 0.00387 A, d = 0.00387 / A (b1 = a1 fixed)
 *   tone 2      a peak by Balance: A = 2^(0.0830 min(22 b, 12)), w = 1.7097 - 0.2850 b, c = 1.4706 sin w
 *   blend       x + Tone (pow2) (tone - x)
 *   EQ          a peak by EQ Gain (A = 2^(2.4914 g - 1.2457)), EQ Freq (pow4: w = w0 + w1 f) and EQ Q (pow0_2:
 *               alpha = sin w / 2q, q = 3.6 - 3 Q or 2.6 - 2 Q)
 *   enhance     a fixed 2nd-order section
 *   output      x 4 Volume
 * Every section but the last divided by its a0; m, d give b0 = 1 + m, b2 = 1 - m, a0 = 1 + d, a2 = 1 - d. The six
 * knobs glide 0.0001 per sample, bypassed too; the tones are recomputed while Balance has not reached its knob, the
 * EQ while EQ Gain, Q or Freq has not (not at the last step, as the original). The Init sets them from the raw knobs
 * with the x 0.01 folded into the constants, and its EQ with the AC Preamp 2's frequency (0.1202 + 1.7342 f) and
 * Q (2.6 - 2 Q), also in the AC Preamp, until the EQ knobs move.
 */
#pragma once

#include "amp.h"
#include "stock_tapers.h"

struct ac_preamp_model {
    float eq_w0, eq_w1;                 /* the process's EQ frequency w0 + w1 f */
    float q_a, q_b;                     /* and its q = q_b + q_a Q (+ 0.6) */
};

#define AC_PREAMP_PARAMS                                                               \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },                   \
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },                     \
    { "Balance", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },                  \
    { "EQ Freq", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },                  \
    { "EQ Q", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },                     \
    { "EQ Gain", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 }

static const struct sos AC_ENHANCE = {
    1.213942289352417f, -2.2533891201019287f, 1.0457171201705933f, 1.0f, -1.8416252136230469f, 0.8478958010673523f,
};

struct ac_preamp {
    float volume, balance, tone, level, width, freq;
    struct sos t1, t2, eq;
    struct sos_state s1, s2, seq, sen;
};

static inline struct sos ac_peak(float m, float d, float a1)
{
    return (struct sos){ m + 1.0f, a1, 1.0f - m, d + 1.0f, a1, 1.0f - d };
}

/* the tones from Balance b; bk = b x 100 is the Init's raw knob (its constants x 0.01) */
static inline void ac_tones(struct ac_preamp *s, float b, float lim, float w)
{
    float b2 = b * b, a = st_exp2f(st_fma(b2 * b2, -2.076205015182495f, 1.660964012145996f));
    s->t1 = ac_peak(a * 0.003869149135425687f, 0.003869149135425687f / a, -1.999994158744812f);
    float g = st_exp2f((lim < 12.0f ? lim : 12.0f) * 0.08304820209741592f);                  /* VMINNM */
    float c = st_sinf(w) * 1.470588207244873f;
    s->t2 = ac_peak(g * c, c / g, st_cosf(w) * -2.0f);
}

static inline void ac_eq(struct ac_preamp *s, float a, float w, float q)
{
    float alpha = st_sinf(w) / (q + q);
    s->eq = ac_peak(alpha * a, alpha / a, st_cosf(w) * -2.0f);
}

static inline void ac_preamp_init(struct ac_preamp *s, const float *k)
{
    *s = (struct ac_preamp){ .volume = k[0] * 0.01f, .balance = k[2] * 0.01f, .tone = st_taper(st_pow2, k[1]),
                             .level = k[5] * 0.01f, .width = st_taper(st_pow0_2, k[4]),
                             .freq = st_taper(st_pow4, k[3]) };
    ac_tones(s, s->balance, k[2] * 0.2199999988079071f, st_fma(k[2], -0.0028495171573013067f, 1.7097103595733643f));
    ac_eq(s, st_exp2f(st_fma(k[5], 0.024914460256695747f, -1.245723009109497f)),
          st_fma(s->freq, 1.7342160940170288f, 0.12024962157011032f),
          (float)((double)st_fma(s->width, -2.0f, 2.0f) + 0.6));
}

static inline void ac_preamp_process(const struct ac_preamp_model *m, struct ac_preamp *s, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    float vol = k[0] * 0.01f, bal = k[2] * 0.01f, tone = st_taper(st_pow2, k[1]), lvl = k[5] * 0.01f;
    float wid = st_taper(st_pow0_2, k[4]), frq = st_taper(st_pow4, k[3]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->volume = st_glide(s->volume, vol);
        s->balance = st_glide(s->balance, bal);
        s->tone = st_glide(s->tone, tone);
        s->level = st_glide(s->level, lvl);
        s->width = st_glide(s->width, wid);
        s->freq = st_glide(s->freq, frq);
        if (!io->enabled)
            continue;
        if (s->balance != bal)
            ac_tones(s, s->balance, s->balance * 22.0f,
                     st_fma(s->balance, -0.28495171666145325f, 1.7097103595733643f));
        if (s->level != lvl || s->width != wid || s->freq != frq)
            ac_eq(s, st_exp2f(st_fma(s->level, 2.491446018218994f, -1.245723009109497f)),
                  st_fma(s->freq, m->eq_w1, m->eq_w0), (float)((double)st_fma(s->width, m->q_a, m->q_b) + 0.6));
        float x = p[0];
        float t = sos_run_div(&s->t2, &s->s2, sos_run_div(&s->t1, &s->s1, x));
        float y = sos_run_div(&s->eq, &s->seq, st_fma(t - x, s->tone, x));
        y = sos_run(&AC_ENHANCE, &s->sen, y);
        p[0] = p[1] = (s->volume * 4.0f) * y;
    }
}

#define AC_PREAMP_BLOCK(sym, idn, nm, model)                                           \
    static const struct wvfx_param params[] = { AC_PREAMP_PARAMS };                    \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    {                                                                                  \
        (void)buffer;                                                                  \
        (void)env;                                                                     \
        ac_preamp_init(state, k);                                                      \
        return 0;                                                                      \
    }                                                                                  \
    static void process(void *state, const struct wvfx_io *io) { ac_preamp_process(&(model), state, io); } \
    const struct wvfx_desc sym = {                                                     \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(WVFX_AMP, idn), .menu = WVFX_AMP, \
        .nparams = sizeof params / sizeof params[0], .name = nm, .category = "Acoustic", .params = params, \
        .state_size = sizeof(struct ac_preamp), .buffer_size = 0, .init = init, .process = process, .poll = 0, \
    }
