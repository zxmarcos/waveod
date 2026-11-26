/*
 * Bass Dist (DRIVE › Bass Drive): the original's Bass Dist (QME-200 V1.2.2, library entry 3.23, id 0x03000040) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample: two 2nd-order sections from Gain (A25), one for the clean path, one for the distorted path: a hard clip
 * at +-4 on 4 points per sample with the anti-aliasing FIR, a fixed 2nd-order section, a rational clipper (drive.h
 * clip_cub) with the FIR, a fixed 3rd-order section. Blend (B25) mixes the two, clean + Blend (dist - clean); then a
 * 4th-order tone stack (Bass, Treble, B25), x 1.95 Volume (A25). Volume and Blend glide 0.005 per block (also while
 * bypassed), Treble 0.0001 per sample while the block is on (the tone stack follows, a step short); Gain and Bass are
 * read as they are, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Blend", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
};

static const struct sos MID = {
    14.36458969116211f, -1.3700579404830933f, -12.933919906616211f, 1.0f, -1.3700579404830933f, 0.4306684136390686f,
};
static const struct sos3 POST = {
    0.12937310338020325f, -0.1234091967344284f, -0.12896659970283508f, 0.12381570041179657f,
    1.0f, -0.8859220743179321f, -0.9987335801124573f, 0.8871884942054749f,
};
static const struct clip_cub CLIP = {
    2.313999891281128f, 3.253999948501587f, 4.63700008392334f, 4.166999816894531f, -0.2718999981880188f,
    -1.218000054359436f,
};

/* a 4th-order section, each sum from its first term, divided by a0 */
struct sos4 { float b[5], a[5]; };
struct sos4_state { float x[4], y[4]; };

static inline float sos4_run_div(const struct sos4 *c, struct sos4_state *s, float x)
{
    float n = c->b[0] * x, d = c->a[1] * s->y[0];
    for (int i = 1; i < 5; i++) {
        n = st_fma(c->b[i], s->x[i - 1], n);
        if (i < 4)
            d = st_fma(c->a[i + 1], s->y[i], d);
    }
    float y = (n - d) / c->a[0];
    for (int i = 3; i > 0; i--) {
        s->x[i] = s->x[i - 1];
        s->y[i] = s->y[i - 1];
    }
    s->x[0] = x;
    s->y[0] = y;
    return y;
}

struct state {
    float gain, blend, volume, bass, treble;    /* Gain and Bass as read, the rest glided (tapers) */
    struct sos clean, dist;
    struct sos4 eq;
    struct sos_state cs, ds, ms;
    struct sos3_state ps;
    struct sos4_state es;
    struct stage st1, st2;
};

static void gain_set(struct state *s, float g)
{
    float g2 = g * g, b1 = st_fma(g, 2.0011789798736572f, st_fma(g2, -1.9996670484542847f, 0.37414100766181946f));
    float d1 = st_fma(g, -1.9984890222549438f, -0.01737911067903042f);
    s->clean = (struct sos){ st_fma(g2, 17.670560836791992f, st_fma(g, -13.13541030883789f, -8.709819793701172f)), b1,
                             st_fma(g2, -15.670889854431152f, st_fma(g, 11.137249946594238f, 8.332085609436035f)),
                             st_fma(g, 3.535146951675415f, -5.5757551193237305f) + g2, b1,
                             st_fma(g2, 0.9996665716171265f, st_fma(g, -5.533301830291748f, 5.198020935058594f)) };
    s->dist = (struct sos){ st_fma(g, 23.887489318847656f, 0.3476386070251465f), d1,
                            st_fma(g, -21.888999938964844f, -0.32984429597854614f), g + 0.14622870087623596f, d1,
                            st_fma(g, 0.9984893798828125f, -0.12843440473079681f) };
}

/* one tone-stack coefficient: S = b^2 t + t^2 b, its terms in t, t^2, b, then b t and b^2 in the order the original
   sums them (bt_last), + b^2 t^2 c[7] (c: base, t, t^2, b, b^2, b t, S, b^2 t^2) */
static float eq_coef(const float *c, float b, float t, int bt_last)
{
    float b2 = b * b, t2 = t * t, bt = b * t, S = st_fma(b2, t, t2 * b);
    float v = c[1] == 1.0f ? t + c[0] : st_fma(t, c[1], c[0]);
    v = st_fma(b, c[3], st_fma(t2, c[2], v));
    v = bt_last ? st_fma(bt, c[5], st_fma(b2, c[4], v)) : st_fma(b2, c[4], st_fma(bt, c[5], v));
    return st_fma(S, c[6], v + (b2 * t2) * c[7]);
}

static const float EQ_B[5][8] = {
    { 0.04212946072220802f, 1.0f, -0.7126435041427612f, 0.031617410480976105f, -0.022531939670443535f,
      0.6885120272636414f, -0.5348262190818787f, 0.38114041090011597f },
    { -0.06947933882474899f, -2.112584114074707f, 1.5397499799728394f, -0.04652339965105057f, 0.034236859530210495f,
      -1.3469289541244507f, 1.0604959726333618f, -0.7740625739097595f },
    { -0.004192862194031477f, 0.35026779770851135f, -0.3520826995372772f, -0.009415117092430592f,
      0.004163771867752075f, 0.08864308148622513f, -0.10958150029182434f, 0.1305198073387146f },
    { 0.048585791140794754f, 1.6376320123672485f, -1.064797043800354f, 0.036074448376894f, -0.023787910118699074f,
      1.1094529628753662f, -0.823019802570343f, 0.5365865230560303f },
    { -0.017034349963068962f, -0.8753156065940857f, 0.5897740125656128f, -0.011753340251743793f,
      0.007919218391180038f, -0.5396789908409119f, 0.406931608915329f, -0.2741841971874237f },
};
static const float EQ_A[5][8] = {
    { 0.18916450440883636f, 0.6463428139686584f, -0.7888715267181396f, 0.09425362944602966f, -0.09876002371311188f,
      0.228684201836586f, -0.3049122989177704f, 0.38114041090011597f },
    { -0.3842765986919403f, -1.386612057685852f, 1.6945619583129883f, -0.18220199644565582f, 0.1890493929386139f,
      -0.4644376039505005f, 0.6192501187324524f, -0.7740625739097595f },
    { 0.0653909221291542f, 0.30777010321617126f, -0.3781867027282715f, 0.022772949188947678f, -0.02194019965827465f,
      0.07831189781427383f, -0.1044159010052681f, 0.1305198073387146f },
    { 0.2663029134273529f, 0.9591553211212158f, -1.1721149682998657f, 0.1263476014137268f, -0.1311051994562149f,
      0.32195189595222473f, -0.4292691946029663f, 0.5365865230560303f },
    { -0.13657300174236298f, -0.5266559720039368f, 0.6446108222007751f, -0.061172209680080414f, 0.06275604665279388f,
      -0.16451050341129303f, 0.2193472981452942f, -0.2741841971874237f },
};
/* which of b t and b^2 each coefficient adds last (b0..b4, a0..a4) */
static const uint8_t EQ_B_BTLAST[5] = { 0, 1, 0, 0, 1 }, EQ_A_BTLAST[5] = { 0, 1, 0, 0, 1 };

static void eq_set(struct sos4 *c, float b, float t)
{
    for (int i = 0; i < 5; i++) {
        c->b[i] = eq_coef(EQ_B[i], b, t, EQ_B_BTLAST[i]);
        c->a[i] = eq_coef(EQ_A[i], b, t, EQ_A_BTLAST[i]);
    }
}

/* x held at +-4 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip4(float x) { return __builtin_fminf(__builtin_fmaxf(x, -4.0f), 4.0f); }

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[0]);
    s->blend = st_taper(st_b25, k[1]);
    s->volume = st_taper(st_a25, k[2]);
    s->bass = st_taper(st_b25, k[3]);
    s->treble = st_taper(st_b25, k[4]);
    gain_set(s, s->gain);
    eq_set(&s->eq, s->bass, s->treble);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->volume = st_glide_by(s->volume, st_taper(st_a25, io->knobs[2]), 0.005f);
    s->blend = st_glide_by(s->blend, st_taper(st_b25, io->knobs[1]), 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float g = st_taper(st_a25, io->knobs[0]), tt = st_taper(st_b25, io->knobs[4]), bt = st_taper(st_b25, io->knobs[3]);
    /* stage by stage over the block (Gain, not glided, before the first sample; Treble's glide once per sample, the
       EQ following it): the same samples as sample by sample */
    if (s->gain != g) {
        s->gain = g;
        gain_set(s, g);
    }
    float *p = io->io, c[WVFX_MAX_FRAMES], d[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        c[i] = d[i] = p[2 * i];
    sos_block_div(&s->clean, &s->cs, c, n);
    sos_block_div(&s->dist, &s->ds, d, n);
    STAGE_BLOCK_POINTS(&s->st1, d, n, buf, clip4(v));
    stage_block_fir(&s->st1, buf, d, n);
    sos_block(&MID, &s->ms, d, n);
    STAGE_BLOCK_POINTS(&s->st2, d, n, buf, clip_cub(&CLIP, v));
    stage_block_fir_ord(&s->st2, buf, d, n, DRV_FIR_ORDER_0321);
    for (int i = 0; i < n; i++) {
        float tr = st_glide(s->treble, tt);
        s->treble = tr;
        if (s->bass != bt || tr != tt) {
            s->bass = bt;
            eq_set(&s->eq, bt, tr);
        }
        float v = sos3_run_div_b1(&POST, &s->ps, d[i]);
        float y = sos4_run_div(&s->eq, &s->es, st_fma(s->blend, v - c[i], c[i]));
        p[2 * i] = p[2 * i + 1] = (y * 1.9499999284744263f) * s->volume;
    }
}

const struct wvfx_desc bass_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x117),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bass Dist",
    .category = "Bass Drive",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
