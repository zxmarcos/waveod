/*
 * The pieces the original's Dizzy VH and VH+ share (docs/stock_port.md): one path with two clipping stages, the
 * same tables, the model's gain section, drive and clippers in a struct dizzy_model. The generic pieces are in
 * amp.h.
 *
 * Per sample:
 *   pre-filter    a fixed 2nd-order section
 *   gain          a 1st-order section from Gain (pow2), the model's low pass and drive
 *   stage 1       the model's first power clipper on 4 points per sample, the FIR
 *   tone stack    3rd order, polynomials in Bass (pow2_5), Middle and Treble (linear), x Volume
 *   stage 2       the model's second clipper, the same 4 points and FIR
 *   presence      two shelves from Presence (pow2; held at 0.01 below the taper's point 5)
 *   power amp     six fixed 2nd-order sections, x the model's output
 * Volume, Middle and Treble glide 0.0001 per sample; Gain, Bass and Presence take effect at once. The tone
 * coefficients stop one glide step short of the knobs.
 */
#pragma once
#include "amp.h"

enum { DK_GAIN, DK_PRESENCE, DK_VOLUME, DK_BASS, DK_MIDDLE, DK_TREBLE };

#define DIZZY_PARAMS { \
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, DK_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, DK_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, DK_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, DK_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, DK_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, DK_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

/* a model: the gain section b0 = g k, b1 = -g k, a1 = g a1k + a1c, a0 = g + a0c; its low pass and the drive after; each
   stage's clip_pow2 (limits above / below, k above / below); the output scale */
struct dizzy_model {
    float k, a1k, a1c, a0c, drive;
    struct sos lowpass;
    float lp1, ln1, kp1, kn1, lp2, ln2, kp2, kn2;
    float out;
};

static const struct sos DIZZY_PRE = { 1574.017578125f, -3129.692138671875f, 1555.67529296875f, 1.0f,
                                      -1.9871089458465576f, 0.9871237874031067f };

static const struct sos DIZZY_POWER[6] = {
    { 0.5434083938598633f, -1.0800906419754028f, 0.536730170249939f, 1.0f, -1.9933371543884277f,
      0.9933980107307434f },
    { 0.5927727222442627f, -1.0800906419754028f, 0.4903973937034607f, 1.0f, -1.7887135744094849f,
      0.8018631935119629f },
    { 1.0001914501190186f, -1.0800906419754028f, 0.1229930967092514f, 1.0f, 0.1433728039264679f,
      0.0008440000237897038f },
    { 1.0800906419754028f, 0.2158564031124115f, 0.3190208971500397f, 1.0f, 0.4121032953262329f,
      0.4069977104663849f },
    { 1.0800906419754028f, 0.6668086051940918f, 0.9086694121360779f, 1.0f, 0.6248704195022583f,
      0.8543516993522644f },
    { 1.0800906419754028f, -0.44509369134902954f, 0.00765890022739768f, 1.0f, -1.2945163249969482f,
      0.3046604096889496f },
};

struct dizzy_state {
    float volume, middle, treble;       /* glided */
    float gain, bass, presence;         /* the knobs the sections were computed for */
    struct fos gain_c;
    float gain_a0;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[6];
    struct fos_state gs;
    struct stage s1, s2;
};

/* the presence section from Presence p (shelves_set) */
static inline void dizzy_presence_set(struct sos *c, float p)
{
    shelves_set(c, p, 0.6643856167793274f, -0.3321928083896637f, 0.014248549938201904f, 0.02849709987640381f);
}

/* the tone stack from Bass b, Middle m and Treble t */
static inline void dizzy_tone_set(struct tone3 *o, float b, float m, float t)
{
    float m2 = m * m, t2 = t * t, t2m2 = t2 * m2, mb = m * b, tb = t * b, tb3 = tb * 3.0f, tm = t * m;
    float b1 = b * 0.01914641074836254f, b3 = b * 0.01665233075618744f;
    float f4 = t2m2 * 0.005809056106954813f, f5 = (mb * 0.2640480101108551f) * t2;
    float f8 = t2m2 * 0.017427170649170876f, f9 = (mb * 0.7921440005302429f) * t2;
    float f2 = tm * 0.02199999988079071f, f7 = tm * 0.06599999964237213f;
    float n0 = st_fma(m2, -0.015781419351696968f, st_fma(t, 0.023037530481815338f, b1 + 0.00042122098966501653f) + tb);
    o->b[0] = st_fma(st_fma(b, 0.7173373103141785f, 0.01609889045357704f), m, n0 - f2);
    float n1 = st_fma(t, -0.06702391058206558f, -0.000366351188858971f - (b3 + tb3)) + f7;
    o->b[1] = st_fma(st_fma(b, -2.045542001724243f, -0.045265890657901764f), m,
                     st_fma(m2, 0.04500193148851395f, n1));
    float n2 = st_fma(t, 0.0649624690413475f, (-0.00042122098966501653f - b1) + tb3);
    o->b[2] = st_fma(st_fma(b, 1.9390729665756226f, 0.04234214127063751f), m,
                     st_fma(m2, -0.04265959933400154f, n2) - f7);
    float n3 = (st_fma(t, -0.02097609080374241f, b3 + 0.000366351188858971f) - tb) + f2;
    o->b[3] = st_fma(st_fma(b, -0.6108677983283997f, -0.013175129890441895f), m,
                     st_fma(m2, 0.013439089991152287f, n3));
    float d0 = (st_fma(m2, -0.032702259719371796f, st_fma(m, -0.014712040312588215f, 0.05121073126792908f)) + f4) - f5;
    d0 = st_fma(st_fma(b, -0.4742704927921295f, st_fma(m, 0.004352737218141556f, -0.010919599793851376f)), t2, d0);
    d0 = st_fma(st_fma(st_fma(t, 0.2409207969903946f, 1.4864660501480103f), m,
                       st_fma(t, 0.5157092809677124f, 2.224760055541992f)), b, d0);
    o->a[0] = st_fma(st_fma(m2, -0.005300257820636034f, st_fma(m, -0.005796705838292837f, 0.011875949800014496f)), t,
                     d0);
    float d1 = (st_fma(b, -6.219334125518799f, -0.13867969810962677f) - f8) + f9;
    d1 = st_fma(st_fma(b, 1.4104410409927368f, 0.03150897100567818f), t2, d1);
    d1 = st_fma(st_fma(b, -1.4507700204849243f, -0.03235093131661415f), t, d1);
    d1 = st_fma(st_fma(t, 0.014858799986541271f, 0.09320034831762314f), m2, d1);
    float d1m = st_fma(st_fma(b, -0.6753997802734375f, 0.016858959570527077f), t,
                       st_fma(t2, -0.0133303701877594f, st_fma(b, -4.236380100250244f, 0.04232095927000046f)));
    o->a[1] = st_fma(d1m, m, d1);
    float d2 = (st_fma(m2, -0.08829893916845322f, st_fma(m, -0.04028676077723503f, 0.12479440122842789f)) + f8) - f9;
    d2 = st_fma(st_fma(b, -1.398069977760315f, st_fma(m, 0.013602529652416706f, -0.03027188964188099f)), t2, d2);
    d2 = st_fma(st_fma(st_fma(t, 0.6280372142791748f, 4.013587951660156f), m,
                       st_fma(t, 1.3566310405731201f, 5.7752461433410645f)), b, d2);
    o->a[2] = st_fma(st_fma(m2, -0.013816820457577705f, st_fma(m, -0.01627770997583866f, 0.029316719621419907f)), t,
                     d2);
    float d3 = (st_fma(b, -1.7806609869003296f, -0.037315189838409424f) - f4) + f5;
    d3 = st_fma(st_fma(b, 0.46189969778060913f, 0.009682518430054188f), t2, d3);
    d3 = st_fma(st_fma(b, -0.42157039046287537f, -0.008839381858706474f), t, d3);
    d3 = st_fma(st_fma(t, 0.004258280154317617f, 0.027800850570201874f), m2, d3);
    float d3m = st_fma(st_fma(b, -0.1935582011938095f, 0.005215453915297985f), t,
                       st_fma(t2, -0.004624894820153713f, st_fma(b, -1.2636749744415283f, 0.012677839957177639f)));
    o->a[3] = st_fma(d3m, m, d3);
}

static inline void dizzy_gain_set(struct fos *c, float *a0, float g, const struct dizzy_model *md)
{
    c->b0 = g * md->k;
    c->b1 = g * -md->k;
    c->a1 = st_fma(g, md->a1k, md->a1c);
    *a0 = g + md->a0c;
}

/* Presence below the taper's point 5 counts as 0.01 */
static inline float dizzy_presence_knob(float knob)
{
    return (int)(knob * 2.55f) < 5 ? 0.01f : st_taper(st_pow2, knob);
}

static inline void dizzy_init(struct dizzy_state *s, const float *k, const struct dizzy_model *md)
{
    *s = (struct dizzy_state){ 0 };
    s->gain = st_taper(st_pow2, k[DK_GAIN]);
    s->presence = st_taper(st_pow2, k[DK_PRESENCE]);
    s->volume = k[DK_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow2_5, k[DK_BASS]);
    s->middle = k[DK_MIDDLE] * 0.01f;
    s->treble = k[DK_TREBLE] * 0.01f;
    dizzy_gain_set(&s->gain_c, &s->gain_a0, s->gain, md);
    dizzy_tone_set(&s->tone, s->bass, s->middle, s->treble);
    dizzy_presence_set(&s->pres_c, dizzy_presence_knob(k[DK_PRESENCE]));
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static inline void dizzy_process(struct dizzy_state *s, const struct wvfx_io *io, const struct dizzy_model *md)
{
    float gain = st_taper(st_pow2, io->knobs[DK_GAIN]);
    float presence = st_taper(st_pow2, io->knobs[DK_PRESENCE]);
    float volume = io->knobs[DK_VOLUME] * 0.01f;
    float bass = st_taper(st_pow2_5, io->knobs[DK_BASS]);
    float middle = io->knobs[DK_MIDDLE] * 0.01f;
    float treble = io->knobs[DK_TREBLE] * 0.01f;
    float *p = io->io, y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->volume = st_glide(s->volume, volume);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    if (s->gain != gain) {
        s->gain = gain;
        dizzy_gain_set(&s->gain_c, &s->gain_a0, s->gain, md);
    }
    if (s->presence != presence) {
        s->presence = presence;
        dizzy_presence_set(&s->pres_c, dizzy_presence_knob(io->knobs[DK_PRESENCE]));
    }
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    sos_block(&DIZZY_PRE, &s->pre, y, n);
    for (int i = 0; i < n; i++)
        y[i] = fos_run_div(&s->gain_c, s->gain_a0, &s->gs, y[i]);
    sos_block_div(&md->lowpass, &s->lowpass, y, n);
    for (int i = 0; i < n; i++)
        y[i] = y[i] * md->drive;
    stage_pow2_block(&s->s1, y, n, md->lp1, md->ln1, md->kp1, md->kn1);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle) {
            s->bass = bass;
            dizzy_tone_set(&s->tone, s->bass, s->middle, s->treble);
        }
        y[i] = tone3_run_b1b0(&s->tone, y[i]) * s->volume;
    }
    stage_pow2_block(&s->s2, y, n, md->lp2, md->ln2, md->kp2, md->kn2);
    sos_block(&s->pres_c, &s->pres, y, n);
    for (int j = 0; j < 6; j++)                     /* the power amp's fixed sections */
        sos_block(&DIZZY_POWER[j], &s->power[j], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i] * md->out;
}
