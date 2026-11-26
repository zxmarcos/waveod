/*
 * The pieces the original's Dragon amps share (docs/stock_port.md): the knobs and their glide, the tone stack, the
 * state, the path after the clipping stage. Each model's file has its own front end (pre-filters and gain section).
 *
 * Per sample:
 *   pre-filters   two fixed 2nd-order sections
 *   gain          the model's (CL: a high pass and a 2nd-order section from Gain; CL B: the 2nd-order section in
 *                 double; OD: x 5 Gain, a fixed 3rd-order section and a low pass)
 *   stage         a power clipper on 4 points per sample, the FIR
 *   tone stack    3rd order, polynomials in Bass (pow3), Middle (pow2) and Treble (linear) with their squares
 *   power amp     five fixed 2nd-order sections, x Volume x the model's output
 * Every knob glides 0.0001 per sample. The gain and tone coefficients are recomputed only while the glide moves (the
 * glided value differs from the knob), so they stop one glide step short of the knob; the glide goes on while the
 * block is bypassed, the coefficients do not.
 */
#pragma once
#include "amp.h"

enum { DR_GAIN, DR_VOLUME, DR_BASS, DR_MIDDLE, DR_TREBLE };

#define DRAGON_PARAMS(gain) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), DR_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, DR_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, DR_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, DR_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, DR_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

/* the CL's and CL B's pre-filters and power amp */
static const struct sos DRAGON_CL_PF[2] = {
    { 5.015294551849365f, -8.989697456359863f, 3.9755303859710693f, 1.0f, -1.7943259477615356f,
      0.7951639294624329f },
    { 8.989697456359863f, 8.550085067749023f, 1.5715818405151367f, 1.0f, 0.9328657984733582f,
      0.1650736927986145f },
};

static const struct sos DRAGON_CL_POWER[5] = {
    { 0.007313000038266182f, 0.0003964999923482537f, 8.999999749903509e-07f, 1.0f, -1.616722583770752f,
      0.6703299880027771f },
    { 2.1148414611816406f, -4.110466480255127f, 2.0010733604431152f, 1.0f, -1.933767318725586f,
      0.9382646083831787f },
    { 4.110466480255127f, -0.5090013146400452f, 0.9124333262443542f, 1.0f, 0.1280342936515808f,
      0.0021166999358683825f },
    { 2.4046902656555176f, -4.110466480255127f, 1.8207920789718628f, 1.0f, 0.2705906927585602f,
      0.050422899425029755f },
    { 2.6103687286376953f, 4.110466480255127f, 1.8066260814666748f, 1.0f, 1.60822594165802f,
      0.7152588963508606f },
};

/* the knobs after their tapers (Gain's is the model's) */
struct dragon_knobs { float gain, volume, bass, middle, treble; };

static inline void dragon_knobs(struct dragon_knobs *o, const float *k, float gain)
{
    o->gain = gain;
    o->volume = k[DR_VOLUME] * 0.01f;
    o->bass = st_taper(st_pow3, k[DR_BASS]);
    o->middle = st_taper(st_pow2, k[DR_MIDDLE]);
    o->treble = k[DR_TREBLE] * 0.01f;
}

struct dragon_state {
    struct dragon_knobs at;             /* glided */
    struct sos_state pf[2];
    struct sos_state hp;                /* the CL's high pass */
    struct sos gain_c;                  /* the CL's and CL B's gain section */
    struct sos_state gs;
    struct tone3 gain3;                 /* the OD's gain section */
    struct sos_state lowpass;           /* the OD's low pass */
    struct stage stage;
    struct tone3 tone;
    struct sos_state power[5];
};

/* one glide step of the knobs after the stage (all but the Gain); whether the tone stack's knobs are still moving */
static inline int dragon_glide_post(struct dragon_state *s, const struct dragon_knobs *k)
{
    s->at.volume = st_glide(s->at.volume, k->volume);
    s->at.bass = st_glide(s->at.bass, k->bass);
    s->at.middle = st_glide(s->at.middle, k->middle);
    s->at.treble = st_glide(s->at.treble, k->treble);
    return s->at.bass != k->bass || s->at.middle != k->middle || s->at.treble != k->treble;
}

/* one glide step of every knob; whether the tone stack's knobs are still moving */
static inline int dragon_glide(struct dragon_state *s, const struct dragon_knobs *k)
{
    s->at.gain = st_glide(s->at.gain, k->gain);
    s->at.volume = st_glide(s->at.volume, k->volume);
    s->at.bass = st_glide(s->at.bass, k->bass);
    s->at.middle = st_glide(s->at.middle, k->middle);
    s->at.treble = st_glide(s->at.treble, k->treble);
    return s->at.bass != k->bass || s->at.middle != k->middle || s->at.treble != k->treble;
}

/* the tone stack's constants (amp.h, tmb_tone_set) */
static const struct tmb_ts DRAGON_TS = {
    .nb0 = 0.01405697, .nk0 = 0.0002811394, .nb1 = 0.01047075, .nk1 = 0.0002094149, .tm = 0.02, .tm3 = 0.06,
    .nt = { 0.02103462, -0.06102681, 0.05896538, -0.01897319 },
    .nm2 = { -0.009092276, 0.02449335, -0.02170986, 0.006308793 },
    .nmb = { 0.4546138, -1.224667, 1.085493, -0.3154397 },
    .nmk = { 0.009279359, -0.02460941, 0.02152278, -0.006192725 },
    .e = 0.005882353, .e3 = 0.01764706, .f = 0.2941176, .f3 = 0.8823529,
    .d0 = { .k = 0.08242429, .m = -0.04605681, .m2 = -0.03077779, .kt2 = -0.0211862, .mt2 = 0.01396607,
            .bt2 = -1.007579, .kb = 3.92096, .tb = 1.238905, .mb = 1.538889, .tmb = 0.2714177, .kt = 0.02605508,
            .mt = -0.01906963, .m2t = -0.005428353 },
    .d1 = { .k = -0.2060584, .b = -10.18019, .kt2 = 0.06117839, .bt2 = 3.007579, .kt = -0.06549041, .bt = -3.23539,
            .km2 = 0.08271105, .tm2 = 0.0135567, .km = 0.1198008, .bm = -4.135553, .t2m = -0.04220137,
            .tm = 0.05101231, .btm = -0.6778348 },
    .d2 = { .k = 0.1684966, .m = -0.1009728, .m2 = -0.07310165, .kt2 = -0.0588138, .mt2 = 0.04250452,
            .bt2 = -2.992421, .kb = 8.624511, .tb = 2.761095, .mb = 3.655082, .tmb = 0.5414166, .kt = 0.05394858,
            .mt = -0.04467368, .m2t = -0.01082833 },
    .d3 = { .k = -0.04483886, .b = -2.365245, .kt2 = 0.01882161, .bt2 = 0.9924212, .kt = -0.01450593,
            .bt = -0.7646104, .km2 = 0.02116839, .tm2 = 0.002699989, .km = 0.02722884, .bm = -1.058419,
            .t2m = -0.01426922, .tm = 0.01273101, .btm = -0.1349995 },
};

/* the tone stack from the glided knobs: in single precision, or in double (the CL B) */
static inline void dragon_tone_set(struct dragon_state *s, int tone_double)
{
    if (tone_double)
        tmb_tone_set_d(&s->tone, &DRAGON_TS, s->at.bass, s->at.middle, s->at.treble);
    else
        tmb_tone_set(&s->tone, &DRAGON_TS, s->at.bass, s->at.middle, s->at.treble);
}

/* the state at Init: every knob at its target, the tone stack from them */
static inline void dragon_init(struct dragon_state *s, const float *k, float gain, int tone_double)
{
    *s = (struct dragon_state){ 0 };
    dragon_knobs(&s->at, k, gain);
    dragon_tone_set(s, tone_double);
}

/* after the stage: the tone stack, the power amp, x Volume x out */
static inline float dragon_tail(struct dragon_state *s, float x, const struct sos *power, float out)
{
    x = tone3_run_b1b0(&s->tone, x);
    for (int i = 0; i < 5; i++)
        x = sos_run(&power[i], &s->power[i], x);
    return s->at.volume * out * x;
}
