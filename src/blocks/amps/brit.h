/*
 * The pieces the original's Brit amps share (docs/stock_port.md): the tone stack's polynomials, the presence
 * shelves, the stages, the tables two or more models share, the knobs; the generic ones are in amp.h. Each
 * model's file strings them in its order with its tables.
 */
#pragma once
#include "amp.h"

#define BRIT50_TS(F) { \
    F(0.02127887), F(0.0005319717), F(0.01820065), F(-0.0004550163), \
    { F(0.02373404), F(-0.06653748), F(0.06202353), F(-0.01922009) }, \
    { F(0.02603876), F(-0.07602267), F(0.07396124), F(-0.02397733) }, \
    { F(0.9322378), F(-2.647389), F(2.498065), F(-0.7829137) }, \
    { F(-0.02330595), F(0.06618473), F(-0.06245163), F(0.01957284) }, \
    { F(1.117771), F(-3.114693), F(2.882229), F(-0.8853068) }, \
    { F(0.02908328), F(-0.07878821), F(0.07091983), F(-0.02120868) }, \
    { F(-0.003678272), F(0.01087483), F(-0.01056415), F(0.003367595) } }

#define BRIT45_TS(F) { \
    F(0.01638156), F(0.000409539), F(0.01293493), F(-0.0003233732), \
    { F(0.01585626), F(-0.04309267), F(0.0387866), F(-0.01155019) }, \
    { F(0.02613953), F(-0.07612805), F(0.07386047), F(-0.02387195) }, \
    { F(0.6224268), F(-1.715284), F(1.563288), F(-0.4704304) }, \
    { F(-0.01556067), F(0.0428821), F(-0.03908219), F(0.01176076) }, \
    { F(1.18435), F(-3.180903), F(2.81565), F(-0.8190966) }, \
    { F(0.03093968), F(-0.0804573), F(0.06906418), F(-0.01953884) }, \
    { F(-0.01334295), F(0.03610654), F(-0.03201419), F(0.009250599) } }

static const struct brit_ts BRIT50_TS_F = BRIT50_TS(ST_F);
static const struct brit_ts_d BRIT50_TS_D = BRIT50_TS(ST_D);
static const struct brit_ts BRIT45_TS_F = BRIT45_TS(ST_F);
static const struct brit_ts_d BRIT45_TS_D = BRIT45_TS(ST_D);

/* ---------------------------------------------------------------- presence */

/* the presence shelf from k = 2^(knob x 0.522...): b = 1 +- 1.4734209 k, a = 1 +- 1.4734209 / k */
static inline void presence_shelf(struct sos *c, float k)
{
    float p = k * 1.4734209f, q = 1.4734209f / k;
    *c = (struct sos){ p + 1.0f, 0.55538225f, 1.0f - p, q + 1.0f, 0.55538225f, 1.0f - q };
}

/* the knob (pow2) times 6.29 in double, then 0.0830482 */
static inline void presence_set(struct sos *c, float knob)
{
    presence_shelf(c, st_exp2f((float)((double)knob * 6.29) * 0.0830482f));
}

/* the knob times 0.5223732 (6.29 x 0.0830482 folded, in single precision) */
static inline void presence_set_f(struct sos *c, float knob)
{
    presence_shelf(c, st_exp2f(knob * 0.5223732f));
}

/* the Brit 45s' shelf: b = 1 +- 1.5405309 k, a = 1 +- 1.5405309 / k, b1 = a1 = 1.7735938 */
static inline void presence45_shelf(struct sos *c, float k)
{
    float p = k * 1.5405309f, q = 1.5405309f / k;
    *c = (struct sos){ p + 1.0f, 1.7735938f, 1.0f - p, q + 1.0f, 1.7735938f, 1.0f - q };
}

/* ---------------------------------------------------------------- the Brit 45s (45, 45+, 45JP) */

static const struct sos BRIT45_PRE[3] = {
    { 8.77729f, -12.000281f, 4.11549f, 1.0f, -1.6727732f, 0.6729386f },
    { 6.998212f, 9.254746f, 2.7350597f, 1.0f, -0.9045531f, 0.1522906f },
    { 11.6897f, -12.000281f, 0.3121885f, 1.0f, 0.878456f, 0.0216278f },
};
static const struct sos BRIT45_POWER[4] = {
    { 0.7043406f, -1.1775819f, 0.4734121f, 1.0f, -1.3381104f, 0.3537903f },
    { 1.1365529f, -1.1775819f, 0.0781216f, 1.0f, -1.0383676f, 0.0397717f },
    { 1.1775819f, 0.0307937f, 0.3011942f, 1.0f, 0.7462798f, 0.4583994f },
    { 1.1775819f, 0.6003372f, 0.0830306f, 1.0f, -0.2796038f, 0.0102194f },
};

/* normal channel (45, 45JP): a 1st-order gain stage (x 0.9994334), a low pass */
static const float BRIT45_NRM_GAIN_K = 0.9994334f, BRIT45_NRM_GAIN_A1 = -0.9988669f;
static const struct sos BRIT45_NRM_LOWPASS = { 0.01137501f, 0.02275002f, 0.01137501f, 1.1499934f, -1.9545f,
                                               0.8500066f };

/* bright channel (45+, 45JP): a 3rd-order gain stage, two pre-EQ sections */
static const struct sos BRIT45_BRT_EQ1 = { 1.4286703f, -1.2779781f, 0.0114836f, 1.0f, -0.8097989f, 0.0099058f };
static const struct sos BRIT45_BRT_EQ2 = { 1.4286703f, 0.11004f, 0.0001405f, 1.0f, 0.3479484f, 0.3916395f };

/* the bright gain stage's coefficients from the Gain g: bi = g^2 qi + g ni, ai = g^2 qi + g di + ki (q0 = 1), in
   double, g^2 taken in single precision */
static inline void brit45_brt_gain(struct tone3 *s, float gain)
{
    static const double Q[4] = { 1.0, -2.845045, 2.690091, -0.8450453 };
    static const double N[4] = { -1.113379, 2.940855, -2.576712, 0.7492352 };
    static const double D[4] = { -0.9994331, 2.844566, -2.690658, 0.8455244 };
    static const double K[4] = { -0.1140099, 0.09621494, 0.1139899, -0.09623486 };
    double g = gain, g2 = (float)(gain * gain);
    for (int i = 0; i < 4; i++) {
        double q = i ? g2 * Q[i] : g2;
        s->b[i] = (float)st_fmad(g, N[i], q);
        s->a[i] = (float)(st_fmad(g, D[i], K[i]) + q);
    }
}

/* the presence shelf of the 45+ and 45JP: k = 2^(knob x 22.7 (in double) x 0.0830482) */
static inline void presence45_set_d(struct sos *c, float knob)
{
    presence45_shelf(c, st_exp2f((float)((double)knob * 22.7) * 0.0830482f));
}

/* the Brit 45s' stages: +-40 (k 0.2) and +-1.5 (k 7 / 1024) */
static inline void stage40_block(struct stage *s, float *y, int n) { stage_exp_block(s, y, n, 40.0f, 0.2f); }
static inline void stage15_block(struct stage *s, float *y, int n) { stage_pow_block(s, y, n, 1.5f, 0.0068359375f); }

/* ---------------------------------------------------------------- the Brit 50's two channels (50, 50+, 50JP) */

/* normal channel: pre-filters, a 1st-order gain stage, the "low pass" (its denominator as numerator too: it
   passes the signal to rounding) */
static const struct sos BRIT50_NRM_PRE1 = { 19.169096f, -35.669075f, 16.500563f, 1.0f, -1.8660856f, 0.8661647f };
static const struct sos BRIT50_NRM_PRE2 = { 35.669075f, 18.477316f, 0.0007657f, 1.0f, -0.0994939f, 5e-07f };
static const float BRIT50_NRM_GAIN_A1 = -0.9989698f;
static const struct sos BRIT50_NRM_LOWPASS = { 1.1499934f, -1.9545f, 0.8500066f, 1.1499934f, -1.9545f, 0.8500066f };

/* bright channel: pre-filters, a 2nd-order gain stage, two pre-EQ sections */
static const struct sos BRIT50_BRT_PRE1 = { 18.518007f, -36.772133f, 18.255064f, 1.0f, -1.9636128f, 0.9640702f };
static const struct sos BRIT50_BRT_PRE2 = { 36.772133f, 19.01352f, 0.0151183f, 1.0f, -0.1057833f, 1.92e-05f };
static const float BRIT50_BRT_GAIN_A1 = -1.892106f, BRIT50_BRT_GAIN_A2 = 0.8931068f;
static const struct sos BRIT50_BRT_EQ1 = { 4.8064f, 0.5554f, -2.8064f, 2.5154f, 0.5554f, -0.5154f };
static const struct sos BRIT50_BRT_EQ2 = { 1.017f, -1.9992f, 0.983f, 1.0239f, -1.9992f, 0.9761f };

/* the power amp */
static const struct sos BRIT50_POWER[6] = {
    { 0.7842386f, -1.2284685f, 0.4442597f, 1.0f, -1.995556f, 0.9958516f },
    { 0.6165967f, -1.2284685f, 0.6120484f, 1.0f, -1.4434046f, 0.4481079f },
    { 0.9167726f, -1.2284685f, 0.3243508f, 1.0f, -1.3559952f, 0.3614594f },
    { 1.2284685f, -0.1838129f, 1.0167582f, 1.0f, -0.127902f, 0.8375524f },
    { 0.8708184f, -1.2284685f, 0.4889923f, 1.0f, 1.0680888f, 0.4243044f },
    { 1.2284685f, 0.9576381f, 0.0671688f, 1.0f, -1.4902158f, 0.656683f },
};

/* ---------------------------------------------------------------- the knobs of a 6-knob Brit */

enum { BK_GAIN, BK_PRESENCE, BK_VOLUME, BK_BASS, BK_MIDDLE, BK_TREBLE, BK_GAIN2 };

#define BRIT_PARAM(name, def, knob) { name, 0.0f, 100.0f, 1.0f, def, knob, 0x15, 0, 0, 0, 0 }

/* what the knobs ask for, through the original's tapers */
struct brit_knobs { float gain, gain2, presence, volume, bass, middle, treble; };

static inline struct brit_knobs brit_knobs_t(const float *k, const float *gain_taper, const float *bass_taper)
{
    struct brit_knobs t;
    t.gain = st_taper(gain_taper, k[BK_GAIN]);
    t.gain2 = st_taper(gain_taper, k[BK_GAIN2]);
    t.presence = st_taper(st_pow2, k[BK_PRESENCE]);
    t.volume = k[BK_VOLUME] * 0.01f;
    t.bass = st_taper(bass_taper, k[BK_BASS]);
    t.middle = k[BK_MIDDLE] * 0.01f;
    t.treble = k[BK_TREBLE] * 0.01f;
    return t;
}

/* Gain through `gain_taper`, Bass through pow2 */
static inline struct brit_knobs brit_knobs(const float *k, const float *gain_taper)
{
    return brit_knobs_t(k, gain_taper, st_pow2);
}
