/*
 * A BassVT (AMP › A BassVT): the original's A BassVT bass amp (QME-200 V1.2.2, library entry 4.56, id 0x07000073) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample:
 *   input         x Gain (B25) x 70
 *   pre-filters   two fixed 2nd-order sections
 *   tone stack    4th order from Bass and Treble (B25)
 *   treble        a 3rd-order section from Treble (through B25v2)
 *   middle        a 3rd-order section from Middle (B25v3), its form and constants from Midrange (0-4)
 *   output        x Volume (B25) x 0.63
 * Gain and Volume glide 0.0001 per sample; the sections follow their knobs at each block, also while bypassed. No
 * clipping stage. Every section divided by its a0.
 */
#include "amp.h"

enum { K_GAIN, K_BASS, K_MIDDLE, K_MIDRANGE, K_TREBLE, K_VOLUME };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOB("Gain", K_GAIN), KNOB("Bass", K_BASS), KNOB("Middle", K_MIDDLE),
    { "Midrange", 0.0f, 4.0f, 1.0f, 1.0f, K_MIDRANGE, 0x10, 0, 0, 0, 0 },
    KNOB("Treble", K_TREBLE), KNOB("Volume", K_VOLUME),
};

static const struct sos PF[2] = {
    { 0.7322989106178284f, -1.1718343496322632f, 0.44055938720703125f, 1.0f, -1.8145240545272827f,
      0.8153988718986511f },
    { 1.1325987577438354f, -1.1718343496322632f, 0.2271219938993454f, 1.0f, -0.8842405080795288f,
      0.18111920356750488f },
};

/* a 4th-order section: y = (b0 x + b1 x1 + .. + b4 x4 - (a1 y1 + .. + a4 y4)) / a0, summed in that order */
struct tone4 {
    float b[5], a[5];
    float x[4], y[4];
};

static float tone4_run(struct tone4 *t, float x)
{
    float n = t->b[0] * x, d = t->a[1] * t->y[0];
    for (int i = 1; i < 5; i++)
        n = st_fma(t->b[i], t->x[i - 1], n);
    for (int i = 2; i < 5; i++)
        d = st_fma(t->a[i], t->y[i - 1], d);
    float y = (n - d) / t->a[0];
    for (int i = 3; i > 0; i--) {
        t->x[i] = t->x[i - 1];
        t->y[i] = t->y[i - 1];
    }
    t->x[0] = x;
    t->y[0] = y;
    return y;
}

struct state {
    float gain, volume;                     /* glided */
    float bass, treble, fake, middle, midrange;     /* the knobs the sections were set for */
    struct sos_state pf[2];
    struct tone4 eq;
    struct tone3 treble_c, mid;
};

/*
 * The 4th-order tone stack from Bass b and Treble t (b2 = b^2, t2 = t^2, tb = t b, b2t = b^2 t, t2b = t^2 b,
 * t2b2 = t^2 b^2): each coefficient a polynomial in those terms, fused from its constant in the original's order.
 */
static void eq_set(struct tone4 *o, float b, float t)
{
    float b2 = b * b, tb = t * b, t2 = t * t, b2t = b2 * t, t2b = t2 * b, t2b2 = t2 * b2;
    float B2 = b2 * 0.041467390954494476f, T2 = t2 * 0.000771262391936034f, T1 = t * 0.0019571080338209867f;
    float T3 = t * 0.008376708254218102f, T4 = t2 * 0.00848388671875f;
    float n = st_fma(b2, -0.02409362979233265f, st_fma(b, 0.024333389475941658f, 2.6846879336517304e-05f)) + tb;
    n = st_fma(t, 0.0011035019997507334f, n);
    n = st_fma(b2t, -0.9901496767997742f, n);
    n = st_fma(t2, -0.00039000349352136254f, n);
    n = st_fma(t2b, -0.3613247871398926f, n);
    o->b[0] = st_fma(t2b2, 0.35765540599823f, n);
    n = st_fma(b, -0.041410889476537704f, (T2 + B2) + 6.4872879192989785e-06f) - T1;
    n = st_fma(b2t, 3.7001349925994873f, n);
    n = st_fma(tb, -3.717679023742676f, n);
    n = st_fma(t2b2, -1.3955639600753784f, n);
    o->b[1] = st_fma(t2b, 1.4025059938430786f, n);
    n = st_fma(b2, 0.006292679812759161f, st_fma(b, -0.006711056921631098f, -4.710469875135459e-05f));
    n = st_fma(t, -0.0002460998948663473f, n);
    n = st_fma(tb, 5.173539161682129f, n);
    n = st_fma(t2, 8.744472324906383e-06f, n);
    n = st_fma(b2t, -5.175522804260254f, n);
    n = st_fma(t2b2, 2.040760040283203f, n);
    o->b[2] = st_fma(t2b, -2.040363073348999f, n);
    n = (st_fma(b, 0.041420139372348785f, -6.283743005042197e-06f) - B2) + T1;
    n = st_fma(b2t, 3.2112410068511963f, n);
    n = st_fma(tb, -3.1936960220336914f, n) - T2;
    n = st_fma(t2b2, -1.3254499435424805f, n);
    o->b[3] = st_fma(t2b, 1.3185080289840698f, n);
    n = st_fma(b2, 0.0178009495139122f, st_fma(b, -0.017613079398870468f, 2.0461369786062278e-05f));
    n = st_fma(t, -0.0008574026287533343f, n);
    n = st_fma(tb, 0.737835705280304f, n);
    n = st_fma(t2, 0.0003812589857261628f, n);
    n = st_fma(b2t, -0.7457028031349182f, n);
    n = st_fma(t2b2, 0.32259801030158997f, n);
    o->b[4] = st_fma(t2b, -0.31932610273361206f, n);
    float d = st_fma(b2, -0.8198744058609009f, st_fma(b, 0.8278303742408752f, 0.0009300924721173942f));
    d = st_fma(t, 0.005463325884193182f, d);
    d = st_fma(b2t, -4.78835916519165f, d);
    d = st_fma(tb, 4.834825038909912f, d);
    d = st_fma(t2, -0.004488775972276926f, d);
    d = st_fma(t2b, -3.9723870754241943f, d);
    o->a[0] = st_fma(t2b2, 3.9342100620269775f, d);
    d = st_fma(b2, 2.613023042678833f, st_fma(b, -2.623246908187866f, -0.0011021699756383896f)) - T3;
    d = st_fma(b2t, 16.975780487060547f, d) + T4;
    d = st_fma(tb, -17.052139282226562f, d);
    d = st_fma(t2b2, -15.35120964050293f, d);
    o->a[1] = st_fma(t2b, 15.427559852600098f, d);
    d = st_fma(b2, -3.0731539726257324f, st_fma(b, 3.068397045135498f, -0.0005811207229271531f));
    d = st_fma(t, -0.0023355858866125345f, d);
    d = st_fma(b2t, -22.364639282226562f, d);
    d = st_fma(tb, 22.348060607910156f, d);
    d = st_fma(t2, 0.0004936652258038521f, d);
    o->a[2] = st_fma(t2b2 - t2b, 22.448360443115234f, d);
    d = st_fma(b2, 1.5773379802703857f, st_fma(b, -1.567113995552063f, 0.0011136609828099608f)) + T3;
    d = st_fma(b2t, 12.95536994934082f, d);
    d = st_fma(tb, -12.879019737243652f, d) - T4;
    d = st_fma(t2b2, -14.579939842224121f, d);
    o->a[3] = st_fma(t2b, 14.503589630126953f, d);
    d = st_fma(b2, -0.29733291268348694f, st_fma(b, 0.29413411021232605f, -0.0003374807129148394f));
    d = st_fma(t, -0.0031277399975806475f, d);
    d = st_fma(b2t, -2.7781529426574707f, d);
    d = st_fma(tb, 2.7482640743255615f, d);
    d = st_fma(t2, 0.003995111212134361f, d);
    d = st_fma(t2b, -3.5104010105133057f, d);
    o->a[4] = st_fma(t2b2, 3.5485780239105225f, d);
}

/* the treble section's form (and the middle's at Midrange 4) from x: P = 0.99867 x^2, Q = 1.7976 x, R = 0.99734 x,
   (P - Q + k0, Q - P + k1, Q - P + k2, P - Q + k3) / ((x^2 + x) + e0, (e1 - R) - 0.99734 x^2,
   e2 - (x^2 + x), (R + e3) + 0.99734 x^2) */
static void fake_set(struct tone3 *o, float x, const float *k, const float *e)
{
    float xx = x * x, P = xx * 0.9986678957939148f, Q = x * 1.7976020574569702f, R = x * 0.9973357915878296f;
    float pq = P - Q, qp = Q - P, s = xx + x;
    o->b[0] = pq + k[0];
    o->b[1] = qp + k[1];
    o->b[2] = qp + k[2];
    o->b[3] = pq + k[3];
    o->a[0] = s + e[0];
    o->a[1] = st_fma(xx, -0.9973357915878296f, e[1] - R);
    o->a[2] = e[2] - s;
    o->a[3] = st_fma(xx, 0.9973357915878296f, R + e[3]);
}

static const float TREBLE_K[4] = { -2.077986001968384f, 5.148375034332275f, -4.9686150550842285f, 1.898224949836731f };
static const float TREBLE_E[4] = { -4.080757141113281f, 7.144371032714844f, -2.9670519828796387f,
                                   -0.09897822886705399f };

/* the middle section's constants at Midrange 0, 1, 2: c1 Q's factor, k the numerator's, c2, c3 and e the
   denominator's: ((e0 - c2 m) + m^2, (c3 m + e1) - 0.99734 m^2, (e2 - m^2) + c2 m, (e3 - c3 m) + 0.99734 m^2) */
struct mid_c { float c1, k[4], c2, c3, e[4]; };
static const struct mid_c MID[3] = {
    { 1.5679090023040771f, { -7.535522937774658f, 22.506399154663086f, -22.41253089904785f, 7.441648960113525f },
      0.5299999713897705f, 0.5285879969596863f,
      { -8.015575408935547f, 22.985069274902344f, -21.9324893951416f, 6.962975025177002f } },
    { 1.5878820419311523f, { -7.5499138832092285f, 22.50798988342285f, -22.39813995361328f, 7.440060138702393f },
      0.49000000953674316f, 0.48869460821151733f,
      { -8.069984436035156f, 23.026519775390625f, -21.878110885620117f, 6.921494960784912f } },
    { 1.5878820419311523f, { -7.568272113800049f, 22.4896297454834f, -22.379779815673828f, 7.458418846130371f },
      0.49000000953674316f, 0.48869460821151733f,
      { -8.088367462158203f, 23.008079528808594f, -21.859830856323242f, 6.939828872680664f } },
};
static const float MID4_K[4] = { -1.927014946937561f, 5.299345970153809f, -5.119585037231445f, 1.7472549676895142f };
static const float MID4_E[4] = { -3.9295859336853027f, 7.295945167541504f, -3.117418050765991f,
                                 -0.24974709749221802f };

/* the middle section from m at Midrange r; any other r leaves it as it is */
static void mid_set(struct tone3 *o, float m, float r)
{
    float mm = m * m;
    if (r == 0.0f || r == 1.0f || r == 2.0f) {
        const struct mid_c *c = &MID[(int)r];
        float P = mm * 0.9986678957939148f, Q = m * c->c1, pq = P - Q, qp = Q - P;
        float s2 = m * c->c2, s3 = m * c->c3;
        o->b[0] = pq + c->k[0];
        o->b[1] = qp + c->k[1];
        o->b[2] = qp + c->k[2];
        o->b[3] = pq + c->k[3];
        o->a[0] = (c->e[0] - s2) + mm;
        o->a[1] = st_fma(mm, -0.9973357915878296f, s3 + c->e[1]);
        o->a[2] = (c->e[2] - mm) + s2;
        o->a[3] = st_fma(mm, 0.9973357915878296f, c->e[3] - s3);
    } else if (r == 3.0f) {
        float P = mm * 0.9986678957939148f, Q = m * 1.7976020574569702f, pq = P - Q, qp = Q - P;
        float R = mm * 0.9973357915878296f;
        o->b[0] = pq + -5.445598125457764f;
        o->b[1] = qp + 15.8739595413208f;
        o->b[2] = qp + -15.694199562072754f;
        o->b[3] = pq + 5.265838146209717f;
        o->a[0] = mm + -6.452861785888672f;
        o->a[1] = 16.87795066833496f - R;
        o->a[2] = -14.6873197555542f - mm;
        o->a[3] = R + 4.261477947235107f;
    } else if (r == 4.0f) {
        fake_set(o, m, MID4_K, MID4_E);
    }
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_b25, k[K_GAIN]);
    s->bass = st_taper(st_b25, k[K_BASS]);
    s->midrange = k[K_MIDRANGE];
    s->middle = st_taper(st_b25v3, k[K_MIDDLE]);
    s->treble = st_taper(st_b25, k[K_TREBLE]);
    s->fake = st_taper(st_b25v2, k[K_TREBLE]);
    s->volume = st_taper(st_b25, k[K_VOLUME]);
    eq_set(&s->eq, s->bass, s->treble);
    fake_set(&s->treble_c, s->fake, TREBLE_K, TREBLE_E);
    mid_set(&s->mid, s->middle, s->midrange);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float bass = st_taper(st_b25, k[K_BASS]), treble = st_taper(st_b25, k[K_TREBLE]);
    float fake = st_taper(st_b25v2, k[K_TREBLE]), middle = st_taper(st_b25v3, k[K_MIDDLE]);
    if (s->bass != bass || s->treble != treble) {
        s->bass = bass;
        s->treble = treble;
        eq_set(&s->eq, s->bass, s->treble);
    }
    if (s->fake != fake) {
        fake_set(&s->treble_c, fake, TREBLE_K, TREBLE_E);
        s->fake = fake;
    }
    if (s->middle != middle || s->midrange != k[K_MIDRANGE]) {
        s->middle = middle;
        s->midrange = k[K_MIDRANGE];
        mid_set(&s->mid, s->middle, s->midrange);
    }
    float gain = st_taper(st_b25, k[K_GAIN]), volume = st_taper(st_b25, k[K_VOLUME]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        if (!io->enabled)
            continue;
        float x = s->gain * 70.0f * p[0];
        x = sos_run(&PF[0], &s->pf[0], x);
        x = sos_run(&PF[1], &s->pf[1], x);
        x = tone4_run(&s->eq, x);
        x = tone3_run_std(&s->treble_c, x);
        x = tone3_run_std(&s->mid, x);
        float y = s->volume * 0.6299999952316284f * x;
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc a_bassvt_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x73),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "A BassVT",
    .category = "A BassVT",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
