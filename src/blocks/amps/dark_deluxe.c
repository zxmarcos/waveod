/*
 * Dark Deluxe (AMP › Dark): the original's Dark Deluxe amp (QME-200 V1.2.2, library entry 4.4, id 0x07000005) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h has the pieces):
 *   pre-filter    three fixed 2nd-order sections
 *   tone stack    3rd order from Bass and Treble (linear), x Gain (pow2)
 *   stage         the clip 5 - 5 e^(-8 |x / 10|) on 4 points per sample, the FIR
 *   power amp     13 fixed 2nd-order sections, x 2.4 x Volume
 * Unlike the other amps: Bass and Treble glide 0.004 per block of samples (also while bypassed), the tone stack
 * recomputed at each step; Gain and Volume glide 0.0001 per sample only while on. The stage's current point is
 * clipped from x 0.8 rather than from (x / 10) x 8. The original also runs an envelope whose output nothing uses;
 * it is left out. Init computes the tone stack from the knobs before their x 0.01 (the factors x 0.01 instead) and
 * sums n3 in another order, a rounding apart from Process's until Bass or Treble moves.
 */
#include "amp.h"

enum { K_GAIN, K_VOLUME, K_BASS, K_TREBLE };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }
#define KNOBD(name, k, d) { name, 0.0f, 100.0f, 1.0f, d, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOBD("Gain", K_GAIN, 30.0f), KNOB("Volume", K_VOLUME), KNOB("Bass", K_BASS), KNOB("Treble", K_TREBLE),
};

static const struct sos PF[3] = {
    { 3.903717517852783f, -7.2850260734558105f, 3.3814244270324707f, 1.0f, -1.8662011623382568f,
      0.8662365078926086f },
    { 7.2850260734558105f, 7.27518367767334f, 0.3599593937397003f, 1.0f, 1.2224481105804443f,
      0.26156970858573914f },
    { 7.160706520080566f, 7.2850260734558105f, 1.5722482204437256f, 1.0f, 0.6989871859550476f,
      0.0013342000311240554f },
};

static const struct sos POWER[13] = {
    { 0.038659900426864624f, -0.0748756006360054f, 0.036236099898815155f, 1.0f, -1.8041987419128418f,
      0.807519793510437f },
    { 0.7942373156547546f, -1.5848737955093384f, 0.7906765937805176f, 1.0f, -1.9960050582885742f,
      0.996067225933075f },
    { 0.9511125087738037f, -1.5848737955093384f, 0.649048924446106f, 1.0f, -1.6735117435455322f,
      0.6791977286338806f },
    { 0.8320233821868896f, -1.5848737955093384f, 0.7717090249061584f, 1.0f, -1.875596046447754f,
      0.9003052711486816f },
    { 0.8874511122703552f, -1.5848737955093384f, 0.7800316214561462f, 1.0f, -1.7966351509094238f,
      0.88055819272995f },
    { 0.8601039052009583f, -1.5848737955093384f, 0.7659509778022766f, 1.0f, -1.8300371170043945f,
      0.8698753714561462f },
    { 1.1113826036453247f, -1.5848737955093384f, 0.8804975748062134f, 1.0f, -1.4330039024353027f,
      0.7794072031974792f },
    { 1.5848737955093384f, -0.779492974281311f, 0.2057054042816162f, 1.0f, -0.5304440855979919f,
      0.13625310361385345f },
    { 1.5848737955093384f, -1.0303833484649658f, 0.5111629962921143f, 1.0f, -0.4602932035923004f,
      0.21187099814414978f },
    { 1.5848737955093384f, -0.012604599818587303f, 0.7306501269340515f, 1.0f, -0.096601203083992f,
      0.21572540700435638f },
    { 1.5848737955093384f, 0.43609461188316345f, 0.6012955904006958f, 1.0f, 0.16337670385837555f,
      0.5001997947692871f },
    { 1.5848737955093384f, 1.4524955749511719f, 0.6063321828842163f, 1.0f, 0.9914008975028992f,
      0.4645518958568573f },
    { 1.263840675354004f, 1.5848737955093384f, 0.5107321739196777f, 1.0f, 1.4527223110198975f,
      0.6016740798950195f },
};

struct state {
    float gain, volume, bass, treble;   /* glided */
    struct tone3 tone;
    struct sos_state pf[3], power[13];
    struct stage st;
};

/*
 * The tone stack from Bass b and Treble t (tb = t b), its constants for knobs 0-1 (Process) or 0-100 (Init, the
 * linear factors x 0.01; tb always from the 0-1 values):
 *   n0 = (b nb0 + nk0) + t 0.00142 + tb          n1 = (b nb1 - nk1) - (t 0.00142 + 3 tb)
 *   n2 = (b nb2 - nk0) - t 0.00142 + 3 tb        n3 = (b nb3 + nk1) - tb + t 0.00142 (Init: + t 0.00142 - tb)
 *   di = b dbi + dki
 */
struct deluxe_ts { float nb[4], t1, t2, db[4]; };

static const struct deluxe_ts TS = {
    { 0.11183589696884155f, -0.30135849118232727f, 0.26896411180496216f, -0.07944151014089584f },
    0.0014195339754223824f, 0.0014173459494486451f,
    { 1.2932419776916504f, -3.4827640056610107f, 3.0875580310821533f, -0.8980355858802795f },
};

static const struct deluxe_ts TS_INIT = {
    { 0.0011183589231222868f, -0.0030135847628116608f, 0.0026896409690380096f, -0.0007944150711409748f },
    1.419533964508446e-05f, 1.4173459021549206e-05f,
    { 0.012932419776916504f, -0.034827638417482376f, 0.03087558038532734f, -0.008980355225503445f },
};

static void tone_set(struct tone3 *o, const struct deluxe_ts *c, float b, float t, float tb, int init)
{
    float t1 = t * c->t1, t2 = t * c->t2, tb3 = tb * 3.0f;
    float n3 = st_fma(b, c->nb[3], 0.00011750840349122882f);
    o->b[0] = (st_fma(b, c->nb[0], 0.0001525624975329265f) + t1) + tb;
    o->b[1] = st_fma(b, c->nb[1], -0.00011750840349122882f) - (t2 + tb3);
    o->b[2] = (st_fma(b, c->nb[2], -0.0001525624975329265f) - t1) + tb3;
    o->b[3] = init ? (n3 + t2) - tb : (n3 - tb) + t2;
    o->a[0] = st_fma(b, c->db[0], 0.0018296079942956567f);
    o->a[1] = st_fma(b, c->db[1], -0.0012769449967890978f);
    o->a[2] = st_fma(b, c->db[2], -0.001828814041800797f);
    o->a[3] = st_fma(b, c->db[3], 0.0012777389492839575f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2, k[K_GAIN]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = k[K_BASS] * 0.01f;
    s->treble = k[K_TREBLE] * 0.01f;
    tone_set(&s->tone, &TS_INIT, k[K_BASS], k[K_TREBLE], s->treble * s->bass, 1);
    return 0;
}

/* the stage: the clip 5 - 5 e^(-8 |q|) on the 4 points of q = x / 10, the current one from x x 0.8 */
/* the stage over a block in place (amp.h, STAGE_BLOCK_POINTS's layout): the points of 0.1 x, the current one clipped
   from x itself (k 0.8), the 3 older ones by clip_env (k 8); no point is the last one's again. clip_env1 = clip_env
   (its side by the sign bit: x and 0.1 x agree, and where 0.1 x rounds to 0 both sides give 0) */
static void stage_block(struct stage *s, float *y, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], c = x * 0.1f, d = c - prev, *o = buf + 40 + 4 * i;
        o[0] = clip_env1(prev, 8.0f, 5.0f);
        o[1] = clip_env1(st_fma(d, 0.33333334f, prev), 8.0f, 5.0f);
        o[2] = clip_env1(st_fma(d, 0.6666667f, prev), 8.0f, 5.0f);
        o[3] = clip_env1(x, 0.8f, 5.0f);
        prev = c;
    }
    s->prev = prev;
    stage_block_fir(s, buf, y, n);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = io->knobs[K_BASS] * 0.01f;
    float treble = io->knobs[K_TREBLE] * 0.01f;
    if (s->bass != bass || s->treble != treble) {
        s->bass = st_glide_by(s->bass, bass, 0.004f);
        s->treble = st_glide_by(s->treble, treble, 0.004f);
        tone_set(&s->tone, &TS, s->bass, s->treble, s->treble * s->bass, 0);
    }
    if (!io->enabled)
        return;
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    /* stage by stage over the block (the glides once per sample where they are used): the same samples */
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    for (int j = 0; j < 3; j++)
        sos_block(&PF[j], &s->pf[j], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        vol[i] = s->volume;
        x[i] = tone3_run_std(&s->tone, x[i]) * s->gain;
    }
    stage_block(&s->st, x, n);
    for (int j = 0; j < 13; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 2.4000000953674316f * vol[i];
}

const struct wvfx_desc dark_deluxe_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x05),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dark Deluxe",
    .category = "Dark",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
