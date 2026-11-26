/*
 * The pieces the original's Dizzy VH S and VH+ S share (docs/stock_port.md): one path with one clipping stage, the
 * same tables, the model's low pass, clipper and output in a struct dizzy_s_model. The VH's gain section and
 * presence are in dizzy.h.
 *
 * Per sample:
 *   pre-filter    a fixed 2nd-order section
 *   gain          the model's 1st-order section from Gain (pow2) (the VH's or VH+'s, in double), its low pass
 *   stage         the model's clip_pow (L - L (1 - k |x|)^1024, odd) on 4 points per sample, the FIR
 *   tone stack    the VH's polynomials in double (dizzy_s_tone_set), x Volume
 *   presence      the VH's two shelves from Presence (pow2; 0 counts as 0.01, Init holds it below point 5)
 *   power amp     seven fixed 2nd-order sections, x the model's output
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample, Presence takes effect at once; the coefficients
 * stop one glide step short of the knobs.
 */
#pragma once
#include "dizzy.h"

static const struct sos DIZZY_S_POWER[7] = {
    { 0.8565025925636292f, -1.7099939584732056f, 0.853499174118042f, 1.0f, -1.9883924722671509f,
      0.9892203211784363f },
    { 0.10203669965267181f, -0.2032058984041214f, 0.10125330090522766f, 1.0f, -1.9971660375595093f,
      0.9971851110458374f },
    { 0.876863420009613f, -1.7398810386657715f, 0.8631563186645508f, 1.0f, -1.9943724870681763f,
      0.9944459199905396f },
    { 0.8885962963104248f, -1.7398810386657715f, 0.8514568209648132f, 1.0f, -1.969735860824585f,
      0.9698160886764526f },
    { 1.4186842441558838f, -1.7398810386657715f, 0.33105459809303284f, 1.0f, 0.08913630247116089f,
      0.0006888000061735511f },
    { 1.7398810386657715f, 0.03644770011305809f, 0.0027757000643759966f, 1.0f, 0.14426730573177338f,
      0.017245400696992874f },
    { 1.7398810386657715f, 0.3280707001686096f, 0.025567999109625816f, 1.0f, -1.2249666452407837f,
      0.24213260412216187f },
};

/* a model's gain section, the VH's shape in double: b0 = g k, b1 = -g k, a1 = g a1k + a1c (fused), a0 = g + a0c */
struct dizzy_s_gain { double k, a1k, a1c, a0c; };

static inline void dizzy_s_gain_set(struct fos *c, float *a0, float gf, const struct dizzy_s_gain *k)
{
    double g = gf;
    c->b0 = (float)(g * k->k);
    c->b1 = (float)(g * -k->k);
    c->a1 = (float)st_fmad(g, k->a1k, k->a1c);
    *a0 = (float)(g + k->a0c);
}

/* a model: its gain section, low pass, clip_pow L and k, output */
struct dizzy_s_model {
    struct dizzy_s_gain gain;
    struct sos lowpass;
    float L, k, out;
};

struct dizzy_s_state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the value the presence section was computed for */
    struct fos gain_c;
    float gain_a0;
    struct tone3 tone;
    struct sos pres_c;
    struct sos_state pre, lowpass, pres, power[7];
    struct fos_state gs;
    struct stage st;
};

/* the VH's tone stack in double from single-precision knobs; m^2, t b (in n0) and t^2 taken in single precision,
   n3's Treble terms grouped as (-0.0209761 - b) t */
static inline void dizzy_s_tone_set(struct tone3 *o, float bf, float mf, float tf)
{
    double b = bf, m = mf, t = tf, m2 = (float)(mf * mf), t2 = (float)(tf * tf), tbf = (float)(tf * bf);
    double tm = t * m, tb3 = (t * b) * 3.0, mb = m * b, t2m2 = t2 * m2;
    double b1 = b * 0.01914641, b3 = b * 0.01665233, f2 = tm * 0.022, f7 = tm * 0.066;
    double f4 = t2m2 * 0.005809056, f5 = (mb * 0.264048) * t2, f8 = t2m2 * 0.01742717, f9 = (mb * 0.792144) * t2;
    double n0 = st_fmad(m2, -0.01578142, st_fmad(t, 0.02303753, b1 + 0.000421221) + tbf) - f2;
    o->b[0] = (float)st_fmad(st_fmad(b, 0.7173373, 0.01609889), m, n0);
    double n1 = st_fmad(t, -0.06702391, -0.0003663512 - (b3 + tb3)) + f7;
    o->b[1] = (float)st_fmad(st_fmad(b, -2.045542, -0.04526589), m, st_fmad(m2, 0.04500193, n1));
    double n2 = st_fmad(m2, -0.0426596, st_fmad(t, 0.06496247, (-0.000421221 - b1) + tb3)) - f7;
    o->b[2] = (float)st_fmad(st_fmad(b, 1.939073, 0.04234214), m, n2);
    double n3 = st_fmad(st_fmad(b, -0.6108678, -0.01317513), m, st_fmad(m2, 0.01343909, (b3 + 0.0003663512) + f2));
    o->b[3] = (float)st_fmad(-0.02097609 - b, t, n3);
    double d0 = (st_fmad(m2, -0.03270226, st_fmad(m, -0.01471204, 0.05121073)) + f4) - f5;
    d0 = st_fmad(st_fmad(b, -0.4742705, st_fmad(m, 0.004352737, -0.0109196)), t2, d0);
    d0 = st_fmad(st_fmad(st_fmad(t, 0.2409208, 1.486466), m, st_fmad(t, 0.5157093, 2.22476)), b, d0);
    o->a[0] = (float)st_fmad(st_fmad(m2, -0.005300258, st_fmad(m, -0.005796706, 0.01187595)), t, d0);
    double d1 = (st_fmad(b, -6.219334, -0.1386797) - f8) + f9;
    d1 = st_fmad(st_fmad(b, 1.410441, 0.03150897), t2, d1);
    d1 = st_fmad(st_fmad(b, -1.45077, -0.03235093), t, d1);
    d1 = st_fmad(st_fmad(t, 0.0148588, 0.09320035), m2, d1);
    double d1m = st_fmad(st_fmad(b, -0.6753998, 0.01685896), t,
                         st_fmad(t2, -0.01333037, st_fmad(b, -4.23638, 0.04232096)));
    o->a[1] = (float)st_fmad(d1m, m, d1);
    double d2 = (st_fmad(m2, -0.08829894, st_fmad(m, -0.04028676, 0.1247944)) + f8) - f9;
    d2 = st_fmad(st_fmad(b, -1.39807, st_fmad(m, 0.01360253, -0.03027189)), t2, d2);
    d2 = st_fmad(st_fmad(st_fmad(t, 0.6280372, 4.013588), m, st_fmad(t, 1.356631, 5.775246)), b, d2);
    o->a[2] = (float)st_fmad(st_fmad(m2, -0.01381682, st_fmad(m, -0.01627771, 0.02931672)), t, d2);
    double d3 = (st_fmad(b, -1.780661, -0.03731519) - f4) + f5;
    d3 = st_fmad(st_fmad(b, 0.4618997, 0.009682518), t2, d3);
    d3 = st_fmad(st_fmad(b, -0.4215704, -0.008839382), t, d3);
    d3 = st_fmad(st_fmad(t, 0.00425828, 0.02780085), m2, d3);
    double d3m = st_fmad(st_fmad(b, -0.1935582, 0.005215454), t,
                         st_fmad(t2, -0.004624895, st_fmad(b, -1.263675, 0.01267784)));
    o->a[3] = (float)st_fmad(d3m, m, d3);
}

static inline void dizzy_s_init(struct dizzy_s_state *s, const float *k, const struct dizzy_s_model *md)
{
    *s = (struct dizzy_s_state){ 0 };
    s->gain = st_taper(st_pow2, k[DK_GAIN]);
    s->presence = dizzy_presence_knob(k[DK_PRESENCE]);
    s->volume = k[DK_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow2_5, k[DK_BASS]);
    s->middle = k[DK_MIDDLE] * 0.01f;
    s->treble = k[DK_TREBLE] * 0.01f;
    dizzy_s_gain_set(&s->gain_c, &s->gain_a0, s->gain, &md->gain);
    dizzy_s_tone_set(&s->tone, s->bass, s->middle, s->treble);
    dizzy_presence_set(&s->pres_c, s->presence);
}

/*
 * Per sample: the pre-filter, the gain section (from the glided Gain), the low pass, the stage (+-L, k), the tone
 * stack x Volume, the presence, the power amp's 7 sections, x out. Here stage by stage over the block, each glide
 * once per sample where it is used (Presence, not glided, before the first sample): the same samples.
 */
static inline void dizzy_s_process(struct dizzy_s_state *s, const struct wvfx_io *io, const struct dizzy_s_model *md)
{
    float gain = st_taper(st_pow2, io->knobs[DK_GAIN]);
    float presence = st_taper(st_pow2, io->knobs[DK_PRESENCE]);
    float volume = io->knobs[DK_VOLUME] * 0.01f;
    float bass = st_taper(st_pow2_5, io->knobs[DK_BASS]);
    float middle = io->knobs[DK_MIDDLE] * 0.01f;
    float treble = io->knobs[DK_TREBLE] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->bass = st_glide(s->bass, bass);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    if (s->presence != presence) {
        s->presence = presence == 0.0f ? 0.01f : presence;
        dizzy_presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&DIZZY_PRE, &s->pre, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            dizzy_s_gain_set(&s->gain_c, &s->gain_a0, s->gain, &md->gain);
        x[i] = fos_run_div(&s->gain_c, s->gain_a0, &s->gs, x[i]);
    }
    sos_block_div(&md->lowpass, &s->lowpass, x, n);
    stage_pow_block(&s->st, x, n, md->L, md->k);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle)
            dizzy_s_tone_set(&s->tone, s->bass, s->middle, s->treble);
        x[i] = tone3_run_b1b0(&s->tone, x[i]) * s->volume;
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 7; j++)
        sos_block(&DIZZY_S_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * md->out;
}