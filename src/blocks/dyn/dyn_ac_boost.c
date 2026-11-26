/*
 * AC-Boost (DYN): the original's AC-Boost (QME-200 V1.2.2, library entry 0.5, id 0x0000000A) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A drive-style boost, mono (the left input; both outputs the same).
 * Single precision, part in double.
 *
 * 1.1 x (in double) through a 2nd-order section, then a gain section set by Gain (g = pow0_5(Gain); its coefficients
 * in double, b1 = 0, divided by its a0); the amps' 4-points stage (amp.h) with an exponential clipper 0.02 v +
 * k 0.98 (1 - e^(-1.4 |v|)) (odd; k = 1.35 - 0.22 g above zero, 0.65 - 0.1 g below, in double) and the 40-tap FIR,
 * plus half the first section's output (in double); a 5th-order tone section by Bass and Treble (its 12
 * coefficients polynomials in both, in double, the original's fused order; divided by its a0), x Volume / 2 (in
 * double) and five 2nd-order sections. Volume glides 0.0001 per sample; Treble 0.0001 per sample while Bass or
 * Treble differs from the knob (Bass jumps), the tone section recomputed then. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 29.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PF = {
    0.9592763781547546f, -1.786057472229004f, 0.8268070220947266f, 1.0f, -1.8592952489852905f, 0.8596842885017395f,
};
static const struct sos POST[5] = {
    { 0.6647089719772339f, -1.3133741617202759f, 0.6487541198730469f, 1.0f, -1.9893115758895874f,
      0.9893636107444763f },
    { 1.122463345527649f, -1.3133741617202759f, 0.22276370227336884f, 1.0f, -1.5750846862792969f,
      0.604121208190918f },
    { 1.3133741617202759f, -0.4222477972507477f, 0.005287699867039919f, 1.0f, -0.5213683843612671f,
      0.19378599524497986f },
    { 1.3133741617202759f, 0.23999999463558197f, 0.022089999169111252f, 1.0f, 1.400674819946289f,
      0.5891482830047607f },
    { 0.8562610745429993f, 1.3133741617202759f, 0.585350513458252f, 1.0f, 1.3538330793380737f, 0.5493118762969971f },
};

/* the FIR's pair order */
static const uint8_t FIR_ORDER[20] = { 0, 3, 2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

struct state {
    float drive, volume, bass, treble;
    float gn0, gn2, gd0, gd1, gd2;      /* the gain section */
    float n[6], d[6];                   /* the tone section, d[0] divides */
    struct sos_state pf, post[5];
    float gx1, gx2, gy1, gy2;
    float ex[6], ey[6];
    struct stage st;
};

static void gain_set(struct state *s, float g)
{
    double dg = (double)g, m = dg * 23.85269;
    s->gn0 = (float)(-2.242152 - m);
    s->gn2 = (float)(m + 2.242152);
    s->gd0 = (float)(dg + 0.5764625);
    s->gd1 = (float)st_fmad(dg, -1.977578, -0.1750748);
    s->gd2 = (float)st_fmad(dg, 0.9775785, -0.3797526);
}

/* the tone section from Bass and Treble (b, t: 0.2 .. 0.8) */
static void eq_set(struct state *s)
{
    float b = s->bass, t = s->treble, *n = s->n, *d = s->d;
    n[0] = (float)st_fmad(st_fmad((double)(t * t), -0.4129945, st_fmad((double)t, 0.9998067, 0.1397673)),
        (double)(b * b), st_fmad(st_fmad((double)(t * t), 0.4131879, (-0.1398195 - (double)t)), (double)b,
        ((((double)t * -0.006573567) + -0.0009072784) + ((double)(t * t) * 0.00266272))));
    n[1] = (float)st_fmad(st_fmad((double)t, -2.857529, st_fmad((double)(t * t), 1.182717, -0.2837266)),
        (double)(b * b), st_fmad(st_fmad((double)(t * t), -1.182523, st_fmad((double)t, 2.857336, 0.2836465)),
        (double)b, ((((double)t * 0.00619431) + 0.0001190611) + ((double)(t * t) * -0.002626376))));
    n[2] = (float)st_fmad(st_fmad((double)t, -1.716218, st_fmad((double)(t * t), 0.7130685, -0.02860672)),
        (double)b, st_fmad(st_fmad((double)(t * t), -0.7134551, st_fmad((double)t, 1.716605, 0.02865343)),
        (double)(b * b), st_fmad((double)(t * t), -0.005325439, st_fmad((double)t, 0.01314713, 0.001751428))));
    n[3] = (float)st_fmad(st_fmad((double)(t * t), -0.8259891, st_fmad((double)t, 1.99884, 0.2598635)),
        (double)(b * b), st_fmad(st_fmad((double)t, -1.998453, -0.2597646), (double)b, st_fmad(st_fmad((double)b,
        0.8256024, 0.005252752), (double)(t * t), st_fmad((double)t, -0.01238862, -0.0003015891))));
    n[4] = (float)st_fmad(st_fmad((double)t, -2.716412, st_fmad((double)(t * t), 1.12645, -0.1684208)),
        (double)(b * b), st_fmad(st_fmad((double)(t * t), -1.126256, st_fmad((double)t, 2.716218, 0.168419)),
        (double)b, ((((double)t * -0.006573567) + -0.0008448266) + (((double)(t * t) * 0.00266272)))));
    n[5] = (float)st_fmad(st_fmad((double)t, -0.8588824, st_fmad((double)(t * t), 0.3569209, -0.02388909)),
        (double)b, st_fmad(st_fmad((double)(t * t), -0.3567276, st_fmad((double)t, 0.8586891, 0.02386303)),
        (double)(b * b), ((((double)t * 0.00619431) + 0.0001818513) + ((double)(t * t) * -0.002626376))));
    d[0] = (float)st_fmad(st_fmad((double)(t * t), 4.137492, st_fmad((double)t, 1.625004, -7.240465)),
        (double)(b * b), st_fmad(st_fmad((double)t, -1.62691, st_fmad((double)(t * t), -4.135593, 7.239957)),
        (double)b, st_fmad((double)(t * t), -0.02861282, st_fmad((double)t, -0.00978783, 0.04854807))));
    d[1] = (float)st_fmad(st_fmad((double)(b * b), -7.652868, st_fmad((double)b, 7.654372, 0.02919639)), (double)t,
        st_fmad(st_fmad((double)(b * b), -19.14516, st_fmad((double)b, 19.14365, 0.07483193)), (double)(t * t),
        st_fmad((double)(b * b), 32.33884, st_fmad((double)b, -32.3387, -0.1218715))));
    d[2] = (float)st_fmad(st_fmad((double)(t * t), 35.31825, st_fmad((double)t, 14.42775, -57.65522)),
        (double)(b * b), st_fmad(st_fmad((double)t, -14.42393, st_fmad((double)(t * t), -35.32204, 57.65616)),
        (double)b, st_fmad((double)(t * t), -0.03607401, st_fmad((double)t, -0.01905039, 0.05363588))));
    d[3] = (float)st_fmad(st_fmad((double)t, -13.61452, st_fmad((double)(t * t), -32.45871, 51.28733)),
        (double)(b * b), st_fmad(st_fmad((double)t, 13.61151, st_fmad((double)(t * t), 32.46173, -51.28768)),
        (double)b, st_fmad((double)(t * t), -0.05636422, st_fmad((double)t, -0.01976672, 0.09330771))));
    d[4] = (float)st_fmad(st_fmad((double)(t * t), 14.85585, st_fmad((double)t, 6.43225, -22.76323)),
        (double)(b * b), st_fmad(st_fmad((double)t, -6.434156, st_fmad((double)(t * t), -14.85396, 22.76279)),
        (double)b, st_fmad((double)(t * t), 0.06468683, st_fmad((double)t, 0.02883822, -0.1021759))));
    d[5] = (float)st_fmad(st_fmad((double)t, -1.217615, st_fmad((double)(t * t), -2.707724, 4.032743)),
        (double)(b * b), st_fmad(st_fmad((double)t, 1.219119, st_fmad((double)(t * t), 2.706212, -4.032535)),
        (double)b, st_fmad((double)(t * t), -0.01846771, st_fmad((double)t, -0.009429666, 0.02857178))));
}

static float tone_knob(float k)
{
    return (float)st_fmad((double)(k * 0.01f), 0.6, 0.2);
}

/* the clipper on one point: the form the original uses for the previous and current points (a) or the two
   between (b) */
/*
 * The two clippers: e = e^(-1.4 |v|) (the original's e^(-+1.4 v), amp.h exp_negabs), k the side's factor from the
 * Drive (kp above zero, kn below, the block's: computed once), picked by v's sign bit (at +0 e = 1 and every term is
 * 0 with either k). clip_a on the current point and prev, clip_b on 1/3 and 2/3.
 */
static inline float clip_a(float v, float kp, float kn)
{
    float e = exp_negabs(v, 1.399999976158142f), k = pick_sign(v, kp, kn);
    return st_fma(st_fma(-e, 0.9800000190734863f, 0.9800000190734863f), k, v * 0.019999980926513672f);   /* VFMS */
}

static inline float clip_b(float v, float kp, float kn)
{
    float e = exp_negabs(v, 1.399999976158142f), k = pick_sign(v, kp, kn);
    return st_fma(k * 0.9800000190734863f, 1.0f - e, v * 0.019999980926513672f);
}

/* the stage over a block in place: prev is clipped by clip_a as the last sample's current point was (with the
   block's Drive: clipped again for the block's first sample) */
static void stage_block(struct stage *s, float *y, int n, float drive)
{
    float kp = (float)st_fmad((double)drive, -0.22, 1.35), kn = (float)st_fmad((double)drive, 0.1, -0.65);
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev, c = clip_a(prev, kp, kn);
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, *o = buf + 40 + 4 * i;
        o[0] = c;
        o[1] = clip_b(st_fma(d, 0.33333334f, prev), kp, kn);
        o[2] = clip_b(st_fma(d, 0.6666667f, prev), kp, kn);
        o[3] = c = clip_a(x, kp, kn);
        prev = x;
    }
    s->prev = prev;
    stage_block_fir_ord(s, buf, y, n, FIR_ORDER);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->volume = k[1] * 0.01f;
    s->drive = st_taper(st_pow0_5, k[0]);
    s->bass = tone_knob(k[2]);
    s->treble = tone_knob(k[3]);
    gain_set(s, s->drive);
    eq_set(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float bass = tone_knob(k[2]), drive = st_taper(st_pow0_5, k[0]), vol = k[1] * 0.01f, treble = tone_knob(k[3]);
    float *p = io->io, yv[WVFX_MAX_FRAMES], gv[WVFX_MAX_FRAMES], vl[WVFX_MAX_FRAMES];
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
        gain_set(s, drive);
    }
    for (int i = 0; i < nf; i++) {
        s->volume = st_glide(s->volume, vol);
        vl[i] = s->volume;
        float x = (float)((double)p[2 * i] * 1.1);
        float y = sos_run(&PF, &s->pf, x);
        float g = (st_fma(s->gn0, y, s->gn2 * s->gx2) - st_fma(s->gd2, s->gy2, s->gd1 * s->gy1)) / s->gd0;
        s->gx2 = s->gx1;
        s->gx1 = y;
        s->gy2 = s->gy1;
        s->gy1 = g;
        yv[i] = y;
        gv[i] = g;
    }
    stage_block(&s->st, gv, nf, s->drive);
    for (int i = 0; i < nf; i++) {
        if (s->bass != bass || s->treble != treble) {
            s->treble = st_glide(s->treble, treble);
            s->bass = bass;
            eq_set(s);
        }
        float f = (float)st_fmad((double)yv[i], 0.5, (double)gv[i]);
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[5], ex[5], st_fma(n[4], ex[4], st_fma(n[3], ex[3],
                          st_fma(n[0], f, st_fma(n[2], ex[2], n[1] * ex[1])))));
        float dd = st_fma(d[5], ey[5], st_fma(d[4], ey[4], st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1]))));
        float e = (nn - dd) / d[0];
        for (int j = 5; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = f;
        ey[1] = ey[0] = e;
        gv[i] = (float)(((double)vl[i] * 0.5) * (double)e);
    }
    for (int j = 0; j < 5; j++)
        sos_block(&POST[j], &s->post[j], gv, nf);
    p = io->io;
    for (int i = 0; i < nf; i++)
        p[2 * i] = p[2 * i + 1] = gv[i];
}

const struct wvfx_desc dyn_ac_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x105),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "AC-Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
