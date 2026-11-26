/*
 * Strive (DRIVE › Distortion): the original's Strive (QME-200 V1.2.2, library entry 3.20, id 0x03000032) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The effect runs on the left input; both outputs
 * crossfade from their own input to it. Single precision throughout.
 *
 * Per sample: a fixed 2nd-order pre-filter, a 2nd-order gain section (Gain, pow2_5 x 0.95), a second fixed
 * pre-filter, a soft clipper on 4 points per sample, A (1 - e^(-k x)) with A and k each side picked by Mode, with the
 * anti-aliasing FIR, a 3rd-order tone section (Tone, pow0_3 at the knob's whole value), a 1st-order level section
 * (Volume), five fixed 2nd-order sections, x Volume (x 0.1 at Mode 1, x 0.2 at Mode 2). The output is 2 (dry + (wet
 * - dry) m), m gliding 0.01 per sample towards 1 while the block is on and 0 while it is off: the original runs the
 * whole path in bypass too and fades itself out. Gain, Tone and Volume glide 0.0001 per sample, only while m was not
 * above the on/off state at the block's start; the sections follow (the gain and tone sections a step short).
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Mode", 0.0f, 2.0f, 1.0f, 0.0f, 3, 0x15, 0, 0, 0, 0 },
};

/* the pre-filters: no x1 term */
static const struct sos PF1 = { 0.9945198893547058f, 0.0f, -0.9945198893547058f, 1.0f, -0.003608305938541889f,
                                -0.9920297265052795f };
static const struct sos PF2 = { -2.5386290550231934f, 0.0f, 2.5386290550231934f, 1.0f, -1.790681004524231f,
                                0.7918323874473572f };
static const struct sos POST[5] = {
    { 1.0389000177383423f, -2.0f, 0.9613999724388123f, 1.0f, -1.9564000368118286f, 0.9567000269889832f },
    { 1.4010000228881836f, -2.0f, 0.7267000079154968f, 1.0f, -1.5261000394821167f, 0.6144999861717224f },
    { 1.2578999996185303f, -2.0f, 0.7473000288009644f, 1.0f, -1.3598999977111816f, 0.3677000105381012f },
    { 2.0f, 0.47029998898506165f, 1.777500033378601f, 1.0f, 0.24400000274181366f, 0.871999979019165f },
    { 1.7187999486923218f, 2.0f, 0.6110000014305115f, 1.0f, 1.3990999460220337f, 0.6175000071525574f },
};

struct state {
    float dist, tone, level, mix;       /* glided */
    struct sos gain;
    struct sos3 tc;
    struct fos lc;                      /* the level section, a0 in la0 */
    float la0;
    struct sos_state pf1, gs, pf2, post[5];
    struct sos3_state ts;
    struct fos_state ls;
    struct stage st;
};

/* the pre-filters' form: b0 x + b2 x2 (from b2), a1 y1 + a2 y2 */
static inline float pf_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b0, x, c->b2 * s->x2);
    float d = st_fma(s->y2, c->a2, s->y1 * c->a1);
    return sos_out(s, x, n - d);
}

static void gain_set(struct sos *c, float d)
{
    *c = (struct sos){ st_fma(d, 2220.404052734375f, 23.1337890625f), st_fma(d, -38.80799865722656f, 2.2675740718841553f),
                       st_fma(d, -2181.595947265625f, -20.8662109375f), d + 23.1337890625f, 2.2675740718841553f,
                       -20.8662109375f - d };
}

static void tone_set(struct sos3 *c, float t)
{
    *c = (struct sos3){ -0.020895300433039665f, 0.018438229337334633f, 0.020895300433039665f, -0.018438229337334633f,
                        t + -1.0977150201797485f, st_fma(t, -2.8946070671081543f, 3.0785679817199707f),
                        st_fma(t, 2.789726972579956f, -2.8701300621032715f),
                        st_fma(t, -0.8951200842857361f, 0.889275312423706f) };
}

static void level_set(struct state *s, float l)
{
    s->lc = (struct fos){ -0.5f, 0.5f, 1.4998869895935059f - l };
    s->la0 = l + -1.5001130104064941f;
}

/* the clipper's constants by Mode (0 differs from 1, any other value takes the second word of each pair) */
struct clip_k { float Ap, An, kp, kn; };

static struct clip_k clip_k(float mode)
{
    if (mode == 0.0f)
        return (struct clip_k){ 0.10000000149011612f, -0.10000000149011612f, -3.0f, 3.0f };
    int one = mode == 1.0f;
    return (struct clip_k){ one ? 0.800000011920929f : 0.4000000059604645f,
                            one ? -0.800000011920929f : -0.20000000298023224f, one ? -1.0f : -2.0f, one ? 1.0f : 4.0f };
}

/* A - A e with e = e^(kp x) above zero (kp < 0), e^(kn x) below (kn > 0): e^(-k |x|) with k = -kp or kn (amp.h,
   exp_negabs), the side by x's sign bit (at +0 e = 1 and both sides give 0) */
static inline float clip(const struct clip_k *c, float x)
{
    float e = exp_negabs(x, pick_sign(x, -c->kp, c->kn));
    float A = pick_sign(x, c->Ap, c->An);
    return st_fma(-e, A, A);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    float g = st_taper(st_pow2_5, k[0]);
    s->dist = g * 0.949999988079071f;
    s->tone = st_pow0_3[(int)k[1]];
    s->level = k[2] * 0.01f;
    s->mix = 1.0f;
    s->gain = (struct sos){ st_fma(g, 2109.3837890625f, 23.1337890625f), st_fma(g, -36.86759948730469f, 2.2675740718841553f),
                            st_fma(g, -2072.51611328125f, -20.8662109375f), s->dist + 23.1337890625f, 2.2675740718841553f,
                            -20.8662109375f - s->dist };
    tone_set(&s->tc, s->tone);
    level_set(s, s->level);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    if (io->frames == 0)
        return;
    float on = io->enabled ? 1.0f : 0.0f, mix0 = s->mix, m = io->knobs[3];
    float dt = st_taper(st_pow2_5, io->knobs[0]) * 0.949999988079071f;
    float tt = st_pow0_3[(int)io->knobs[1]], lt = io->knobs[2] * 0.01f;
    struct clip_k ck = clip_k(m);
    /* stage by stage over the block: the Distortion's glide before the stage, Tone's and Level's after it (each
       once per sample, its section following it; only while fading in or on, as the block began); the same
       samples as sample by sample */
    const int glide = mix0 <= on;
    float *p = io->io, x[WVFX_MAX_FRAMES], lev[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        if (glide) {
            s->dist = st_glide(s->dist, dt);
            if (s->dist != dt)
                gain_set(&s->gain, s->dist);
        }
        x[i] = pf_run(&PF2, &s->pf2, sos_run_div(&s->gain, &s->gs, pf_run(&PF1, &s->pf1, p[2 * i])));
    }
    STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip(&ck, v));
    stage_block_fir(&s->st, buf, x, n);
    for (int i = 0; i < n; i++) {
        if (glide) {
            s->tone = st_glide(s->tone, tt);
            if (s->tone != tt)
                tone_set(&s->tc, s->tone);
            if (s->level != lt) {
                s->level = st_glide(s->level, lt);
                level_set(s, s->level);
            }
        }
        lev[i] = s->level;
        x[i] = fos_run_div(&s->lc, s->la0, &s->ls, sos3_run_div_b1(&s->tc, &s->ts, x[i]));
    }
    for (int k = 0; k < 5; k++)
        sos_block(&POST[k], &s->post[k], x, n);
    for (int i = 0; i < n; i++) {
        float w = lev[i] * x[i];
        if (m == 1.0f)
            w = w * 0.10000000149011612f;
        else if (m == 2.0f)
            w = w * 0.20000000298023224f;
        s->mix = st_glide_by(s->mix, on, 0.01f);
        float l = st_fma(w - p[2 * i], s->mix, p[2 * i]), r = st_fma(w - p[2 * i + 1], s->mix, p[2 * i + 1]);
        p[2 * i] = l + l;
        p[2 * i + 1] = r + r;
    }
}

const struct wvfx_desc strive_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x114),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Strive",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
