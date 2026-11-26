/*
 * The pieces the original's Calif amps share (docs/stock_port.md): their knobs, the bright filter, the presence
 * section, the asymmetric clippers, the tone stacks, the Star's envelope-driven stages. The generic ones are in
 * amp.h. Each model's file strings them in its order with its tables.
 */
#pragma once
#include "amp.h"

enum { CK_GAIN, CK_PRESENCE, CK_VOLUME, CK_BASS, CK_MIDDLE, CK_TREBLE };

#define CALIF_PARAM(name, def, knob) { name, 0.0f, 100.0f, 1.0f, def, knob, 0x15, 0, 0, 0, 0 }

/* ---------------------------------------------------------------- the IIC+ and IV LD (MesseIICPlus, MesseIVLead) */

/* what the knobs ask for: Gain pow2, Bass pow4, Treble pow3 x 0.99 + 0.01, the rest linear; Presence 0 is 0.01 for
   the filter (the original compares the knob's own value to tell it moved) */
struct iic_knobs { float gain, presence, presence_raw, volume, bass, middle, treble; };

static inline struct iic_knobs iic_knobs(const float *k)
{
    struct iic_knobs t;
    t.gain = st_taper(st_pow2, k[CK_GAIN]);
    t.presence_raw = k[CK_PRESENCE] * 0.01f;
    t.presence = t.presence_raw == 0.0f ? 0.01f : t.presence_raw;
    t.volume = k[CK_VOLUME] * 0.01f;
    t.bass = st_taper(st_pow4, k[CK_BASS]);
    t.middle = k[CK_MIDDLE] * 0.01f;
    t.treble = st_fma(st_taper(st_pow3, k[CK_TREBLE]), 0.99f, 0.01f);
    return t;
}

/* the IIC+'s fixed sections: the low pass after the pre-filter, the power amp */
static const struct sos IIC_LOWPASS = { 0.020162404f, 0.040324807f, 0.020162404f, 1.1988056f, -1.9193504f,
                                        0.80119437f };
static const struct sos IIC_POWER[6] = {
    { 0.3634097f, -0.7215851f, 0.358267f, 1.0f, -1.9966717f, 0.9967653f },
    { 0.6223038f, -1.241684f, 0.6194318f, 1.0f, -1.9935158f, 0.9937149f },
    { 1.241684f, -1.2290859f, 0.9009717f, 1.0f, -0.9058071f, 0.6242073f },
    { 1.1412952f, -1.241684f, 0.6344272f, 1.0f, -1.0405114f, 0.6289817f },
    { 1.241684f, 0.6024673f, 0.873567f, 1.0f, 0.5226998f, 0.6838359f },
    { 1.2003514f, 1.241684f, 0.350321f, 1.0f, 1.1235178f, 0.479752f },
};

/* the IV LD's knobs: Gain pow2, Bass and Middle pow3, Treble pow3 x 0.95 + 0.05, Volume and Presence linear */
static inline struct iic_knobs ivld_knobs(const float *k)
{
    struct iic_knobs t;
    t.gain = st_taper(st_pow2, k[CK_GAIN]);
    t.presence = t.presence_raw = k[CK_PRESENCE] * 0.01f;
    t.volume = k[CK_VOLUME] * 0.01f;
    t.bass = st_taper(st_pow3, k[CK_BASS]);
    t.middle = st_taper(st_pow3, k[CK_MIDDLE]);
    t.treble = st_fma(st_taper(st_pow3, k[CK_TREBLE]), 0.95f, 0.05f);
    return t;
}

/* the IV LD 1 / 2's low pass and power amp */
static const struct sos IVLD_LOWPASS = { 0.01137501f, 0.02275002f, 0.01137501f, 1.1499934f, -1.9545f, 0.8500066f };
static const struct sos IVLD_POWER[9] = {
    { 0.4504254f, -0.8974701f, 0.4470609f, 1.0f, -1.992521f, 0.9926606f },
    { 0.4503775f, -0.8974701f, 0.4472878f, 1.0f, -1.9931592f, 0.9935371f },
    { 0.4879853f, -0.8974701f, 0.4143509f, 1.0f, -1.7572234f, 0.7775041f },
    { 0.5812113f, -0.8974701f, 0.3344245f, 1.0f, -1.5863042f, 0.6034928f },
    { 0.7578991f, -0.8974701f, 0.3342693f, 1.0f, -1.290088f, 0.5200852f },
    { 0.8974701f, -0.390664f, 0.1504897f, 1.0f, -0.1873714f, 0.0656312f },
    { 0.8974701f, 0.1219955f, 0.575071f, 1.0f, 0.1396811f, 0.6796794f },
    { 0.8974701f, -0.0721657f, 0.4695962f, 1.0f, 0.031583f, 0.0061364f },
    { 0.8974701f, 0.6091134f, 0.054444f, 1.0f, -0.0552451f, 0.4304163f },
};

/* the IV LD 3's power amp */
static const struct sos IVLD3_POWER[9] = {
    { 0.4356695f, -0.8701157f, 0.4344711f, 1.0f, -1.9957572f, 0.996028f },
    { 0.4512024f, -0.8967028f, 0.4456255f, 1.0f, -1.9963202f, 0.9963868f },
    { 0.4581001f, -0.8967028f, 0.4416947f, 1.0f, -1.9469621f, 0.9543165f },
    { 0.6356653f, -0.8967028f, 0.2912903f, 1.0f, -1.5268342f, 0.5835509f },
    { 0.7346148f, -0.8967028f, 0.326815f, 1.0f, -0.7505897f, 0.2116547f },
    { 0.8967028f, -0.1286785f, 0.0486488f, 1.0f, -0.1821481f, 0.0924875f },
    { 0.8967028f, -0.2833702f, 0.395635f, 1.0f, -0.0018881f, 0.0295751f },
    { 0.8967028f, 0.7399345f, 0.2983802f, 1.0f, 0.0483651f, 0.0671221f },
    { 0.8967028f, 0.7870977f, 0.1310537f, 1.0f, 0.6018498f, 0.1096834f },
};

/* the IV LD 2 / 3's knobs: their tapers in double (Bass pow3 x 0.9, Middle pow3 x 0.9 + 0.1, Treble pow3 x 0.95 +
   0.05) */
static inline struct iic_knobs ivld2_knobs(const float *k)
{
    struct iic_knobs t = ivld_knobs(k);
    t.treble = (float)st_fmad((double)st_taper(st_pow3, k[CK_TREBLE]), 0.95, 0.05);
    t.middle = (float)st_fmad((double)st_taper(st_pow3, k[CK_MIDDLE]), 0.9, 0.1);
    t.bass = (float)((double)st_taper(st_pow3, k[CK_BASS]) * 0.9);
    return t;
}

/* the IV LD's presence: a bilinear high shelf of k = 10^(p/2) (its argument computed by the caller: Init and Process
   scale the knob differently), times a fixed section (a0 = 1) */
static inline void ivld_presence_set(struct sos *c, float arg)
{
    const float K = 0.6408323f, P = 1.6408323f, N = -0.3591677f;
    float k = st_exp2f(arg);
    float a = 2.0f / (k + 1.0f), b = 2.0f / (1.0f / k + 1.0f);
    float ap = a + K, am = K - a, bm = K - b, bp = b + K;
    float n = ap * P;
    *c = (struct sos){ (bp * N) / n, st_fma(bp, P, bm * N) / n, (bm * P) / n,
                       1.0f, st_fma(am, P, ap * N) / n, (am * N) / n };
}

/* the IV LD's clipper: +-1.5, k 0.00634765625 below zero, 1 / 128 above */
static inline float ivld_clip(float x);

/* the bright filter, 1st order, its zero and pole moved by the Gain g:
   y = (b0 x + b1 x1 - a1 y1) / a0, b0 = g^2 - 1.094482 g, b1 = 0.9055178 g - g^2, a0 = g^2 - g - 0.0944822,
   a1 = g - 0.0944822 - g^2 */
static inline void iic_bright_set(struct fos *c, float *a0, float g)
{
    float g2 = g * g;
    *c = (struct fos){ st_fma(g, -1.094482f, g2), st_fma(g, 0.9055178f, -g2), (g + -0.09448224f) - g2 };
    *a0 = (-0.09448224f - g) + g2;
}

/* the same in double (the IIC+ 3), g^2 taken in single precision */
static inline void iic_bright_set_d(struct fos *c, float *a0, float gf)
{
    double g = gf, g2 = (float)(gf * gf);
    *c = (struct fos){ (float)st_fmad(g, -1.094482, g2), (float)st_fmad(g, 0.9055178, -g2),
                       (float)((g + -0.09448224) - g2) };
    *a0 = (float)((-0.09448224 - g) + g2);
}

/* a presence section: a high shelf of k1 = 2^arg1 and a 1st-order section of k2 = 2^arg2, bilinear, multiplied out
   (a0 = 1); K and C fold the sections' frequencies */
static inline void calif_presence_set(struct sos *c, float arg1, float arg2, float K, float C)
{
    float k1 = st_exp2f(arg1), k2 = st_exp2f(arg2);
    float a = 2.0f / (1.0f / k1 + 1.0f), b = 2.0f / (k1 + 1.0f);
    float cc = C / (k2 + 1.0f), d = C / (1.0f / k2 + 1.0f);
    float ap = a + K, am = K - a, bp = b + K, bm = K - b;
    float c1 = cc + 1.0f, cm1 = cc + -1.0f, d1 = d + 1.0f, dm1 = d + -1.0f;
    float n = bp * c1;
    *c = (struct sos){ (ap * dm1) / n, st_fma(ap, d1, am * dm1) / n, (am * d1) / n,
                       1.0f, st_fma(bp, cm1, bm * c1) / n, (bm * cm1) / n };
}

/* the IIC+'s, from the knob p (0.01..1): 10^(p/2) and 10^(p/20) */
static inline void iic_presence_set(struct sos *c, float p)
{
    calif_presence_set(c, p * 1.660964f, p * 0.1660964f, 0.10726547f, 0.21453094f);
}

/* ---------------------------------------------------------------- the Dual V and M (RectorDual) */

/* the knobs: Gain pow2_5, Bass pow3, the rest linear */
static inline struct iic_knobs dual_knobs(const float *k)
{
    struct iic_knobs t;
    t.gain = st_taper(st_pow2_5, k[CK_GAIN]);
    t.presence = t.presence_raw = k[CK_PRESENCE] * 0.01f;
    t.volume = k[CK_VOLUME] * 0.01f;
    t.bass = st_taper(st_pow3, k[CK_BASS]);
    t.middle = k[CK_MIDDLE] * 0.01f;
    t.treble = k[CK_TREBLE] * 0.01f;
    return t;
}

/* the sections ahead of stage 1: a pre-filter, then after the Gain a 3rd-order section that follows the Gain too
   (divided by its a0), a low pass */
static const struct sos DUAL_PRE = { 3776.1526f, -7508.1973f, 3732.2373f, 1.0f, -1.9402872f, 0.9413199f };

/* the 3rd-order section's coefficients: polynomials in g = max(Gain, 0.3) and g^2 */
static inline void dual_pf2_set(struct tone3 *s, float gain)
{
    float g = gain > 0.3f ? gain : 0.3f, g2 = g * g;    /* VMAXNM in the original */
    float q0 = g2 * 0.5380433f, q1 = g2 * 0.526879f;
    s->b[0] = st_fma(g, -0.5441436f, q0);
    s->b[1] = st_fma(g, 0.5206522f, -q1);
    s->b[2] = st_fma(g, 0.5438904f, -q0);
    s->b[3] = st_fma(g, -0.5209054f, q1);
    s->a[0] = st_fma(g, -0.9947544f, -0.01652161f) + g2;
    s->a[1] = st_fma(g2, -1.871518f, st_fma(g, 1.871956f, -0.001895662f));
    s->a[2] = st_fma(g2, 0.7658116f, st_fma(g, -0.7710572f, 0.01613695f));
    s->a[3] = st_fma(g2, 0.1057066f, st_fma(g, -0.1061443f, 0.001511003f));
}

static const struct sos DUAL_LOWPASS = { 0.031383842f, 0.062767684f, 0.031383842f, 1.2466093f, -1.8744646f,
                                         0.75339067f };

/* the presence: 10^(0.4 p) and 10^(-p/20); Init scales the knob itself (arg = knob x 0.01 x ...) */
static inline void dual_presence_set(struct sos *c, float arg1, float arg2)
{
    calif_presence_set(c, arg1, arg2, 0.07135868f, 0.014247827f);
}

/* the IIC+'s clipper: +-1.3, k 1 / 256 below zero, 1 / 204.8 above */
static inline float iic_clip(float x)
{
    return clip_pow_asym(x, 1.3f, 0.00390625f, 0.0048828125f);
}

static inline float ivld_clip(float x)
{
    return clip_pow_asym(x, 1.5f, 0.00634765625f, 0.0078125f);
}

/* the IIC+ tone stack (b, m, t after their tapers): its polynomials in single precision, summed in the original's
   order; one sum is a fused negate (-q - k b) */
static inline void tone_iic(struct tone3 *o, float b, float m, float t)
{
    float q3 = st_fma(b, 0.004269454f, st_fma(m, 0.0002243567f, t * 0.001419534f));
    float q1 = st_fma(t, 0.001417346f, m * 0.0001728065f);
    float bt = b * t, bm = b * m, t2 = t * t, mt = m * t, bt3 = bt * 3.0f;
    float t2m = t2 * m * 3.546099e-05f;
    float q6 = st_fma(t2, 0.0003543364f, t2m), q5 = st_fma(t2, 0.0003548834f, t2m);
    float q10 = mt * 2.769849e-05f, q4 = mt * 1.48547e-05f;
    float q11 = bm * 0.025f * t2, q9 = bm * 0.075f * t2;

    o->b[0] = st_fma(bm, 0.1581859f, q3 + bt);
    o->b[1] = st_fma(-st_fma(m, 0.4381859f, 0.00339205f), b, -(bt3 + q1));
    o->b[2] = bt3 - st_fma(bm, -0.4018141f, q3);
    o->b[3] = st_fma(st_fma(m, -0.1218141f, 0.00339205f), b, q1 - bt);

    float e0 = st_fma(t2, -0.2506031f, st_fma(t, 0.2957091f, 1.186095f));
    e0 = st_fma(st_fma(t, 0.01952381f, 0.1700134f), m, e0);
    float d0 = (st_fma(m, 0.0002411271f, st_fma(t, 0.0004192614f, 0.001677046f)) + q10) - (q5 + q11);
    o->a[0] = st_fma(e0, b, d0);

    float e1 = st_fma(t, -0.7954908f, st_fma(t2, 0.7506031f, -3.185088f));
    e1 = st_fma(st_fma(t, -0.04952381f, -0.4700093f), m, e1);
    float d1 = (st_fma(m, -0.0001844008f, st_fma(t, -0.0002898592f, q6 + -0.001159437f)) - q4) + q9;
    o->a[1] = st_fma(e1, b, d1);

    float e2 = st_fma(t2, -0.7493969f, st_fma(t, 0.7042909f, 2.813906f));
    e2 = st_fma(st_fma(t, 0.04047619f, 0.4299866f), m, e2);
    float d2 = st_fma(m, -0.0002411192f, st_fma(t, -0.0004190629f, q5 + -0.001676252f)) - (q10 + q9);
    o->a[2] = st_fma(e2, b, d2);

    float e3 = st_fma(t, -0.2045092f, st_fma(t2, 0.2493969f, -0.814912f));
    e3 = st_fma(st_fma(t, -0.01047619f, -0.1299907f), m, e3);
    float d3 = ((st_fma(m, 0.0001844088f, st_fma(t, 0.0002900577f, 0.001160231f)) + q4) - q6) + q11;
    o->a[3] = st_fma(e3, b, d3);
}

/* the IIC+ 3's tone stack: the same shape with its own constants, in double from the single-precision knobs (t^2
   and the b t of b0 in single precision), b3's b terms gathered as (0.003720256 - t - 0.1354195 m) b */
static inline void tone_iic3(struct tone3 *o, float bf, float mf, float tf)
{
    double b = bf, m = mf, t = tf, t2 = (float)(tf * tf), btf = (float)(bf * tf);
    double q3 = st_fmad(b, 0.003941248, st_fmad(m, 0.0002050582, t * 0.001419534));
    double q1 = st_fmad(t, 0.001417346, m * 0.000192105);
    double bt = b * t, bm = b * m, mt = m * t, bt3 = bt * 3.0;
    double t2m = t2 * m * 3.546099e-05;
    double q6 = st_fmad(t2, 0.0003543364, t2m), q5 = st_fmad(t2, 0.0003548834, t2m);
    double q10 = mt * 2.287386e-05, q4 = mt * 1.967933e-05;
    double q11 = bm * 0.025 * t2, q9 = bm * 0.075 * t2;

    o->b[0] = (float)st_fmad(bm, 0.1445805, q3 + btf);
    o->b[1] = (float)st_fmad(-st_fmad(m, 0.4245805, 0.003720256), b, -(bt3 + q1));
    o->b[2] = (float)(bt3 - st_fmad(bm, -0.4154195, q3));
    o->b[3] = (float)st_fmad(st_fmad(m, -0.1354195, 0.003720256 - t), b, q1);

    double e0 = st_fmad(t2, -0.2506031, st_fmad(t, 0.2616134, 1.049664));
    e0 = st_fmad(st_fmad(t, 0.01612245, 0.1550459), m, e0);
    double d0 = (st_fmad(m, 0.0002198973, st_fmad(t, 0.0003709779, 0.001483912)) + q10) - (q5 + q11);
    o->a[0] = (float)st_fmad(e0, b, d0);

    double e1 = st_fmad(t, -0.7615593, st_fmad(t2, 0.7506031, -3.04941));
    e1 = st_fmad(st_fmad(t, -0.04612245, -0.4550448), m, e1);
    double d1 = (st_fmad(m, -0.0002056337, st_fmad(t, -0.0003382171, q6 + -0.001352869)) - q4) + q9;
    o->a[1] = (float)st_fmad(e1, b, d1);

    double e2 = st_fmad(t2, -0.7493969, st_fmad(t, 0.7383866, 2.950336));
    e2 = st_fmad(st_fmad(t, 0.04387755, 0.4449541), m, e2);
    double d2 = st_fmad(m, -0.0002198953, st_fmad(t, -0.0003709283, q5 + -0.001483713)) - (q10 + q9);
    o->a[2] = (float)st_fmad(e2, b, d2);

    double e3 = st_fmad(t, -0.2384407, st_fmad(t2, 0.2493969, -0.95059));
    e3 = st_fmad(st_fmad(t, -0.01387755, -0.1449552), m, e3);
    double d3 = ((st_fmad(m, 0.0002056356, st_fmad(t, 0.0003382667, 0.001353067)) + q4) - q6) + q11;
    o->a[3] = (float)st_fmad(e3, b, d3);
}

/* ------------------------------------------------------- the Star CL and OD (SuperstarClean, SuperstarDrive) */

/* what the knobs ask for, from Gain on (knob `first`; the Star OD's Input is before it): Gain through `gain_tab` x
   `gain_scale` (CL pow3 x 0.7, OD pow1_5), Presence pow3, Volume pow2_5, Bass pow4_5, Treble pow3, Middle linear;
   Presence and Volume held in 0.01..0.99 (0.01 below points 57 and 42, 0.99 from point 253). The Star CL's Init
   computes from the Gain's and Middle's undivided values (gain_raw, middle_raw) */
struct star_knobs { float gain, gain_raw, presence, volume, bass, middle, middle_raw, treble; };

static inline float star_held(const float *tab, float knob, int lo)
{
    int i = (int)(knob * 2.55f);
    float v = st_taper(tab, knob);
    if (i >= 253)
        v = 0.99f;
    if (i < lo)
        v = 0.01f;
    return v;
}

static inline struct star_knobs star_knobs(const float *k, int first, const float *gain_tab, float gain_scale)
{
    const float *q = k + first;
    struct star_knobs t;
    t.gain_raw = st_taper(gain_tab, q[0]);
    t.gain = t.gain_raw * gain_scale;
    t.presence = star_held(st_pow3, q[1], 57);
    t.volume = star_held(st_pow2_5, q[2], 42);
    t.bass = st_taper(st_pow4_5, q[3]);
    t.middle_raw = q[4];
    t.middle = q[4] * 0.01f;
    t.treble = st_taper(st_pow3, q[5]);
    return t;
}

/* the tone stack from Bass b, Middle m and Treble t; m1, m2 are Middle's own terms (m x 2.243567e-4, m x 1.728065e-4;
   Init: the knob x 2.243567e-6, x 1.728065e-6). Process adds three Bass terms as products, Init fuses them (fused) */
static inline float star_bterm(float acc, float b, float k, int fused)
{
    return fused ? st_fma(b, k, acc) : acc + b * k;
}

static inline void star_tone_set(struct tone3 *o, float b, float m, float t, float m1, float m2, int fused)
{
    float tb = t * b, tb3 = tb * 3.0f, mb = m * b;
    float mb1 = mb * -0.4381859004497528f, mb2 = mb * 0.4018141031265259f;
    float t1 = t * 0.0014195339754223824f, t2 = t * 0.0014173459494486451f, b1 = b * 0.004269454162567854f;
    float u = st_fma(mb, 0.1581858992576599f, m1);
    float v = st_fma(mb, -0.12181410193443298f, m2);
    o->b[0] = ((t1 + b1) + u) + tb;
    o->b[1] = star_bterm(mb1, b, -0.0033920500427484512f, fused) - ((m2 + t2) + tb3);
    o->b[2] = (mb2 - ((b1 + m1) + t1)) + tb3;
    o->b[3] = (star_bterm(v, b, 0.0033920500427484512f, fused) + t2) - tb;
    o->a[0] = st_fma(b, 1.185675024986267f, 0.0016770459478721023f) + u;
    o->a[1] = (st_fma(b, -3.184798002243042f, -0.0011594370007514954f) - m2) + mb1;
    o->a[2] = (st_fma(b, 2.8143250942230225f, -0.0016762519953772426f) - m1) + mb2;
    o->a[3] = star_bterm(v + 0.001160230953246355f, b, -0.8152021169662476f, fused);
}

/* the gain section, 1st order from the glided Gain g: b0 = g^2 + k0 p, b1 = k1 p - g^2 (p = g; Init: p the knob's
   pow3 before x 0.7 and k0, k1 x 0.7) */
static inline void star_gain_set(struct fos *c, float *a0, float g, float p, float k0, float k1)
{
    float g2 = g * g;
    c->b0 = st_fma(p, k0, g2);
    c->b1 = st_fma(p, k1, -g2);
    *a0 = (-0.1764822006225586f - g) + g2;
    c->a1 = (g + -0.012482239864766598f) - g2;
}

/* the presence section from Volume v and Presence p */
static inline void star_presence_set(struct sos *c, float v, float p)
{
    float vp = v * p, v2 = v * v, v2k = v2 * 0.8928570747375488f;
    float w = st_fma(p * 9.449999809265137f, v2, st_fma(vp, -9.449999809265137f, -0.010123100131750107f));
    float z = st_fma(v, 0.8928570747375488f, p);
    c->b0 = v * -0.010123100131750107f - vp;
    c->b1 = v * -0.020246189087629318f;
    c->b2 = vp + v * -0.010123100131750107f;
    c->a0 = (w - z) + v2k;
    c->a1 = st_fma(p * 18.899999618530273f, v - v2, -0.020246189087629318f);
    c->a2 = (w + z) - v2k;
}

/* the Star's envelope: |x| through a fixed 2nd-order low pass */
static inline float star_env_run(struct env_state *e, float x)
{
    return env_run(e, x, 9.920032653099042e-07f, -1.9980080127716064f, 0.9980090260505676f);
}

/*
 * The Star CL's stage over a block in place, the sample's Gain in g[i]: the envelope of x and Gain g set the clip's
 * k = 5 + 5 g + 15 env, C = 0.2 - 0.1 g and M = max(1 / (1 + 3 k), 0.4); the 3 older points are scaled M C before the
 * (1 - e) and the current one after (e = e^(-k |q|), C's sign q's: exp_negabs, sign_of, without branches); then the
 * FIR four samples at a time, AMP_FIR_ORDER2.
 */
static inline void star_cl_stage_block(struct stage *s, struct env_state *e, float *y, const float *g, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], env = star_env_run(e, x);
        float k = st_fma(env, 15.0f, st_fma(g[i], 5.0f, 5.0f));
        float m = 1.0f / st_fma(k, 3.0f, 1.0f);
        m = m > 0.4f ? m : 0.4f;
        float c = st_fma(g[i], -0.1f, 0.2f), d = x - prev;
        float q[4] = { x, st_fma(d, 0.6666667f, prev), st_fma(d, 0.33333334f, prev), prev }, v[4];
        prev = x;
#pragma clang loop unroll(full)
        for (int j = 0; j < 4; j++) {
            float ci = sign_of(c, q[j]), ex = exp_negabs(q[j], k);
            if (j == 0) {
                v[j] = st_fma(-ex, ci, ci) * m;
            } else {
                float mc = m * ci;
                v[j] = st_fma(-ex, mc, mc);
            }
        }
        float *o = buf + 40 + 4 * i;
        o[0] = v[3];
        o[1] = v[2];
        o[2] = v[1];
        o[3] = v[0];
    }
    s->prev = prev;
    stage_block_fir_ord(s, buf, y, n, AMP_FIR_ORDER2);
}
