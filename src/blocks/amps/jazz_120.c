/*
 * Jazz 120 (AMP › Jazz): the original's Jazz 120 amp (QME-200 V1.2.2, library entry 4.10, id 0x07000014) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 *
 * Per sample:
 *   tone stack    3rd order from Bass (A25), Middle (B25) and Treble (B25 read backwards), in double: its
 *                 coefficients, its state and its output
 *   gain          Bright off: x 6 Gain (A25) in double; on: a 1st-order section from Gain on 4.5 x the tone stack
 *   output        x 4.54
 * All four knobs glide 0.0001 per sample; the coefficients stop one glide step short of them. Turning Bright clears
 * the Bright section's state. No clipping stage.
 */
#include "amp.h"

enum { K_GAIN, K_BASS, K_MIDDLE, K_TREBLE, K_BRIGHT };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOB("Gain", K_GAIN), KNOB("Bass", K_BASS), KNOB("Middle", K_MIDDLE), KNOB("Treble", K_TREBLE),
    { "Bright", 0.0f, 1.0f, 1.0f, 0.0f, K_BRIGHT, 0, 0, 0, "Off\0On\0", 0 },
};

/* the 3rd-order section in double: y = (b0 x + b1 x1 + b2 x2 + b3 x3 - (a1 y1 + a2 y2 + a3 y3)) / a0 */
struct tone3d {
    double b[4], a[4];
    double x[3], y[3];
};

struct state {
    float gain, bass, middle, treble;   /* glided */
    float bright;                       /* the switch the Bright section's state is for */
    struct tone3d tone;
    struct fos bright_c;
    float bright_a0;
    struct fos_state bs;
};

static float treble_taper(float knob)
{
    int i = (int)(knob * 2.55f);
    return st_b25[255 - (i < 0 ? 0 : i > 255 ? 255 : i)];
}

/*
 * The tone stack from Bass b, Middle m and Treble t (in double; mb = m b, tb = t b, M1 = 0.000977 m, A = 0.177 mb,
 * P = 0.995 tb, Q = 2.986 tb, T = 6.3e-22 t):
 *   n0 = ((b + 0.00551) + M1 + A) - 0.00549 t - P          n1 = (q1 - 0.01346) - 2.989 b + 0.01345 t + Q
 *   n2 = (2.981 b + 0.01041) + q2 - 0.01043 t - Q         n3 = (q3 - 0.00246) - 0.992 b + 0.00247 t + P
 *   d0 = (1.376 b + 0.00759) + M1 + A - T   d1 = (q1 - 0.0144) - 3.365 b + T   d2 = (2.605 b + 0.00834) + q2 + T
 *   d3 = (q3 - 0.00153) - 0.616 b - T
 * with q1 = -0.00198 m - 0.456 mb, q2 = 0.00125 m + 0.380 mb, q3 = -0.000252 m - 0.102 mb; the products by t and b
 * fused in the sums, in the order written.
 */
static void tone_set(struct tone3d *o, float bass, float middle, float treble)
{
    double b = bass, m = middle, t = treble, mb = m * b, tb = t * b;
    double M1 = m * 0.0009766042931005359, A = mb * 0.17699097096920013;
    double P = tb * 0.9951800107955933, Q = tb * 2.9855401515960693, T = t * 6.323562834288304e-22;
    double q1 = st_fmad(mb, -0.45564138889312744, m * -0.0019769142381846905);
    double q2 = st_fmad(mb, 0.3803098499774933, m * 0.001252598944120109);
    double q3 = st_fmad(mb, -0.10165943205356598, m * -0.0002522891154512763);
    o->b[0] = st_fmad(t, -0.00549272308126092, ((b + 0.005512002855539322) + M1) + A) - P;
    o->b[1] = st_fmad(t, 0.013451507315039635, st_fmad(b, -2.9885427951812744, q1 + -0.01346351858228445)) + Q;
    o->b[2] = st_fmad(t, -0.01043015718460083, st_fmad(b, 2.980720043182373, 0.010410877875983715) + q2) - Q;
    o->b[3] = st_fmad(t, 0.0024713727179914713, st_fmad(b, -0.9921773076057434, q3 + -0.0024593619164079428)) + P;
    o->a[0] = ((st_fmad(b, 1.3761073350906372, 0.007587861735373735) + M1) + A) - T;
    o->a[1] = st_fmad(b, -3.364650249481201, q1 + -0.014395514503121376) + T;
    o->a[2] = (st_fmad(b, 2.6046128273010254, 0.00833702553063631) + q2) + T;
    o->a[3] = st_fmad(b, -0.6160699129104614, q3 + -0.0015253585297614336) - T;
}

static double tone_run(struct tone3d *t, double x)
{
    double n = t->b[0] * x;
    n = st_fmad(t->b[1], t->x[0], n);
    n = st_fmad(t->b[2], t->x[1], n);
    n = st_fmad(t->b[3], t->x[2], n);
    double d = t->a[1] * t->y[0];
    d = st_fmad(t->a[2], t->y[1], d);
    d = st_fmad(t->a[3], t->y[2], d);
    double y = (n - d) / t->a[0];
    t->x[2] = t->x[1]; t->x[1] = t->x[0]; t->x[0] = x;
    t->y[2] = t->y[1]; t->y[1] = t->y[0]; t->y[0] = y;
    return y;
}

/* the Bright section from the glided Gain g: (g^2 - 1.0515 g, 0.9485 g - g^2) / (-0.0515 - g + g^2, g - 0.0515 - g^2) */
static void bright_set(struct fos *c, float *a0, float g)
{
    float g2 = g * g;
    c->b0 = st_fma(g, -1.051535725593567f, g2);
    c->b1 = st_fma(g, 0.9484642148017883f, -g2);
    *a0 = (-0.05153576657176018f - g) + g2;
    c->a1 = (g + -0.05153576657176018f) + -g2;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[K_GAIN]);
    s->bass = st_taper(st_a25, k[K_BASS]);
    s->middle = st_taper(st_b25, k[K_MIDDLE]);
    s->treble = treble_taper(k[K_TREBLE]);
    s->bright = k[K_BRIGHT];
    tone_set(&s->tone, s->bass, s->middle, s->treble);
    bright_set(&s->bright_c, &s->bright_a0, s->gain);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float bright = io->knobs[K_BRIGHT];
    if (bright != s->bright) {
        s->bs = (struct fos_state){ 0 };
        s->bright = bright;
    }
    float gain = st_taper(st_a25, io->knobs[K_GAIN]), bass = st_taper(st_a25, io->knobs[K_BASS]);
    float middle = st_taper(st_b25, io->knobs[K_MIDDLE]), treble = treble_taper(io->knobs[K_TREBLE]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->gain = st_glide(s->gain, gain);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (!io->enabled)
            continue;
        if (s->gain != gain)
            bright_set(&s->bright_c, &s->bright_a0, s->gain);
        if (s->bass != bass || s->middle != middle || s->treble != treble)
            tone_set(&s->tone, s->bass, s->middle, s->treble);
        double y = tone_run(&s->tone, p[0]);
        float x = bright == 0.0f ? (float)((double)s->gain * 6.0 * y)
                                 : fos_run_div(&s->bright_c, s->bright_a0, &s->bs, (float)(y * 4.5));
        x = x * 4.539999961853027f;
        p[0] = x;
        p[1] = x;
    }
}

const struct wvfx_desc jazz_120_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x14),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Jazz 120",
    .category = "Jazz",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
