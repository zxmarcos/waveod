/*
 * Plexi Dist (DRIVE › Distortion): the original's Plexi Dist (QME-200 V1.2.2, library entry 3.16, id 0x0300002D) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample: two 2nd-order gain sections (Gain, level), a piecewise clipper on 4 points per sample (five ranges of
 * x, each a quadratic in u = |x| / (2 |x| + 1), (|x| + 1) / (10 |x|) or |x| / (|x| + 20)) with the anti-aliasing
 * FIR, a 3rd-order tone stack (Bass pow2, Middle, Treble), a fixed 1st-order section with no x term, x 2.88 Volume
 * (level). All five knobs glide 0.0001 per sample, Gain and Volume also while bypassed; the sections follow (a step
 * short).
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
};

#define PF2_B 0.4790513515472412f
#define LF_B1 0.8894000053405762f
#define LF_A1 -0.11060000211000443f

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    struct sos pf1, pf2;
    struct sos3 eq;
    struct sos_state s1, s2;
    struct sos3_state es;
    float lx1, ly1;                             /* the last section's x1, y1 */
    struct stage st;
};

static void gain_set(struct state *s, float g)
{
    float b1 = st_fma(g, -1.9019800424575806f, 0.09261148422956467f);
    s->pf1 = (struct sos){ st_fma(g, 41.8416633605957f, 0.944822371006012f), b1,
                           st_fma(g, -39.9396858215332f, -0.8522108793258667f), g + 0.944822371006012f, b1,
                           st_fma(g, 0.9019799828529358f, -0.8522108793258667f) };
    s->pf2 = (struct sos){ PF2_B, 0.0f, -PF2_B, g + -1.1016491651535034f,
                           st_fma(g, -1.859102487564087f, 2.0447804927825928f),
                           st_fma(g, 0.8591025471687317f, -0.9435960054397583f) };
}

/* the tone stack from Bass b, Middle m, Treble t */
static void eq_set(struct sos3 *c, float b, float m, float t)
{
    float m2 = m * m, t2 = t * t, tb = t * b, tm = t * m, mb = m * b, t2m2 = t2 * m2;
    c->b0 = st_fma(st_fma(b, 0.8413286805152893f, 0.01889047957956791f), m,
                   st_fma(m2, -0.01850922964513302f, st_fma(t, 0.02303875982761383f,
                          b * 0.022050950676202774f + 0.00048512101056985557f) + tb) - tm * 0.02199999988079071f);
    c->b1 = st_fma(st_fma(b, -2.374661922454834f, -0.05254846066236496f), m,
                   st_fma(m2, 0.052242569625377655f, st_fma(t, -0.0670226663351059f,
                          -0.00040816559339873493f - (b * 0.018552979454398155f + tb * 3.0f)) + tm * 0.06599999964237213f));
    c->b2 = st_fma(st_fma(b, 2.2253379821777344f, 0.04857619106769562f), m,
                   st_fma(m2, -0.04895742982625961f, st_fma(t, 0.06496123969554901f,
                          (-0.00048512101056985557f - b * 0.022050950676202774f) + tb * 3.0f)) - tm * 0.06599999964237213f);
    c->b3 = st_fma(st_fma(b, -0.6920046210289001f, -0.014918199740350246f), m,
                   st_fma(m2, 0.015224100090563297f, (st_fma(t, -0.020977329462766647f,
                          b * 0.018552979454398155f + 0.00040816559339873493f) - tb) + tm * 0.02199999988079071f));
    c->a0 = st_fma(st_fma(m2, -0.003088914090767503f, st_fma(m, -0.0021979210432618856f, 0.0056853159330785275f)), t,
            st_fma(st_fma(st_fma(t, 0.14040519297122955f, 0.866122305393219f), m,
                          st_fma(t, 0.24689680337905884f, 1.1328539848327637f)), b,
            st_fma(st_fma(b, -0.22343569993972778f, st_fma(m, 0.0015377480303868651f, -0.005144114140421152f)), t2,
                   (st_fma(m2, -0.01905469037592411f, st_fma(m, -0.004976376891136169f, 0.026075460016727448f))
                    + t2m2 * 0.003226666944101453f) - (mb * 0.14666670560836792f) * t2)));
    c->a1 = st_fma(st_fma(st_fma(b, -0.3897385001182556f, 0.006409164052456617f), t,
                          st_fma(t2, -0.0047644139267504215f, st_fma(b, -2.443380117416382f, 0.014352049678564072f))), m,
            st_fma(st_fma(t, 0.008574247360229492f, 0.05375437065958977f), m2,
            st_fma(st_fma(b, -0.6861593723297119f, -0.015295050106942654f), t,
            st_fma(st_fma(b, 0.663435697555542f, 0.01482057012617588f), t2,
                   (st_fma(b, -3.1291110515594482f, -0.0697474330663681f) - t2m2 * 0.009680000133812428f)
                   + (mb * 0.4399999976158142f) * t2))));
    c->a2 = st_fma(st_fma(m2, -0.007881753146648407f, st_fma(m, -0.00619141198694706f, 0.013675459660589695f)), t,
            st_fma(st_fma(st_fma(t, 0.35826149582862854f, 2.288543939590454f), m,
                          st_fma(t, 0.6331031918525696f, 2.8671491146087646f)), b,
            st_fma(st_fma(b, -0.6565642952919006f, st_fma(m, 0.004915586207062006f, -0.014215890318155289f)), t2,
                   (st_fma(m2, -0.05034798011183739f, st_fma(m, -0.013620959594845772f, 0.061928149312734604f))
                    + t2m2 * 0.009680000133812428f) - (mb * 0.4399999976158142f) * t2)));
    c->a3 = st_fma(st_fma(st_fma(b, -0.10892820358276367f, 0.0019801699090749025f), t,
                          st_fma(t2, -0.0016889190301299095f, st_fma(b, -0.7112864255905151f, 0.004245285876095295f))), m,
            st_fma(st_fma(t, 0.002396418945863843f, 0.01564829982817173f), m2,
            st_fma(st_fma(b, -0.19384059309959412f, -0.004064174834638834f), t,
            st_fma(st_fma(b, 0.21656429767608643f, 0.0045394268818199635f), t2,
                   (st_fma(b, -0.8708859086036682f, -0.018248960375785828f) - t2m2 * 0.003226666944101453f)
                   + (mb * 0.14666670560836792f) * t2))));
}

/* the clipper: in each range B u + (k v + base) with u, B, k, v, base per range; beyond +-6 an odd curve */
static float clip(float x)
{
    float a = st_abs(x), x2 = x * x, u, base, k, v, B;
    if (x > -0.5f && x < 0.0f) {
        u = a / st_fma(a, 2.0f, 1.0f);
        base = x2 * 0.7526299953460693f, k = -2.184000015258789f, v = a;
        B = st_fma(u, 2.3010001182556152f, 1.2454999685287476f);
    } else if (x >= 0.0f && x < 0.5f) {
        u = a / st_fma(a, 2.0f, 1.0f);
        base = a * 2.184000015258789f, k = -0.7526299953460693f, v = x2;
        B = st_fma(u, -2.3010001182556152f, -1.2454999685287476f);
    } else if (x >= 0.5f && x < 6.0f) {
        u = (a + 1.0f) / (a * 10.0f);
        base = st_fma(a, 0.023480625823140144f, 0.6063299775123596f), k = -0.0013914062874391675f, v = x2;
        B = st_fma(u, -4.923399925231934f, 0.9102299809455872f);
    } else if (x <= -0.5f && x > -6.0f) {
        u = (a + 1.0f) / (a * 10.0f);
        base = st_fma(x2, 0.0013914062874391675f, -0.6063299775123596f), k = -0.023480625823140144f, v = a;
        B = st_fma(u, 4.923399925231934f, -0.9102299809455872f);
    } else {
        u = a / (a + 20.0f);
        float y = st_fma(st_fma(u, -0.05432499945163727f, 0.23037000000476837f), u,
                         st_fma(x2, -3.416099900732661e-07f, st_fma(a, 0.0005180499865673482f, 0.6829299926757812f)));
        return x < 6.0f ? -y : y;
    }
    return st_fma(B, u, st_fma(v, k, base));
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_level, k[0]);
    s->volume = st_taper(st_level, k[1]);
    s->bass = st_taper(st_pow2, k[2]);
    s->middle = k[3] * 0.01f;
    s->treble = k[4] * 0.01f;
    gain_set(s, s->gain);
    eq_set(&s->eq, s->bass, s->middle, s->treble);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float vt = st_taper(st_level, io->knobs[1]), mt = io->knobs[3] * 0.01f, tt = io->knobs[4] * 0.01f;
    float bt = st_taper(st_pow2, io->knobs[2]), gt = st_taper(st_level, io->knobs[0]);
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int nf = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || nf <= 0) {
        for (int i = 0; i < nf; i++) {
            s->gain = st_glide(s->gain, gt);
            s->volume = st_glide(s->volume, vt);
            s->bass = st_glide(s->bass, bt);
            s->middle = st_glide(s->middle, mt);
            s->treble = st_glide(s->treble, tt);
        }
        return;
    }
    /* stage by stage over the block: Gain's glide before the stage, the others after it (each once per sample, its
       section following it); the same samples as sample by sample */
    for (int i = 0; i < nf; i++) {
        s->gain = st_glide(s->gain, gt);
        if (s->gain != gt)
            gain_set(s, s->gain);
        float v = sos_run_div(&s->pf1, &s->s1, p[2 * i]);
        float n = st_fma(s->pf2.b0, v, s->pf2.b2 * s->s2.x2);       /* no x1 term, from b2 x2 */
        float d = st_fma(s->pf2.a2, s->s2.y2, s->pf2.a1 * s->s2.y1);
        x[i] = sos_out(&s->s2, v, (n - d) / s->pf2.a0);
    }
    STAGE_BLOCK_POINTS(&s->st, x, nf, buf, clip(v));
    stage_block_fir(&s->st, buf, x, nf);
    for (int i = 0; i < nf; i++) {
        s->volume = st_glide(s->volume, vt);
        s->bass = st_glide(s->bass, bt);
        s->middle = st_glide(s->middle, mt);
        s->treble = st_glide(s->treble, tt);
        if (s->bass != bt || s->middle != mt || s->treble != tt)
            eq_set(&s->eq, s->bass, s->middle, s->treble);
        float v = sos3_run_div_b1(&s->eq, &s->es, x[i]);
        float y = st_fma(LF_B1, s->lx1, -(LF_A1 * s->ly1));
        s->lx1 = v;
        s->ly1 = y;
        p[2 * i] = p[2 * i + 1] = (s->volume * 2.879999876022339f) * y;
    }
}

const struct wvfx_desc plexi_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x110),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Plexi Dist",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
