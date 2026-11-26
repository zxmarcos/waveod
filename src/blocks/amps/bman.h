/*
 * The pieces the original's B-Man amps share (docs/stock_port.md): the knobs and their glide, the pre-filters, the
 * tone stack's constants, the envelope stage, the presence, the state. Each model's file has its gain section and
 * its output level.
 *
 * Per sample:
 *   pre-filters   x 1.1, two fixed 2nd-order sections
 *   gain          the model's section from Gain (pow2): N 1st order, Bri 2nd order
 *   tone stack    the classic one (amp.h, tone_brit_f) from Bass (pow3_5), Middle and Treble (linear)
 *   stage         the envelope-driven clip 0.15 - 0.15 e^(-k |x|), k = 1.4 + 2 env (env: |x| through a fixed low
 *                 pass), x max(1 / (1 + 3 k), 0.1), on 4 points per sample, the FIR
 *   presence      a shelf from Presence (pow1_5)
 *   output        x Volume x the model's level
 * Gain, Volume, Middle and Treble glide 0.0001 per sample; Bass and Presence take effect at once. The gain and tone
 * coefficients stop one glide step short of the knobs.
 */
#pragma once
#include "amp.h"

enum { BM_GAIN, BM_PRESENCE, BM_VOLUME, BM_BASS, BM_MIDDLE, BM_TREBLE };

#define BMAN_PARAMS(gain) { \
    { "Gain", 0.0f, 100.0f, 1.0f, (gain), BM_GAIN, 0x15, 0, 0, 0, 0 }, \
    { "Presence", 0.0f, 100.0f, 1.0f, 50.0f, BM_PRESENCE, 0x15, 0, 0, 0, 0 }, \
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, BM_VOLUME, 0x15, 0, 0, 0, 0 }, \
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, BM_BASS, 0x15, 0, 0, 0, 0 }, \
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, BM_MIDDLE, 0x15, 0, 0, 0, 0 }, \
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, BM_TREBLE, 0x15, 0, 0, 0, 0 }, \
}

static const struct sos BMAN_PF[2] = {
    { 6.685556888580322f, -8.51430606842041f, 1.8307924270629883f, 1.0f, -1.304119348526001f,
      0.3044809103012085f },
    { 8.51430606842041f, 3.8251593112945557f, 7.94000006862916e-05f, 1.0f, 0.38221681118011475f,
      7.029999687802047e-05f },
};

/* the tone stack (amp.h, tone_brit_f), the same in both */
static const struct brit_ts BMAN_TS = {
    .nb0 = 0.01651759f, .nk0 = 0.0004129397f, .nb1 = 0.0127989f, .nk1 = -0.00031997249f,
    .nm = { 0.01600963f, -0.04323924f, 0.03863323f, -0.01140362f },
    .nt = { 0.02613953f, -0.07612805f, 0.07386047f, -0.02387195f },
    .mb = { 0.6284257f, -1.721283f, 1.557289f, -0.4644315f },
    .m2 = { -0.01571064f, 0.04303207f, -0.03893222f, 0.01161079f },
    .db = { 1.197923f, -3.194205f, 2.802077f, -0.8057952f },
    .dk = { 0.03129433f, -0.08077437f, 0.06870983f, -0.01922147f },
    .dm = { -0.01352552f, 0.03629591f, -0.03183162f, 0.009061233f },
};

/* the presence shelf's exponent factor (amp.h, shelf_set: e = 2^(1.885 p)); its K and middle terms are the model's */
#define BMAN_PRES_X 1.8851943016052246f

struct bman_knobs { float gain, presence, volume, bass, middle, treble; };

static inline void bman_knobs(struct bman_knobs *o, const float *k)
{
    o->gain = st_taper(st_pow2, k[BM_GAIN]);
    o->presence = st_taper(st_pow1_5, k[BM_PRESENCE]);
    o->volume = k[BM_VOLUME] * 0.01f;
    o->bass = st_taper(st_pow3_5, k[BM_BASS]);
    o->middle = k[BM_MIDDLE] * 0.01f;
    o->treble = k[BM_TREBLE] * 0.01f;
}

struct bman_state {
    float gain, volume, middle, treble;     /* glided */
    float bass, presence;                   /* the knobs the tone stack and the presence were set for */
    struct sos_state pf[2];
    struct tone3 tone;
    struct env_state env;
    struct stage st;
    struct sos pres_c;
    struct sos_state pres;
};

/* the state from the knobs; the presence's K and middle terms (pk, pc) are the model's */
static inline void bman_init(struct bman_state *s, const struct bman_knobs *k, float pk, float pc)
{
    *s = (struct bman_state){ 0 };
    s->gain = k->gain;
    s->volume = k->volume;
    s->middle = k->middle;
    s->treble = k->treble;
    s->bass = k->bass;
    s->presence = k->presence;
    tone_brit_f(&s->tone, &BMAN_TS, s->bass, s->middle, s->treble);
    shelf_set(&s->pres_c, s->presence, BMAN_PRES_X, pk, pc);
}

/* one sample's glide (also while bypassed) */
static inline void bman_glide(struct bman_state *s, const struct bman_knobs *k)
{
    s->gain = st_glide(s->gain, k->gain);
    s->volume = st_glide(s->volume, k->volume);
    s->middle = st_glide(s->middle, k->middle);
    s->treble = st_glide(s->treble, k->treble);
}

/*
 * The models run stage by stage over the block (each glide once per sample where it is used; Presence, not glided,
 * before the first sample), the same samples as sample by sample:
 *   per sample   bman_glide_pre, the model's gain section from the glided Gain, bman_tone_update, bman_pre, the
 *                gain section, bman_tone_env (the tone stack, the stage's k from the envelope)
 *   the block    bman_post_block (the envelope stage)
 *   per sample   the Volume's glide, bman_out (the presence, x Volume x level)
 */
static inline void bman_glide_pre(struct bman_state *s, const struct bman_knobs *k)
{
    s->gain = st_glide(s->gain, k->gain);
    s->middle = st_glide(s->middle, k->middle);
    s->treble = st_glide(s->treble, k->treble);
}

static inline void bman_presence_update(struct bman_state *s, const struct bman_knobs *k, float pk, float pc)
{
    if (s->presence != k->presence) {
        s->presence = k->presence;
        shelf_set(&s->pres_c, s->presence, BMAN_PRES_X, pk, pc);
    }
}

static inline void bman_tone_update(struct bman_state *s, const struct bman_knobs *k)
{
    if (s->bass != k->bass || s->middle != k->middle || s->treble != k->treble) {
        s->bass = k->bass;
        tone_brit_f(&s->tone, &BMAN_TS, s->bass, s->middle, s->treble);
    }
}

static inline float bman_pre(struct bman_state *s, float x)
{
    x = sos_run(&BMAN_PF[0], &s->pf[0], x * 1.1f);
    return sos_run(&BMAN_PF[1], &s->pf[1], x);
}

/* after the gain section: the tone stack; the stage's k (1.4 + 2 env) into *k */
static inline float bman_tone_env(struct bman_state *s, float x, float *k)
{
    x = tone3_run_std(&s->tone, x);
    *k = st_fma(amp_env_run(&s->env, x), 2.0f, 1.4f);
    return x;
}

static inline void bman_post_block(struct bman_state *s, float *x, const float *k, int n)
{
    stage_env_block(&s->st, x, k, n, 0.1f, 0.15f);
}

/* after the stage: the presence, x Volume x level */
static inline float bman_out(struct bman_state *s, float x, float level)
{
    x = sos_run_div_b1(&s->pres_c, &s->pres, x);
    return s->volume * level * x;
}
