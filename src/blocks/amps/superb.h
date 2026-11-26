/*
 * The original's Superb amps (docs/stock_port.md): CL and OD share everything but the Gain taper, the input level
 * and the output level, so the whole path is here and each model's file gives those three.
 *
 * Per sample:
 *   input         x Gain^2 x the model's level
 *   pre-filter    a fixed 1st-order section (divided by its a0 of 1)
 *   bass, middle  two 2nd-order sections from Bass (C25) and Middle (A25)
 *   stage         8x oversampled (amp.h, os8_block), the clipper 1.3416 v / (|8 v| + 0.08215)
 *   tone stack    3rd order from Treble (At) and Volume
 *   presence      a 2nd-order section from Presence (1 - C)
 *   output        x Volume x the model's level
 * Gain and Volume glide 0.0001 per sample, the rest take effect at once; the tone stack follows Volume's glide and
 * stops one step short of it. Init computes the tone stack from the Volume knob before its x 0.01 (the factors
 * x 0.01 instead, summed in another order), a rounding apart from Process's until Treble or Volume moves.
 */
#pragma once
#include "amp.h"

enum { SB_GAIN, SB_PRESENCE, SB_VOLUME, SB_BASS, SB_MIDDLE, SB_TREBLE };

#define SUPERB_PARAMS(gain) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), SB_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, SB_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, SB_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, SB_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, SB_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, SB_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

/* what differs between the models */
struct superb_model {
    float (*gain)(float knob);      /* the Gain taper */
    float in, out;                  /* the input and output levels */
};

static const struct fos SUPERB_PF = { 1.0487710237503052f, -0.8439328670501709f, -0.8927038908004761f };

struct superb_state {
    float gain, volume;                     /* glided */
    float bass, middle, treble, presence;   /* the knobs the sections were set for */
    struct fos_state pf;
    struct sos bass_c, mid_c, pres_c;
    struct sos_state bs, ms, ps;
    struct os8 nl;
    struct tone3 tone;
};

/* ---------------------------------------------------------------- the sections */

static inline void superb_bass_set(struct sos *c, float b)
{
    float p0 = b * 0.9282434f, p1 = b * -1.8564868f;
    c->b0 = p0 + 0.012787461f;
    c->b1 = p1 + -0.016877152f;
    c->b2 = p0 + 0.004089691f;
    c->a0 = b + 0.0130820125f;
    c->a1 = p1 + -0.016809916f;
    c->a2 = st_fma(b, 0.8564868f, 0.0038623768f);
}

static inline void superb_middle_set(struct sos *c, float m)
{
    float q = m * 0.8735586f;
    c->b0 = m + 0.28556892f;
    c->b1 = st_fma(m, -1.8735585f, -0.5614941f);
    c->b2 = q + 0.27707207f;
    c->a0 = m + 0.32350135f;
    c->a1 = c->b1;
    c->a2 = q + 0.23913963f;
}

/* from c, the C taper of the knob (Presence = 1 - c) */
static inline void superb_presence_set(struct sos *s, float c)
{
    float p = 1.0f - c, a = p * 0.9993705f, b = p * 1.998741f;
    s->b0 = 1.0356694f - a;
    s->b1 = b + -2.0632164f;
    s->b2 = 1.0275471f - a;
    s->a0 = -0.014829993f - c;
    s->a1 = 2.0202277f - b;
    s->a2 = st_fma(p, 0.99874103f, -1.005408f);
}

/*
 * The tone stack from Treble t and the glided Volume m (tm = t m, A = 179.9 tm m, B = 539.8 tm m, M = 0.185 m^2):
 *   n0 = (0.270 m + 261.5 tm) - (M + A)          n1 = (M - (0.101 m + 621.4 tm)) + B
 *   n2 = (M + 458.2 tm) - (0.270 m + B)          n3 = (0.101 m - M) - 98.3 tm + A
 *   d0 = 1 + 93.64 t + 2.152 m + 171.6 tm - 2.165 m^2 - A      d1 = -0.819 - 109.1 t - 6.111 m - 515.6 tm + 6.123 m^2 + B
 *   d2 = -0.999 - 61.98 t + 5.764 m + 516.4 tm - 5.752 m^2 - B  d3 = 0.820 + 77.47 t - 1.806 m - 172.4 tm + 1.794 m^2 + A
 * the d's fused in the order written.
 */
static inline void superb_tone_set(struct tone3 *o, float t, float m)
{
    float mk1 = m * 0.10134279727935791f, mk0 = m * 0.26954659819602966f, tm = m * t, m2 = m * m;
    float tmm = tm * m, M = m2 * 0.1854446977376938f;
    float A = tmm * 179.91845703125f, B = tmm * 539.75537109375f;
    o->b[0] = st_fma(tm, 261.5141296386719f, mk0) - (M + A);
    o->b[1] = (M - st_fma(tm, 621.3510131835938f, mk1)) + B;
    o->b[2] = st_fma(tm, 458.1596984863281f, M) - (mk0 + B);
    o->b[3] = st_fma(tm, -98.32278442382812f, mk1 - M) + A;
    o->a[0] = st_fma(m2, -2.1645476818084717f, st_fma(tm, 171.6311492919922f,
              st_fma(m, 2.151926040649414f, st_fma(t, 93.64138793945312f, 1.0f)))) - A;
    o->a[1] = st_fma(m2, 6.122753620147705f, st_fma(tm, -515.63525390625f,
              st_fma(m, -6.110896587371826f, st_fma(t, -109.13780212402344f, -0.819236695766449f)))) + B;
    o->a[2] = st_fma(m2, -5.751864433288574f, st_fma(tm, 516.3770141601562f,
              st_fma(m, 5.764485836029053f, st_fma(t, -61.975738525390625f, -0.9993064999580383f)))) - B;
    o->a[3] = st_fma(m2, 1.7936582565307617f, st_fma(tm, -172.3729248046875f,
              st_fma(m, -1.8055152893066406f, st_fma(t, 77.47215270996094f, 0.8199301958084106f)))) + A;
}

/* Init's: v the Volume knob (0-100), m = v x 0.01; the m terms from v, the d's fused 1, v, m^2, t, tm */
static inline void superb_tone_set_init(struct tone3 *o, float t, float v)
{
    float m = v * 0.01f, tm = t * m, tmm = tm * m, m2 = m * m;
    float vk0 = v * 0.002695465926080942f, vk1 = v * 0.001013427972793579f, M = m2 * 0.1854446977376938f;
    float A = tmm * 179.91845703125f, B = tmm * 539.75537109375f;
    o->b[0] = st_fma(tm, 261.5141296386719f, vk0) - (M + A);
    o->b[1] = st_fma(tm, -621.3510131835938f, M - vk1) + B;
    o->b[2] = st_fma(tm, 458.1596984863281f, M) - (vk0 + B);
    o->b[3] = st_fma(tm, -98.32278442382812f, vk1 - M) + A;
    o->a[0] = st_fma(tm, 171.6311492919922f, st_fma(t, 93.64138793945312f,
              st_fma(m2, -2.1645476818084717f, st_fma(v, 0.021519260480999947f, 1.0f)))) - A;
    o->a[1] = st_fma(tm, -515.63525390625f, st_fma(t, -109.13780212402344f,
              st_fma(m2, 6.122753620147705f, st_fma(v, -0.061108965426683426f, -0.819236695766449f)))) + B;
    o->a[2] = st_fma(tm, 516.3770141601562f, st_fma(t, -61.975738525390625f,
              st_fma(m2, -5.751864433288574f, st_fma(v, 0.05764485523104668f, -0.9993064999580383f)))) - B;
    o->a[3] = st_fma(tm, -172.3729248046875f, st_fma(t, 77.47215270996094f,
              st_fma(m2, 1.7936582565307617f, st_fma(v, -0.018055152148008347f, 0.8199301958084106f)))) + A;
}

/* the stage's clipper: 1.3416 v / (|8 v| + 0.08215) */
static inline float superb_clip(float v)
{
    return (v * 1.3416f) / (st_abs(v * 8.0f) + 0.08215f);
}

/* ---------------------------------------------------------------- the block */

static inline void superb_init(struct superb_state *s, const struct superb_model *md, const float *k)
{
    *s = (struct superb_state){ 0 };
    float c = st_taper(st_c, k[SB_PRESENCE]);
    s->gain = md->gain(k[SB_GAIN]);
    s->volume = k[SB_VOLUME] * 0.01f;
    s->bass = st_taper(st_c25, k[SB_BASS]);
    s->middle = st_taper(st_a25, k[SB_MIDDLE]);
    s->treble = st_taper(st_at, k[SB_TREBLE]);
    s->presence = 1.0f - c;
    superb_bass_set(&s->bass_c, s->bass);
    superb_middle_set(&s->mid_c, s->middle);
    superb_tone_set_init(&s->tone, s->treble, k[SB_VOLUME]);
    superb_presence_set(&s->pres_c, c);
}

static inline void superb_process(struct superb_state *s, const struct superb_model *md, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    float gain = md->gain(k[SB_GAIN]), volume = k[SB_VOLUME] * 0.01f;
    float bass = st_taper(st_c25, k[SB_BASS]), middle = st_taper(st_a25, k[SB_MIDDLE]);
    float treble = st_taper(st_at, k[SB_TREBLE]), c = st_taper(st_c, k[SB_PRESENCE]), presence = 1.0f - c;
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
        }
        return;
    }
    /* stage by stage over the block: Bass, Middle and Presence (not glided) can only change before the first
       sample; the tone stack follows the glided Volume sample by sample. The same samples as sample by sample. */
    if (s->bass != bass) {
        s->bass = bass;
        superb_bass_set(&s->bass_c, bass);
    }
    if (s->middle != middle) {
        s->middle = middle;
        superb_middle_set(&s->mid_c, middle);
    }
    if (s->presence != presence) {
        s->presence = presence;
        superb_presence_set(&s->pres_c, c);
    }
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        vol[i] = s->volume;
        x[i] = fos_run_div(&SUPERB_PF, 1.0f, &s->pf, s->gain * s->gain * md->in * p[2 * i]);
    }
    sos_block_div(&s->bass_c, &s->bs, x, n);
    sos_block_div(&s->mid_c, &s->ms, x, n);
    os8_block(&s->nl, x, n, superb_clip);
    for (int i = 0; i < n; i++) {
        if (s->treble != treble || vol[i] != volume) {
            s->treble = treble;
            superb_tone_set(&s->tone, treble, vol[i]);
        }
        float v = sos_run_div(&s->pres_c, &s->ps, tone3_run_std(&s->tone, x[i]));
        p[2 * i] = p[2 * i + 1] = vol[i] * md->out * v;
    }
}
