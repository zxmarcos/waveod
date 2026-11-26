/*
 * The pieces the original's Doctor amps share (docs/stock_port.md): the knobs and their glide, the pre-filters, the
 * tone stack, the Tone Cut section, the post section, the state. Each model's file has its stage and levels.
 *
 * Per sample:
 *   input         x Gain x 29.4
 *   pre-filters   three fixed 2nd-order sections
 *   tone stack    3rd order from Bass (A25_0_0_8), Middle and Treble (B25)
 *   stage         the model's: the clip +-1.275 on 4 points + FIR (CL), the 8x oversampled clip +-1 (OD)
 *   tone cut      a 1st-order section from Tone Cut (B25), divided by its a0
 *   post          a fixed 2nd-order section, x Volume (B25) x the model's level
 * Every knob glides 0.0001 per sample; the tone and cut coefficients stop one glide step short of the knobs.
 */
#pragma once
#include "amp.h"

enum { DR_GAIN, DR_CUT, DR_VOLUME, DR_BASS, DR_MIDDLE, DR_TREBLE };

#define DOCTOR_PARAMS(gain) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), DR_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Tone Cut", 0.0f, 100.0f, 1.0f, 50.0f, DR_CUT, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, DR_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, DR_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, DR_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, DR_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

static const struct sos DOCTOR_PF[3] = {
    { 0.4616096019744873f, -0.8037999868392944f, 0.3427189886569977f, 1.0f, -1.777618646621704f,
      0.7801463007926941f },
    { 0.8037999868392944f, -0.316558301448822f, 0.0421799011528492f, 1.0f, -0.7825261950492859f,
      0.17750640213489532f },
    { 0.8037999868392944f, 0.36757931113243103f, 0.01685580052435398f, 1.0f, -0.15728940069675446f,
      0.012856200337409973f },
};

static const struct sos DOCTOR_POST = { -2.082494020462036f, 3.7553861141204834f, -1.6728919744491577f, 1.0f,
                                        -1.5875170230865479f, 0.5880330801010132f };

/* the knobs after their tapers (Gain before the model's factor) */
struct doctor_knobs { float gain, cut, volume, bass, middle, treble; };

static inline void doctor_knobs(struct doctor_knobs *o, const float *k)
{
    o->gain = st_taper(st_b25, k[DR_GAIN]);
    o->cut = st_taper(st_b25, k[DR_CUT]);
    o->volume = st_taper(st_b25, k[DR_VOLUME]);
    o->bass = st_taper(st_a25_0_0_8, k[DR_BASS]);
    o->middle = st_taper(st_b25, k[DR_MIDDLE]);
    o->treble = st_taper(st_b25, k[DR_TREBLE]);
}

struct doctor_state {
    struct doctor_knobs at;                 /* glided */
    struct sos_state pf[3], post;
    struct tone3 tone;
    struct fos cut_c;
    float cut_a0;
    struct fos_state cut;
};

/*
 * The tone stack from Bass b, Middle m and Treble t (tb = t b, mb = m b; c = (m 0.000356 + b 0.00651) + t 0.00142,
 * e = m 0.000264 + t 0.00142, both fused):
 *   n0 = (tb + 0.251 mb) + c                  n1 = (-0.688 mb - 0.00494 b) - (3 tb + e)
 *   n2 = (0.623 mb - c) + 3 tb                n3 = ((-0.186 mb + 0.00494 b) - tb) + e
 *   d0 = (1.188 b + 0.00168 + m 0.000356) + 0.251 mb     d1 = ((-3.186 b - 0.00116) - m 0.000264) - 0.688 mb
 *   d2 = ((2.812 b - 0.00168) - m 0.000356) + 0.623 mb   d3 = (-0.814 b + 0.00116 + m 0.000264) - 0.186 mb
 */
static inline void doctor_tone_set(struct tone3 *o, float b, float m, float t)
{
    float m1 = m * 0.00035605730954557657f, m2 = m * 0.0002640034072101116f;
    float mb = m * b, tb = t * b, tb3 = tb * 3.0f;
    float c = st_fma(t, 0.0014203930040821433f, st_fma(b, 0.006509535945951939f, m1));
    float e = st_fma(t, 0.0014164859894663095f, m2);
    float mb0 = mb * 0.2510462999343872f, mb1 = mb * -0.6881892085075378f;
    float mb2 = mb * 0.6232393980026245f, mb3 = mb * -0.18609650433063507f;
    o->b[0] = (tb + mb0) + c;
    o->b[1] = st_fma(b, -0.004942744970321655f, mb1) - (tb3 + e);
    o->b[2] = (mb2 - c) + tb3;
    o->b[3] = (st_fma(b, 0.004942744970321655f, mb3) - tb) + e;
    o->a[0] = (st_fma(b, 1.1879149675369263f, 0.0016780609730631113f) + m1) + mb0;
    o->a[1] = (st_fma(b, -3.1863489151000977f, -0.0011581099824979901f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.812084913253784f, -0.0016766430344432592f) - m1) + mb2;
    o->a[3] = (st_fma(b, -0.8136513829231262f, 0.0011595269897952676f) + m2) + mb3;
}

/* the Tone Cut section from c: (-0.0524, -0.0524) / (c + 0.0902, 0.0147 - c) */
static inline void doctor_cut_set(struct doctor_state *s, float c)
{
    s->cut_c.b0 = -0.052431873977184296f;
    s->cut_c.b1 = -0.052431873977184296f;
    s->cut_a0 = c + 0.09016772359609604f;
    s->cut_c.a1 = 0.014696025289595127f - c;
}

/* the state from the knobs; gain is the model's (the taper times its factor) */
static inline void doctor_init(struct doctor_state *s, const struct doctor_knobs *k)
{
    *s = (struct doctor_state){ 0 };
    s->at = *k;
    doctor_tone_set(&s->tone, k->bass, k->middle, k->treble);
    doctor_cut_set(s, k->cut);
}

/* one sample's glide (also while bypassed) */
static inline void doctor_glide(struct doctor_state *s, const struct doctor_knobs *k)
{
    struct doctor_knobs *a = &s->at;
    a->gain = st_glide(a->gain, k->gain);
    a->cut = st_glide(a->cut, k->cut);
    a->volume = st_glide(a->volume, k->volume);
    a->bass = st_glide(a->bass, k->bass);
    a->middle = st_glide(a->middle, k->middle);
    a->treble = st_glide(a->treble, k->treble);
}

/* the input through the pre-filters and the tone stack */
static inline float doctor_pre(struct doctor_state *s, float x)
{
    x = s->at.gain * 29.400001525878906f * x;
    for (int i = 0; i < 3; i++)
        x = sos_run(&DOCTOR_PF[i], &s->pf[i], x);
    return tone3_run_std(&s->tone, x);
}

/* the post section and the output level, after the Tone Cut */
static inline float doctor_post(struct doctor_state *s, float x, float level)
{
    x = sos_run(&DOCTOR_POST, &s->post, x);
    return s->at.volume * level * x;
}
