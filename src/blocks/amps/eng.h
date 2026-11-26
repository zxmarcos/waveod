/*
 * The original's Eng 120 amps (docs/stock_port.md): the 120 and 120+ share the path; they differ in the tone stack's
 * constants, the power amp's sections and the output level, so the whole path is here and each model's file gives
 * those.
 *
 * Per sample:
 *   pre-filters   a fixed 2nd-order section, a 3rd-order one from Gain (at least 0.3; divided by its a0), a low pass
 *   gain          x Gain (pow2) x 0.5
 *   stage         the power clipper +-1 (k 1 / 341.3 above zero, 1 / 365.7 below) on 4 points per sample, the FIR
 *   tone stack    3rd order from Bass (pow2), Middle and Treble (linear)
 *   presence      a 2nd-order section from Presence (linear, 0 counts as 0.01), on 4 x Volume x the tone stack
 *   power amp     four fixed 2nd-order sections, x the model's level (the 120: 1, the 120+: 0.5)
 * Gain, Volume, Middle and Treble glide 0.0001 per sample, Bass and Presence take effect at once; the gain and tone
 * coefficients stop one glide step short of the knobs.
 */
#pragma once
#include "amp.h"

enum { EN_GAIN, EN_PRESENCE, EN_VOLUME, EN_BASS, EN_MIDDLE, EN_TREBLE };

#define ENG_PARAMS { \
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, EN_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, EN_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, EN_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, EN_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, EN_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, EN_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

static const struct sos ENG_PF1 = { 3776.152587890625f, -7508.197265625f, 3732.2373046875f, 1.0f, -1.9402872323989868f,
                                    0.9413198828697205f };
static const struct sos ENG_LOWPASS = { 0.03138384222984314f, 0.06276768445968628f, 0.03138384222984314f,
                                        1.2466093301773071f, -1.8744646310806274f, 0.7533906698226929f };

/*
 * The tone stack's constants (b Bass, m Middle, t Treble; tb, tm, mb products, t2 = t^2, m2 = m^2):
 *   n0 = ((b nb0 + k0) + t nt0 + tb + m2 nm0) - 0.02 tm + (b x0b + x0k) m
 *   n1 = (-k1 - (b nb1 + 3 tb)) + t nt1 + 0.06 tm + m2 nm1 + (b x1b + x1k) m
 *   n2 = (-k0 - b nb0) + 3 tb + t nt2 + m2 nm2 - 0.06 tm + (b x2b + x2k) m
 *   n3 = ((b nb1 + k1) + t nt3 - tb) + 0.02 tm + m2 nm3 + (b x3b + x3k) m
 * and the denominator in the Flyman's shape (flyman.h) without Volume: each di from its constant, the t^2 m^2 and
 * m b t^2 terms (A = 0.0213 t2 m2, B = 1.064 mb t2, C = 0.0638 t2 m2, E = 3.191 mb t2), then polynomials in b, m
 * and t scaled by t2, b, t, m2 and m. Each sum fused from its constant, in the order the original sums it.
 */
struct eng_ts {
    float k0, nb0, nt0, nm0, x0b, x0k;
    float k1, nb1, nt1, nm1, x1b, x1k;
    float nt2, nm2, x2b, x2k;
    float nt3, nm3, x3b, x3k;
    float d0m, d0k, d0m2, e0m, e0k, e0b, y1t, y1k, y0t, y0k, l0m, l0k, l0m2;            /* a0 */
    float k1b, k1k, g1b, g1k, j1b, j1k, m1t, m1k, i1b, i1k, i1t2, h1b, h1k;              /* a1 */
    float d2m, d2k, d2m2, f2m, f2k, f2b, z1t, z1k, z0t, z0k, l2m, l2k, l2m2;            /* a2 */
    float k3b, k3k, g3b, g3k, j3b, j3k, m3t, m3k, i3b, i3k, i3t2, h3b, h3k;              /* a3 */
};

static inline void eng_tone_set(struct tone3 *o, const struct eng_ts *c, float b, float m, float t)
{
    float m2 = m * m, t2 = t * t, t2m2 = t2 * m2, mb = m * b, tb = t * b, tb3 = tb * 3.0f, tm = t * m;
    float bk0 = b * c->nb0, bk1 = b * c->nb1, tm02 = tm * 0.019999999552965164f, tm06 = tm * 0.05999999865889549f;
    float A = t2m2 * 0.021276600658893585f, B = (mb * 1.063830018043518f) * t2;
    float C = t2m2 * 0.06382978707551956f, E = (mb * 3.1914889812469482f) * t2;
    float n = st_fma(m2, c->nm0, st_fma(t, c->nt0, bk0 + c->k0) + tb) - tm02;
    o->b[0] = st_fma(st_fma(b, c->x0b, c->x0k), m, n);
    n = st_fma(m2, c->nm1, st_fma(t, c->nt1, -c->k1 - (bk1 + tb3)) + tm06);
    o->b[1] = st_fma(st_fma(b, c->x1b, c->x1k), m, n);
    n = st_fma(m2, c->nm2, st_fma(t, c->nt2, (-c->k0 - bk0) + tb3)) - tm06;
    o->b[2] = st_fma(st_fma(b, c->x2b, c->x2k), m, n);
    n = st_fma(m2, c->nm3, (st_fma(t, c->nt3, bk1 + c->k1) - tb) + tm02);
    o->b[3] = st_fma(st_fma(b, c->x3b, c->x3k), m, n);
    float d = (st_fma(m2, c->d0m2, st_fma(m, c->d0m, c->d0k)) + A) - B;
    d = st_fma(st_fma(b, c->e0b, st_fma(m, c->e0m, c->e0k)), t2, d);
    d = st_fma(st_fma(st_fma(t, c->y1t, c->y1k), m, st_fma(t, c->y0t, c->y0k)), b, d);
    o->a[0] = st_fma(st_fma(m2, c->l0m2, st_fma(m, c->l0m, c->l0k)), t, d);
    d = (st_fma(b, c->k1b, c->k1k) - C) + E;
    d = st_fma(st_fma(b, c->g1b, c->g1k), t2, d);
    d = st_fma(st_fma(b, c->j1b, c->j1k), t, d);
    d = st_fma(st_fma(t, c->m1t, c->m1k), m2, d);
    o->a[1] = st_fma(st_fma(st_fma(b, c->h1b, c->h1k), t, st_fma(t2, c->i1t2, st_fma(b, c->i1b, c->i1k))), m, d);
    d = (st_fma(m2, c->d2m2, st_fma(m, c->d2m, c->d2k)) + C) - E;
    d = st_fma(st_fma(b, c->f2b, st_fma(m, c->f2m, c->f2k)), t2, d);
    d = st_fma(st_fma(st_fma(t, c->z1t, c->z1k), m, st_fma(t, c->z0t, c->z0k)), b, d);
    o->a[2] = st_fma(st_fma(m2, c->l2m2, st_fma(m, c->l2m, c->l2k)), t, d);
    d = (st_fma(b, c->k3b, c->k3k) - A) + B;
    d = st_fma(st_fma(b, c->g3b, c->g3k), t2, d);
    d = st_fma(st_fma(b, c->j3b, c->j3k), t, d);
    d = st_fma(st_fma(t, c->m3t, c->m3k), m2, d);
    o->a[3] = st_fma(st_fma(st_fma(b, c->h3b, c->h3k), t, st_fma(t2, c->i3t2, st_fma(b, c->i3b, c->i3k))), m, d);
}

/* the 3rd-order pre-filter from the glided Gain, G = max(Gain, 0.3) (compared in double) */
static inline void eng_pf2_set(struct tone3 *o, float g)
{
    float G = (double)g < 0.3 ? 0.30000001192092896f : g, G2 = G * G;
    float p = G2 * 0.5380433201789856f, q = G2 * 0.5268790125846863f;
    o->b[0] = st_fma(G, -0.5441436171531677f, p);
    o->b[1] = st_fma(G, 0.520652174949646f, -q);
    o->b[2] = st_fma(G, 0.5438904166221619f, -p);
    o->b[3] = st_fma(G, -0.5209053754806519f, q);
    o->a[0] = st_fma(G, -0.9947543740272522f, -0.01652161031961441f) + G2;
    o->a[1] = st_fma(G2, -1.8715180158615112f, st_fma(G, 1.8719559907913208f, -0.0018956620479002595f));
    o->a[2] = st_fma(G2, 0.7658116221427917f, st_fma(G, -0.7710571885108948f, 0.016136949881911278f));
    o->a[3] = st_fma(G2, 0.1057066023349762f, st_fma(G, -0.10614430159330368f, 0.0015110030071809888f));
}

/* the presence from p: e = 2^(1.993 p - 0.997), a = 2 / (e + 1), c = 2 / (1 / e + 1), shelves at K = 0.1434 with
   the factors 1.0057 and -0.9943, normalised by (a + K) 1.0057 */
static inline void eng_presence_set(struct sos *s, float p)
{
    float e = st_exp2f(st_fma(p, 1.993156909942627f, -0.9965784549713135f));
    float a = 2.0f / (e + 1.0f), c = 2.0f / (1.0f / e + 1.0f);
    float K = 0.14344780147075653f, U = 1.0056990385055542f, V = -0.994300901889801f;
    float f2 = a + K, f1 = K - a, f3 = K - c, f5 = c + K, n = f2 * U;
    s->b0 = (f5 * V) / n;
    s->b1 = st_fma(f5, U, f3 * V) / n;
    s->b2 = (f3 * U) / n;
    s->a0 = 1.0f;
    s->a1 = st_fma(f1, U, f2 * V) / n;
    s->a2 = (f1 * V) / n;
}

/* what differs between the models */
struct eng_model {
    const struct eng_ts *ts;
    const struct sos *power;        /* four sections */
    float out;                      /* the output level (the 120: 1) */
};

struct eng_state {
    float gain, volume, middle, treble;     /* glided */
    float bass, presence;                   /* the knobs the tone stack and the presence were set for */
    struct sos_state pf1, lowpass, pres, power[4];
    struct tone3 pf2, tone;
    struct stage st;
    struct sos pres_c;
};

static inline float eng_presence_knob(float k) { return k == 0.0f ? 0.01f : k; }

static inline void eng_init(struct eng_state *s, const struct eng_model *md, const float *k)
{
    *s = (struct eng_state){ 0 };
    s->gain = st_taper(st_pow2, k[EN_GAIN]);
    s->presence = eng_presence_knob(k[EN_PRESENCE] * 0.01f);
    s->volume = k[EN_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow2, k[EN_BASS]);
    s->middle = k[EN_MIDDLE] * 0.01f;
    s->treble = k[EN_TREBLE] * 0.01f;
    eng_pf2_set(&s->pf2, s->gain);
    eng_tone_set(&s->tone, md->ts, s->bass, s->middle, s->treble);
    eng_presence_set(&s->pres_c, s->presence);
}

static inline void eng_process(struct eng_state *s, const struct eng_model *md, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    float gain = st_taper(st_pow2, k[EN_GAIN]), presence = k[EN_PRESENCE] * 0.01f;
    float volume = k[EN_VOLUME] * 0.01f, bass = st_taper(st_pow2, k[EN_BASS]);
    float middle = k[EN_MIDDLE] * 0.01f, treble = k[EN_TREBLE] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    /* stage by stage over the block (each glide once per sample where it is used; Presence, not glided, before the
       first sample): the same samples as sample by sample */
    if (s->presence != presence) {
        s->presence = eng_presence_knob(presence);
        eng_presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&ENG_PF1, &s->pf1, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            eng_pf2_set(&s->pf2, s->gain);
        x[i] = s->gain * 0.5f * sos_run_div(&ENG_LOWPASS, &s->lowpass, tone3_run_std(&s->pf2, x[i]));
    }
    stage_pow2_block(&s->st, x, n, 1.0f, 1.0f, 0.0029296875f, 0.0027343749534338713f);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle) {
            s->bass = bass;
            eng_tone_set(&s->tone, md->ts, s->bass, s->middle, s->treble);
        }
        x[i] = s->volume * 4.0f * tone3_run_b1b0(&s->tone, x[i]);
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 4; j++)
        sos_block(&md->power[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * md->out;
}

#define ENG_BLOCK(sym, idv, nm) \
    static const struct wvfx_param params[] = ENG_PARAMS; \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    { \
        (void)buffer; (void)env; \
        eng_init(state, &MODEL, k); \
        return 0; \
    } \
    static void process(void *state, const struct wvfx_io *io) { eng_process(state, &MODEL, io); } \
    const struct wvfx_desc sym = { \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(WVFX_AMP, idv), .menu = WVFX_AMP, \
        .nparams = sizeof params / sizeof params[0], .name = nm, .category = "Eng", .params = params, \
        .state_size = sizeof(struct eng_state), .buffer_size = 0, .init = init, .process = process, \
        .poll = 0, \
    };
