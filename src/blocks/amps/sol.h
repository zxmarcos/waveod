/*
 * The pieces the original's Sol 100 amps share (docs/stock_port.md): the knobs and their glide, the presence, the
 * tone stacks' constants, the state. Each model's file has its own path.
 *
 * Per sample:
 *   pre-filters   two fixed 2nd-order sections
 *   gain          the model's (CL, OD: x Gain (pow2_5) and a fixed section; LD: a 3rd-order section from Gain), the
 *                 OD's and LD's low pass
 *   stage         a power clipper on 4 points per sample, the FIR
 *   tone stack    3rd order from Bass (pow3), Middle and Treble (linear): the full one with Treble^2 terms (CL in
 *                 double, OD), the classic one (LD); x Volume
 *   presence      two shelves from Presence (pow2)
 *   power amp     the model's sections, x its output
 * Gain, Volume, Middle and Treble glide 0.0001 per sample; Bass and Presence take effect at once. The tone
 * coefficients stop one glide step short of the knobs (and the LD's gain section of Gain).
 */
#pragma once
#include "amp.h"

enum { SL_GAIN, SL_PRESENCE, SL_VOLUME, SL_BASS, SL_MIDDLE, SL_TREBLE };

#define SOL_PARAMS(gain) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), SL_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, SL_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, SL_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, SL_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, SL_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, SL_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

/* the CL's and OD's pre-filters */
static const struct sos SOL_PF[2] = {
    { 11.725530624389648f, -23.047094345092773f, 11.321643829345703f, 1.0f, -1.8984003067016602f,
      0.9020028710365295f },
    { 23.047094345092773f, 12.095137596130371f, 0.008084200322628021f, 1.0f, -0.14529240131378174f,
      1.4700000065204222e-05f },
};

/* the CL's and OD's tone stack (amp.h, tmb_tone_set) */
static const struct tmb_ts SOL_TS = {
    .nb0 = 0.01778791, .nk0 = 0.0004446978, .nb1 = 0.01540551, .nk1 = 0.0003851377, .tm = 0.025, .tm3 = 0.075,
    .nt = { 0.02614062, -0.07612695, 0.07385938, -0.02387305 },
    .nm2 = { -0.01711117, 0.04870692, -0.04608032, 0.01448457 },
    .nmb = { 0.6844468, -1.948277, 1.843213, -0.5793829 },
    .nmk = { 0.01744181, -0.04897936, 0.04574968, -0.01421213 },
    .e = 0.003324468, .e3 = 0.009973404, .f = 0.1329787, .f3 = 0.3989362,
    .d0 = { .k = 0.02913896, .m = -0.009276932, .m2 = -0.01780474, .kt2 = -0.006610541, .mt2 = 0.002850147,
            .bt2 = -0.2530154, .kb = 1.115531, .tb = 0.2762889, .mb = 0.7121898, .tmb = 0.1205086,
            .kt = 0.007219893, .mt = -0.003757703, .m2t = -0.003012716 },
    .d1 = { .k = -0.07883841, .b = -3.112914, .kt2 = 0.01910712, .bt2 = 0.7530154, .kt = -0.01964691,
            .bt = -0.7757206, .km2 = 0.05064903, .tm2 = 0.008411652, .km = 0.02649083, .bm = -2.025961,
            .t2m = -0.008701211, .tm = 0.01087332, .btm = -0.3364661 },
    .d2 = { .k = 0.07086374, .m = -0.02503158, .m2 = -0.04788674, .kt2 = -0.01838946, .mt2 = 0.008851981,
            .bt2 = -0.7469846, .kb = 2.884472, .tb = 0.7237111, .mb = 1.91547, .tmb = 0.3114063,
            .kt = 0.01778077, .mt = -0.01044442, .m2t = -0.007785157 },
    .d3 = { .k = -0.02115889, .b = -0.8870838, .kt2 = 0.005892878, .bt2 = 0.2469846, .kt = -0.005352433,
            .bt = -0.2242794, .km2 = 0.01504245, .tm2 = 0.00238622, .km = 0.007817676, .bm = -0.6016982,
            .t2m = -0.003000917, .tm = 0.003328812, .btm = -0.09544881 },
};

/* the presence's two shelves (amp.h, shelves_set) */
#define SOL_PRES_K 0.023655403405427933f
#define SOL_PRES_G 0.047310806810855865f

/* Presence after its taper, held at 0.01 below the taper's point 5 */
static inline float sol_presence_knob(float knob)
{
    return (int)(knob * 2.55f) < 5 ? 0.01f : st_taper(st_pow2, knob);
}

struct sol_state {
    float gain, volume, middle, treble;     /* glided */
    float bass, presence;                   /* the knobs the sections were computed for */
    struct sos_state pf[2], pf3, lowpass;
    struct tone3 gain3;                     /* the LD's gain section */
    struct stage stage;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pres, power[5];
};

/* the state at Init: the knobs at their targets (the sections are the model's to set) */
static inline void sol_init(struct sol_state *s, const float *k)
{
    *s = (struct sol_state){ 0 };
    s->gain = st_taper(st_pow2_5, k[SL_GAIN]);
    s->volume = k[SL_VOLUME] * 0.01f;
    s->middle = k[SL_MIDDLE] * 0.01f;
    s->treble = k[SL_TREBLE] * 0.01f;
    s->bass = st_taper(st_pow3, k[SL_BASS]);
    s->presence = sol_presence_knob(k[SL_PRESENCE]);
}

/* one glide step of the knobs after the stage; whether the tone stack needs recomputing (Bass, taken at once, or
   Middle or Treble moving) */
static inline int sol_glide_post(struct sol_state *s, const float *k, float bass)
{
    s->volume = st_glide(s->volume, k[SL_VOLUME] * 0.01f);
    float middle = k[SL_MIDDLE] * 0.01f, treble = k[SL_TREBLE] * 0.01f;
    s->middle = st_glide(s->middle, middle);
    s->treble = st_glide(s->treble, treble);
    return s->bass != bass || s->middle != middle || s->treble != treble;
}

/* one glide step of every knob (the Gain before the stage, sol_glide_post's after it) */
static inline int sol_glide(struct sol_state *s, const float *k, float bass)
{
    s->gain = st_glide(s->gain, st_taper(st_pow2_5, k[SL_GAIN]));
    return sol_glide_post(s, k, bass);
}
