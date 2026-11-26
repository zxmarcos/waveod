/*
 * RC-Boost (DYN): the original's RC-Boost (QME-200 V1.2.2, library entry 0.7, id 0x0000000C) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A clean boost, mono (the left input; both outputs the same).
 * Single precision.
 *
 * x through a gain section set by Gain (b1 = 0, divided by its a0), the amps' 4-points stage (amp.h) with an
 * exponential clipper 0.02 v + k 0.98 (1 - e^(-0.9 |v|)) (odd; k = 2 - 0.25 Gain / 100) and the 40-tap FIR summed
 * onto x / 2, a 4th-order tone section by Bass (x 0.0095) and Treble (its coefficients polynomials, the original's
 * fused order; divided by its a0), x 0.65 Volume. Volume glides 0.0001 per sample; Treble 0.0001 per sample while
 * Bass or Treble differs from the knob (Bass jumps), the tone section recomputed then. Bypassed it passes the signal.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    float drive, volume, bass, treble;
    float gn0, gn2, gd0, gd1, gd2;
    float n[5], d[5];
    float gx1, gx2, gy1, gy2;
    float ex[5], ey[5];
    struct stage st;
};

/* from the raw Gain knob */
static void gain_set(struct state *s, float k)
{
    float m = k * 0.10959409177303314f;
    s->gn0 = -2.0603690147399902f - m;
    s->gn2 = m + 2.0603690147399902f;
    s->gd0 = k * 0.009999999776482582f + 1.1529250144958496f;
    s->gd1 = st_fma(k, -0.019989699125289917f, -0.3748123049736023f);
    s->gd2 = st_fma(k, 0.009989697486162186f, -0.7761245965957642f);
}

/* the tone section from Bass (x 0.0095) and Treble (x 0.01) */
static void eq_set(struct state *s)
{
    float b = s->bass, t = s->treble, *n = s->n, *d = s->d;
    n[0] = st_fma(st_fma((t * t), -0.41299450397491455f, st_fma(t, 0.9998067021369934f, 0.13976730406284332f)),
        (b * b), st_fma(st_fma(b, 0.4131878912448883f, 0.002662719925865531f), (t * t),
        st_fma((-0.13981950283050537f - t), b, st_fma(t, -0.006573567166924477f, -0.000907278386875987f))));
    n[1] = st_fma(st_fma(t, -3.8573360443115234f, st_fma((t * t), 1.5957109928131104f, -0.423493891954422f)),
        (b * b), st_fma(st_fma((t * t), -1.5957109928131104f, st_fma(t, 3.8573360443115234f, 0.4234659969806671f)),
        b, (((t * 0.012767880223691463f) + 0.0010263390140607953f) - ((t * t) * 0.0052890959195792675f))));
    n[2] = st_fma(st_fma(t, -5.573554039001465f, st_fma((t * t), 2.3087799549102783f, -0.45207270979881287f)), b,
        st_fma(st_fma((t * t), -2.3091659545898438f, st_fma(t, 5.573941230773926f, 0.45214730501174927f)), (b * b),
        st_fma((t * t), -3.6343681131256744e-05f, st_fma(t, 0.00037925640936009586f, 0.0007250886992551386f))));
    n[3] = st_fma(st_fma(t, -3.575100898742676f, st_fma((t * t), 1.4831769466400146f, -0.19228379428386688f)),
        (b * b), st_fma(st_fma((t * t), -1.4831769466400146f, st_fma(t, 3.575100898742676f, 0.19230809807777405f)),
        b, ((-0.0010266780154779553f - (t * 0.012767880223691463f)) + ((t * t) * 0.0052890959195792675f))));
    n[4] = st_fma(st_fma(t, -0.8588824272155762f, st_fma((t * t), 0.35692089796066284f, -0.02388909086585045f)), b,
        st_fma(st_fma((t * t), -0.35672760009765625f, st_fma(t, 0.8586891293525696f, 0.02386303059756756f)),
        (b * b), st_fma((t * t), -0.0026263759937137365f, st_fma(t, 0.006194309797137976f,
        0.00018185129738412797f))));
    d[0] = st_fma(st_fma((t * t), 0.49487990140914917f, st_fma(t, 0.0919322669506073f, -0.8320344090461731f)),
        (b * b), st_fma(st_fma(t, -0.09213332086801529f, st_fma((t * t), -0.4946866035461426f,
        0.831986129283905f)), b, st_fma((t * t), -0.0034225729759782553f, st_fma(t, -0.0004844074137508869f,
        0.005592054221779108f))));
    d[1] = st_fma(((b - (b * b)) * t), 0.33837220072746277f, st_fma(((b - (b * b)) * (t * t)), 1.923253059387207f,
        st_fma((b * b), 3.0796940326690674f, st_fma(b, -3.079720973968506f, (st_fma((t * t), 0.006414434872567654f,
        (t * 0.0010643460555002093f)) + -0.009738555178046227f)))));
    d[2] = st_fma(st_fma((t * t), 2.800477981567383f, st_fma(t, 0.4642961919307709f, -4.268911838531494f)),
        (b * b), st_fma(st_fma(t, -0.4638940989971161f, st_fma((t * t), -2.8008649349212646f, 4.268979072570801f)),
        b, st_fma((t * t), 0.00043071131221950054f, st_fma(t, -9.553130075801164e-05f, -0.001316532026976347f))));
    d[3] = st_fma(((b - (b * b)) * t), 0.2812049984931946f, st_fma(((b - (b * b)) * (t * t)), 1.8107190132141113f,
        st_fma((b * b), 2.6262829303741455f, st_fma(b, -2.6262590885162354f,
        (0.00974256545305252f - st_fma((t * t), 0.006414434872567654f, (t * 0.0010643460555002093f)))))));
    d[4] = st_fma(st_fma((t * t), 0.4386129081249237f, st_fma(t, 0.06334865093231201f, -0.6050298810005188f)),
        (b * b), st_fma(st_fma(t, -0.0635496973991394f, st_fma((t * t), -0.4384196102619171f,
        0.6050078272819519f)), b, st_fma((t * t), 0.0029918618965893984f, st_fma(t, 0.0005799386999569833f,
        -0.004271511919796467f))));
}

/* the clipper's two forms: the current point (a), the other three (b). e = e^(-0.9 |v|) (the original's e^(-+0.9
   v), amp.h exp_negabs), k the side's (kp, kn: from the block's Drive), by v's sign bit (at +0 e = 1 and every term
   is 0 with either k) */
static inline float clip_a(float v, float kp, float kn)
{
    float e = exp_negabs(v, 0.8999999761581421f);
    return st_fma(st_fma(-e, 0.9800000190734863f, 0.9800000190734863f), pick_sign(v, kp, kn),
                  v * 0.019999980926513672f);
}

static inline float clip_b(float v, float kp, float kn)
{
    float e = exp_negabs(v, 0.8999999761581421f);
    return st_fma(pick_sign(v, kp, kn) * 0.9800000190734863f, 1.0f - e, v * 0.019999980926513672f);
}

/* the stage over a block in place: clip_b on prev, 1/3 and 2/3, clip_a on the current point (prev is not the last
   one's again); then the FIR, summed onto acc[i], four samples at a time */
static void stage_block(struct stage *s, float *y, int n, float dr, const float *acc)
{
    float kp = st_fma(dr, -0.25f, 2.0f), kn = st_fma(-dr, -0.25f, -2.0f);
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, *o = buf + 40 + 4 * i;
        o[0] = clip_b(prev, kp, kn);
        o[1] = clip_b(st_fma(d, 0.33333334f, prev), kp, kn);
        o[2] = clip_b(st_fma(d, 0.6666667f, prev), kp, kn);
        o[3] = clip_a(x, kp, kn);
        prev = x;
    }
    s->prev = prev;
    stage_block_fir_acc(s, buf, y, n, AMP_FIR_ORDER2, acc);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->drive = k[0] * 0.01f;
    s->volume = k[1] * 0.01f;
    s->bass = k[2] * 0.009499999694526196f;
    s->treble = k[3] * 0.01f;
    gain_set(s, k[0]);
    eq_set(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float drive = k[0] * 0.01f, vol = k[1] * 0.01f, bass = k[2] * 0.009499999694526196f, treble = k[3] * 0.01f;
    float *p = io->io, xv[WVFX_MAX_FRAMES], gv[WVFX_MAX_FRAMES], vl[WVFX_MAX_FRAMES];
    const int nf = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || nf <= 0) {
        for (int i = 0; i < nf; i++)
            s->volume = st_glide(s->volume, vol);
        return;
    }
    /* stage by stage over the block (Drive, not glided, before the first sample; the Volume's and Treble's glides
       once per sample, the EQ following Treble after the stage): the same samples as sample by sample */
    if (s->drive != drive) {
        s->drive = drive;
        gain_set(s, k[0]);
    }
    for (int i = 0; i < nf; i++) {
        s->volume = st_glide(s->volume, vol);
        vl[i] = s->volume;
        float x = p[2 * i];
        float g = (st_fma(s->gn0, x, s->gn2 * s->gx2) - st_fma(s->gd2, s->gy2, s->gd1 * s->gy1)) / s->gd0;
        s->gx2 = s->gx1;
        s->gx1 = x;
        s->gy2 = s->gy1;
        s->gy1 = g;
        xv[i] = x * 0.5f;
        gv[i] = g;
    }
    stage_block(&s->st, gv, nf, s->drive, xv);
    for (int i = 0; i < nf; i++, p += 2) {
        if (s->bass != bass || s->treble != treble) {
            s->treble = st_glide(s->treble, treble);
            s->bass = bass;
            eq_set(s);
        }
        float f = gv[i];
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[4], ex[4], st_fma(n[3], ex[3], st_fma(n[2], ex[2], st_fma(f, n[0], n[1] * ex[1]))));
        float dd = st_fma(d[4], ey[4], st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1])));
        float e = (nn - dd) / d[0];
        for (int j = 4; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = f;
        ey[1] = ey[0] = e;
        p[0] = p[1] = (vl[i] * 0.6499999761581421f) * e;
    }
}

const struct wvfx_desc dyn_rc_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x107),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "RC-Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
