/*
 * The original's Flyman amps (docs/stock_port.md): the four models (1, 2, + 1, + 2) share the path and its tables
 * but for the PfMode section, the low pass, the clipper's level, the presence's input level, the output level and
 * the tone stack's precision, so the whole path is here and each model's file gives those.
 *
 * Per sample:
 *   pre-filters   three fixed 2nd-order sections, the PfMode section and a low pass (both divided by their a0)
 *   gain          x Gain (pow2)
 *   stage         the asymmetric power clipper (+-3 or +-0.8) on 4 points per sample, the FIR
 *   tone stack    3rd order from Bass (pow3), Middle, Treble and Volume (linear), in single or double precision
 *   presence      a 2nd-order section from Presence (pow1_5): sine, cosine and 2^x of it (armcc's sinf, cosf,
 *                 exp2f), on the tone stack's output x the model's level
 *   power amp     six fixed 2nd-order sections, x the model's level (the + 1: x Volume too)
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample, Presence takes effect at once; the tone
 * coefficients stop one glide step short of the knobs.
 */
#pragma once
#include "amp.h"

enum { FM_GAIN, FM_PRESENCE, FM_VOLUME, FM_BASS, FM_MIDDLE, FM_TREBLE };

#define FLYMAN_PARAMS { \
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, FM_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, FM_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, FM_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, FM_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, FM_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, FM_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

static const struct sos FLYMAN_PF[3] = {
    { 8.151378631591797f, -16.128480911254883f, 7.97831916809082f, 1.0f, -1.9180333614349365f,
      0.9204468131065369f },
    { 11.986227989196777f, -16.128480911254883f, 5.151795387268066f, 1.0f, -1.4820538759231567f,
      0.5490512847900391f },
    { 16.128480911254883f, 14.572543144226074f, 3.3417205810546875f, 1.0f, 0.6005560755729675f,
      0.0013755999971181154f },
};

/* the PfMode sections (A: the 1 and + 2, B: the 2 and + 1) and the low passes (1: the 1 and 2, 2: the + models) */
static const struct sos FLYMAN_PFMODE_A = { 0.9296044707298279f, -1.8054540157318115f, 0.875849187374115f,
                                            0.9999998807907104f, -1.805338978767395f, 0.805568277835846f };
static const struct sos FLYMAN_PFMODE_B = { 0.48174479603767395f, -0.9356322884559631f, 0.4538874924182892f, 1.0f,
                                            -1.8691530227661133f, 0.8733757734298706f };
static const struct sos FLYMAN_LOWPASS_1 = { 0.060889214277267456f, 0.12177842855453491f, 0.060889214277267456f,
                                             1.3382277488708496f, -1.7564431428909302f, 0.6617722511291504f };
static const struct sos FLYMAN_LOWPASS_2 = { 0.03138384222984314f, 0.06276768445968628f, 0.03138384222984314f,
                                             1.2466093301773071f, -1.8744646310806274f, 0.7533906698226929f };

static const struct sos FLYMAN_POWER[6] = {
    { 0.5671448707580566f, -1.1233744621276855f, 0.5562595725059509f, 1.0f, -1.99136483669281f,
      0.9915403127670288f },
    { 0.6695097088813782f, -1.1233744621276855f, 0.4557720124721527f, 1.0f, -1.738616943359375f,
      0.7392944097518921f },
    { 0.6060866117477417f, -1.1233744621276855f, 0.528272807598114f, 1.0f, -1.8031361103057861f,
      0.8219441175460815f },
    { 1.1233744621276855f, 0.023533500730991364f, 0.0006348000024445355f, 1.0f, -0.03208509832620621f,
      0.00043799998820759356f },
    { 0.9733759164810181f, -1.1233744621276855f, 0.33261141180992126f, 1.0f, -0.9202510714530945f,
      0.11039719730615616f },
    { 1.1233744621276855f, 0.829358696937561f, 0.011031599715352058f, 1.0f, 0.712796688079834f,
      0.021492699161171913f },
};

/* what differs between the models */
struct flyman_model {
    const struct sos *pfmode, *lowpass;
    float clip_l, clip_kneg, clip_kpos;     /* the clipper's level and its k below and above zero */
    float pres_in, out;                     /* the presence's input level, the output level */
    int out_volume;                         /* the output also x Volume (the + 1) */
    int dbl;                                /* the tone stack in double (the 2 and + 2) */
};

struct flyman_state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the knob the presence section was set for */
    struct sos_state pf[3], pfmode, lowpass, pres, power[6];
    struct stage st;
    struct tone3 tone;
    struct sos pres_c;
};

/* ---------------------------------------------------------------- the tone stack */

/*
 * From Bass b, Middle m, Treble t and Volume v. Shared by both precisions: the products of v (f9, f1, f3, f4, f11),
 * the polynomials p0..p3 scaled by v in the numerator, and the denominator's terms in t^2, m^2 and t^2 m^2
 * (f5, f6, f8, f12), each summed from the constant in the order written. The caller computes n3, summed
 * differently in single and double precision.
 */
#define FLYMAN_TONE(T, F, C) \
    T mb = m * b, tm = t * m, tb = t * b, vb = v * b, t2m2 = t2 * m2; \
    T f9 = F(vb, C(0.02118845), v * C(0.0005297113)), f1 = F(v, C(0.0004572767), vb * C(0.01829107)); \
    T f3 = (tm * C(0.025)) * v, f4 = (tb * C(3.0)) * v, f11 = (tm * C(0.075)) * v; \
    T p0 = F(F(b, C(0.9278518), C(0.02362213)), m, F(m2, C(-0.0231963), t * C(0.02603876))); \
    T p1 = F(F(b, C(-2.643003), C(-0.06643009)), m, F(t, C(-0.07602267), m2 * C(0.06607508))); \
    T p2 = F(F(b, C(2.502451), C(0.06213545)), m, F(m2, C(-0.06256128), t * C(0.07396124))); \
    T x3 = F(b, C(-0.7872997), C(-0.01932748)); \
    o->b[0] = (float)F(p0, v, (f9 + f7) - f3); \
    o->b[1] = (float)F(p1, v, f11 - (f4 + f1)); \
    o->b[2] = (float)F(p2, v, f4 - (f9 + f11)); \
    T f5 = t2m2 * C(0.009469697), f8 = t2m2 * C(0.02840909); \
    T f6 = (mb * C(0.3787879)) * t2, f12 = (mb * C(1.136364)) * t2; \
    T d0 = (F(m2, C(-0.03478734), F(m, C(-0.003864769), C(0.04171623))) + f5) - f6; \
    d0 = F(F(b, C(-0.5078084), F(m, C(0.002835092), C(-0.01321459))), t2, d0); \
    d0 = F(F(F(t, C(0.3623642), C(1.391493)), m, F(t, C(0.5517906), C(1.60339))), b, d0); \
    o->a[0] = (float)F(F(m2, C(-0.009059105), F(m, C(-0.004361764), C(0.01436126))), t, d0); \
    T d1 = (F(b, C(-4.478805), C(-0.113305)) - f8) + f12; \
    d1 = F(F(b, C(1.507808), C(0.03820654)), t2, d1); \
    d1 = F(F(b, C(-1.550406), C(-0.03922365)), t, d1); \
    d1 = F(F(t, C(0.0254985), C(0.09902925)), m2, d1); \
    o->a[1] = (float)F(F(F(b, C(-1.01994), C(0.01295778)), t, F(t2, C(-0.008895698), \
                       F(b, C(-3.96117), C(0.01172484)))), m, d1); \
    T d2 = (F(m2, C(-0.09370357), F(m, C(-0.01164432), C(0.1022881))) + f8) - f12; \
    d2 = F(F(b, C(-1.492192), F(m, C(0.00928612), C(-0.03678541))), t2, d2); \
    d2 = F(F(F(t, C(0.9527873), C(3.748143)), m, F(t, C(1.448209), C(4.156616))), b, d2); \
    o->a[2] = (float)F(F(m2, C(-0.02381968), F(m, C(-0.01275945), C(0.0356402))), t, d2); \
    T d3 = (F(b, C(-1.281189), C(-0.03069065)) - f5) + f6; \
    d3 = F(F(b, C(0.4921916), C(0.01179346)), t2, d3); \
    d3 = F(F(b, C(-0.4495937), C(-0.01077489)), t, d3); \
    d3 = F(F(t, C(0.007380289), C(0.02946166)), m2, d3); \
    o->a[3] = (float)F(F(F(b, C(-0.2952115), C(0.004163429)), t, F(t2, C(-0.003225514), \
                       F(b, C(-1.178466), C(0.003784248)))), m, d3);

/* in single precision: n3 = (f1 - f7) + f3 + (m^2 0.0197 - 0.024 t + x3 m) v */
static inline void flyman_tone_set(struct tone3 *o, float b, float m, float t, float v)
{
    float m2 = m * m, t2 = t * t, f7 = (t * b) * v;
    FLYMAN_TONE(float, st_fma, ST_F)
    o->b[3] = st_fma(st_fma(x3, m, st_fma(t, -0.02397733f, m2 * 0.01968249f)), v, (f1 - f7) + f3);
}

/* in double from single-precision knobs: m^2, t^2 and t b v rounded to single first; n3's - t b v moved into the
   v term as (-0.024 - b) t */
static inline void flyman_tone_set_d(struct tone3 *o, float bass, float middle, float treble, float volume)
{
    double b = bass, m = middle, t = treble, v = volume;
    double m2 = (float)(middle * middle), t2 = (float)(treble * treble), f7 = (float)((treble * bass) * volume);
    FLYMAN_TONE(double, st_fmad, ST_D)
    o->b[3] = (float)st_fmad(st_fmad(-0.02397733 - b, t, st_fmad(x3, m, m2 * 0.01968249)), v, f1 + f3);
}

static inline void flyman_tone(struct flyman_state *s, const struct flyman_model *md)
{
    if (md->dbl)
        flyman_tone_set_d(&s->tone, s->bass, s->middle, s->treble, s->volume);
    else
        flyman_tone_set(&s->tone, s->bass, s->middle, s->treble, s->volume);
}

/* ---------------------------------------------------------------- the presence */

/* from Presence p: e = 2^(0.519 p), q = 0.647 - 0.185 p, (1 + K sin q e, -2 cos q, 1 - K sin q e) /
   (1 + K sin q / e, -2 cos q, 1 - K sin q / e), K = 1.3736 */
static inline void flyman_presence_set(struct sos *c, float p)
{
    float e = st_exp2f(p * 0.5190512537956238f);
    float q = st_fma(p, -0.18521861732006073f, 0.6472678184509277f);
    float k = st_sinf(q) * 1.3736263513565063f, n = k * e, d = k / e;
    float cs = st_cosf(q) * -2.0f;
    c->b0 = n + 1.0f;
    c->b1 = cs;
    c->b2 = 1.0f - n;
    c->a0 = d + 1.0f;
    c->a1 = cs;
    c->a2 = 1.0f - d;
}

/* ---------------------------------------------------------------- the block */

static inline void flyman_init(struct flyman_state *s, const struct flyman_model *md, const float *k)
{
    *s = (struct flyman_state){ 0 };
    s->gain = st_taper(st_pow2, k[FM_GAIN]);
    s->presence = st_taper(st_pow1_5, k[FM_PRESENCE]);
    s->volume = k[FM_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow3, k[FM_BASS]);
    s->middle = k[FM_MIDDLE] * 0.01f;
    s->treble = k[FM_TREBLE] * 0.01f;
    flyman_tone(s, md);
    flyman_presence_set(&s->pres_c, s->presence);
}

static inline void flyman_process(struct flyman_state *s, const struct flyman_model *md, const struct wvfx_io *io)
{
    const float *k = io->knobs;
    float gain = st_taper(st_pow2, k[FM_GAIN]), presence = st_taper(st_pow1_5, k[FM_PRESENCE]);
    float volume = k[FM_VOLUME] * 0.01f, bass = st_taper(st_pow3, k[FM_BASS]);
    float middle = k[FM_MIDDLE] * 0.01f, treble = k[FM_TREBLE] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
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
    /* stage by stage over the block (each glide once per sample where it is used; Presence, not glided, before the
       first sample): the same samples as sample by sample */
    if (s->presence != presence) {
        s->presence = presence;
        flyman_presence_set(&s->pres_c, presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    for (int j = 0; j < 3; j++)
        sos_block(&FLYMAN_PF[j], &s->pf[j], x, n);
    sos_block_div(md->pfmode, &s->pfmode, x, n);
    sos_block_div(md->lowpass, &s->lowpass, x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        x[i] = x[i] * s->gain;
    }
    stage_pow_asym_block(&s->st, x, n, md->clip_l, md->clip_kneg, md->clip_kpos);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle || s->volume != volume)
            flyman_tone(s, md);
        x[i] = tone3_run_b1b0(&s->tone, x[i]) * md->pres_in;
        vol[i] = s->volume;
    }
    sos_block_div(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 6; j++)
        sos_block(&FLYMAN_POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++) {
        float y = x[i] * md->out;
        if (md->out_volume)
            y = y * vol[i];
        p[2 * i] = p[2 * i + 1] = y;
    }
}

#define FLYMAN_BLOCK(sym, idv, nm) \
    static const struct wvfx_param params[] = FLYMAN_PARAMS; \
    static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k) \
    { \
        (void)buffer; (void)env; \
        flyman_init(state, &MODEL, k); \
        return 0; \
    } \
    static void process(void *state, const struct wvfx_io *io) { flyman_process(state, &MODEL, io); } \
    const struct wvfx_desc sym = { \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(WVFX_AMP, idv), .menu = WVFX_AMP, \
        .nparams = sizeof params / sizeof params[0], .name = nm, .category = "Flyman", .params = params, \
        .state_size = sizeof(struct flyman_state), .buffer_size = 0, .init = init, .process = process, \
        .poll = 0, \
    };
