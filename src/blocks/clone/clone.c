/*
 * Clone (CLONE): the original's Clone (QME-200 V1.2.2, library entry 11.0, id 0x0F000000, ToneCloneInit /
 * ToneCloneProcess) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). It plays a capture of
 * the original's own format (the PC editor fits it to a .nam: research/NOTES.md "Clone"), a Wiener-Hammerstein
 * model run per 64-sample block on the left input:
 *
 *   gain -> IIR 1 -> convolution with IR 1 -> saturation (4x oversampled) -> IIR 2 -> convolution with IR 2
 *        -> Bass, Middle, Treble (three biquads, the last x Volume)
 *
 * and both outputs mixed from the input to it by a bypass ramp. The capture (the item's data, 0x2000 bytes): +0x12,
 * +0x13 the original wants 0; +0x18 the model: 10 doubles (the two IIRs' b0 b1 b2 a1 a2) and the saturation's
 * p50, p54, p58, p5c; +0x78 / +0x7c IR 1's offset (floats from +0x88) and length; +0x80 / +0x84 IR 2's (its length
 * kept to 1024). Without +0x7c and +0x84 the block plays silence, as the original does.
 *
 * The parts as the module computes them:
 *  - IIRs: direct form II in double, x * 1000 in, (float)((double)(float)y * 0.001) out;
 *  - convolutions: uniformly partitioned (64), a 128-point real FFT of the last two blocks spread to the full
 *    spectrum, products with the partitions (CMSIS's complex FFT of each 64 samples in the real parts) summed over a
 *    delay line, the inverse 128-point FFT, its second half's real parts;
 *  - saturation: two half-band all-pass interpolators (orders 7, 5) to 4x, y = A - e^(k x) A (x > 0: A = p50,
 *    k = -p58; else A = -p54, k = p5c; armcc's expf), two decimators (orders 4, 6);
 *  - Gain, Volume: 2^(0.0797 knob - 3.986), gliding 0.05 per block; Bass, Middle, Treble: (knob - 50) 0.12 dB,
 *    gliding 0.1 per block while more than 0.01 away, their coefficients recomputed then (armcc's exp2f);
 *  - bypass: a mix gliding 0.01 per sample to 1 (on) or 0 (off); the model keeps running bypassed.
 * The pipeline runs only on whole 64-frame blocks (the original's). Not kept: a capture with +0x12 / +0x13 not 0
 * (the original plays it with an empty model, writing below its delay lines every block: here silence), more than
 * 16 partitions in IR 1 (the original writes past its room: here the first 16), reads past the item's 0x2000 bytes
 * (the original reads what follows in the flash: here zeros).
 */
#include "clone_fft.h"

#define PARTS   16                      /* partitions of each convolution (the original's room) */
#define BLOCK   64

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
};

struct conv {
    float prev[BLOCK];                  /* the last block */
    float fdl[PARTS][256];              /* the spectra of the last blocks */
    float ir[PARTS][256];               /* the partitions' spectra */
    int32_t parts, idx;
};

struct biquad {
    float b0, b1, b2, a1, a2, s1, s2;
};

struct state {
    float gain_t, gain, vol_t, vol;
    float bass_t, bass, mid_t, mid, treble_t, treble;   /* dB */
    float on, mix;
    struct biquad tone[3];
    uint8_t valid;
    double iir[2][5];                   /* b0 b1 b2 a1 a2 */
    double is[2][2];                    /* w[n-1], w[n-2] */
    float p50, p54, p58, p5c;
    float up1[7], up2[5], dn6[6], dn4[4];
    float last6, last4;
    struct conv c1, c2;
};

/* ---------------------------------------------------------------- the capture */

static uint32_t rd32(const uint8_t *d, uint32_t size, uint32_t at)
{
    uint32_t v = 0;
    if (at + 4 <= size)
        __builtin_memcpy(&v, d + at, 4);
    return v;
}

/* sample i of the IR at float offset `off` from +0x88 (0 past the data) */
static float ir_at(const uint8_t *d, uint32_t size, int32_t off, uint32_t i)
{
    int64_t at = 0x88 + 4 * ((int64_t)off + i);
    float v = 0.0f;
    if (at >= 0 && at + 4 <= size)
        __builtin_memcpy(&v, d + at, 4);
    return v;
}

/* ---------------------------------------------------------------- the knobs */

/* towards t by step (the original's compares: equal stays; down to t when the step would pass it) */
static float glide(float v, float t, float step)
{
    if (v == t)
        return t;
    if (t <= v) {
        float n = v + -step;
        return n <= t ? t : n;
    }
    float n = v + step;
    return t <= n ? t : n;
}

static float level_of(float k)
{
    return st_exp2f(st_fma(k, 0.07972627133131027f, -3.986313819885254f));
}

static float db_of(float k)
{
    return st_fma(k, 0.11999999731779099f, -6.0f);
}

/* Bass and Middle: a shelf from A = 2^(dB 0.083) (c, k: the section's constants) */
static void shelf_set(struct biquad *b, float db, float c, float k)
{
    float a = st_exp2f(db * 0.08304820209741592f);
    float q = c / a, m = a * c, d = q + 1.0f;
    b->b0 = (m + 1.0f) / d;
    b->b2 = (1.0f - m) / d;
    b->b1 = k / d;
    b->a1 = b->b1;
    b->a2 = (1.0f - q) / d;
}

static void treble_set(struct biquad *b, float db)
{
    float a = st_exp2f(db * 0.08304820209741592f);
    float sq = st_sqrt(a) * 0.3196493685245514f, am = a + -1.0f, ap = a + 1.0f;
    float u = am * 0.9741291403770447f, v = ap * 0.9741291403770447f;
    float e = ap - u, f = u + ap, g = v + am;
    float r = 1.0f / (e + sq), h = r * a;
    b->b0 = h * (f + sq);
    b->b1 = (g * -2.0f) * h;
    b->b2 = h * (f - sq);
    b->a1 = (am - v) * (r + r);
    b->a2 = r * (e - sq);
}

static float biquad_run(struct biquad *b, float x)
{
    float acc = st_fma(b->s2, b->a2, b->a1 * b->s1);
    float bb = st_fma(b->b1, b->s1, b->b2 * b->s2);
    float w = x - acc;
    float y = st_fma(b->b0, w, bb);
    b->s2 = b->s1;
    b->s1 = w;
    return y;
}

/* ---------------------------------------------------------------- the model */

static void iir_run(const double *c, double *s, float *w)
{
    double s1 = s[0], s2 = s[1];
    for (int i = 0; i < BLOCK; i++) {
        double acc = st_fmad(s2, c[4], s1 * c[3]);
        double v = st_fmad((double)w[i], 1000.0, -acc);              /* VFNMS: x 1000 - acc */
        double y = st_fmad(c[0], v, st_fmad(c[1], s1, c[2] * s2));
        w[i] = (float)((double)(float)y * 0.001);
        s2 = s1;
        s1 = v;
    }
    s[0] = s1;
    s[1] = s2;
}

static void conv_init(struct conv *c, const uint8_t *d, uint32_t size, int32_t off, uint32_t len)
{
    int parts = (int)__builtin_ceilf((float)(int32_t)len * 0.015625f);         /* VCVTP */
    c->parts = parts < 0 ? 0 : parts > PARTS ? PARTS : parts;
    for (int p = 0; p < c->parts; p++) {
        float *buf = c->ir[p];
        __builtin_memset(buf, 0, 256 * sizeof(float));
        int32_t n = p == parts - 1 ? (int32_t)len - 64 * p : 64;
        for (int32_t j = 0; j < n && j < 128; j++)
            buf[2 * j] = ir_at(d, size, off, (uint32_t)(64 * p + j));
        clone_cfft128(buf, 0);
    }
}

static void conv_run(struct conv *c, float *w)
{
    float in[128], spec[256], acc[256];
    if (c->parts == 0) {
        /* a capture the original takes but does not set up: its index then walks down through the memory below
           the delay line, writing a spectrum there every block; here the silence it outputs, nothing written */
        __builtin_memset(w, 0, BLOCK * sizeof(float));
        return;
    }
    __builtin_memcpy(in, c->prev, sizeof c->prev);
    __builtin_memcpy(in + BLOCK, w, BLOCK * sizeof(float));
    __builtin_memcpy(c->prev, w, BLOCK * sizeof(float));
    __builtin_memset(spec, 0, sizeof spec);
    clone_rfft128(in, spec);
    spec[128] = spec[1];                                /* the Nyquist bin */
    spec[129] = 0.0f;
    spec[1] = 0.0f;
    for (int k = 65; k < 128; k++) {                    /* the rest by symmetry */
        spec[2 * k] = spec[256 - 2 * k];
        spec[2 * k + 1] = -spec[257 - 2 * k];
    }
    __builtin_memset(acc, 0, sizeof acc);
    __builtin_memcpy(c->fdl[c->idx], spec, sizeof spec);
    int slot = c->idx;
    for (int p = 0; p < c->parts; p++) {
        const float *a = c->fdl[slot], *b = c->ir[p];
        for (int i = 0; i < 128; i++) {
            float ar = a[2 * i], ai = a[2 * i + 1], br = b[2 * i], bi = b[2 * i + 1];
            acc[2 * i] = acc[2 * i] + st_fma(br, ar, -(bi * ai));
            acc[2 * i + 1] = acc[2 * i + 1] + st_fma(bi, ar, br * ai);
        }
        slot = slot + 1 >= c->parts ? slot + 1 - c->parts : slot + 1;
    }
    if (c->idx < 1)
        c->idx += c->parts;
    c->idx -= 1;
    clone_cfft128(acc, 1);
    for (int i = 0; i < BLOCK; i++)
        w[i] = acc[128 + 2 * i];
}

/* one all-pass section of a half-band branch: y = s + c x, s = x - y c */
static inline float allpass(float c, float *s, float x)
{
    float y = st_fma(c, x, *s);
    *s = st_fma(-y, c, x);
    return y;
}

/* the interpolator of `order` (branch A: the first order - order / 2 sections, branch B: the rest): x -> 2 */
static void up2x(const float *c, float *s, int order, float x, float *out)
{
    int na = order - order / 2;
    float a = x, b = x;
    for (int i = 0; i < na; i++)
        a = allpass(c[i], &s[i], a);
    for (int i = na; i < order; i++)
        b = allpass(c[i], &s[i], b);
    out[0] = a;
    out[1] = b;
}

/* the decimator: n outputs from 2n inputs, branch B a sample late (*last) */
static void down2x(const float *c, float *s, float *last, int order, const float *in, float *out, int n)
{
    int na = order - order / 2;
    for (int k = 0; k < n; k++) {
        float a = in[2 * k], b = in[2 * k + 1];
        for (int i = 0; i < na; i++)
            a = allpass(c[i], &s[i], a);
        for (int i = na; i < order; i++)
            b = allpass(c[i], &s[i], b);
        out[k] = (*last + a) * 0.5f;
        *last = b;
    }
}

static void saturate(struct state *s, float *w)
{
    float os[4 * BLOCK];
    for (int i = 0; i < BLOCK; i++) {
        float two[2];
        up2x(CLONE_UP_IIR, s->up1, 7, w[i], two);
        up2x(CLONE_UP_IIR + 7, s->up2, 5, two[0], os + 4 * i);
        up2x(CLONE_UP_IIR + 7, s->up2, 5, two[1], os + 4 * i + 2);
    }
    float pa = s->p50, na = -s->p54, pk = -s->p58, nk = s->p5c;
    for (int i = 0; i < 4 * BLOCK; i++) {
        float x = os[i], a = x > 0.0f ? pa : na, k = x > 0.0f ? pk : nk;
        os[i] = st_fma(-st_expf(k * x), a, a);
    }
    for (int i = 0; i < BLOCK; i++) {
        float two[2];
        down2x(CLONE_DOWN_IIR + 6, s->dn4, &s->last4, 4, os + 4 * i, two, 2);
        down2x(CLONE_DOWN_IIR, s->dn6, &s->last6, 6, two, &w[i], 1);
    }
}

/* ---------------------------------------------------------------- the block */

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    __builtin_memset(s, 0, sizeof *s);
    s->gain = level_of(k[0]);
    s->vol = level_of(k[1]);
    s->bass = db_of(k[2]);
    s->mid = db_of(k[3]);
    s->treble = db_of(k[4]);
    s->mix = 1.0f;
    shelf_set(&s->tone[0], s->bass, 0.012594442814588547f, -1.9996827840805054f);
    shelf_set(&s->tone[1], s->mid, 0.08043426275253296f, -1.9870225191116333f);
    treble_set(&s->tone[2], s->treble);
    const uint8_t *d = env->data;
    uint32_t size = d ? env->data_size : 0;
    uint32_t len1 = rd32(d, size, 0x7c), len2 = rd32(d, size, 0x84);
    if (!len1 || !len2)
        return 0;                                       /* no capture: silence */
    s->valid = 1;
    if (size < 0x88 || d[0x12] || d[0x13])
        return 0;                                       /* the original plays its empty model: silence too */
    __builtin_memcpy(s->iir, d + 0x18, sizeof s->iir);
    __builtin_memcpy(&s->p50, d + 0x18 + 0x50, 4 * sizeof(float));
    conv_init(&s->c1, d, size, (int32_t)rd32(d, size, 0x78), len1);
    conv_init(&s->c2, d, size, (int32_t)rd32(d, size, 0x80), len2 > 1024 ? 1024 : len2);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    s->gain_t = level_of(k[0]);
    s->vol_t = level_of(k[1]);
    s->bass_t = db_of(k[2]);
    s->mid_t = db_of(k[3]);
    s->treble_t = db_of(k[4]);
    s->on = (float)io->enabled;
    s->gain = glide(s->gain, s->gain_t, 0.05000000074505806f);
    s->vol = glide(s->vol, s->vol_t, 0.05000000074505806f);
    if (st_abs(s->bass_t - s->bass) > 0.009999999776482582f) {
        s->bass = glide(s->bass, s->bass_t, 0.10000000149011612f);
        shelf_set(&s->tone[0], s->bass, 0.012594442814588547f, -1.9996827840805054f);
    }
    if (st_abs(s->mid_t - s->mid) > 0.009999999776482582f) {
        s->mid = glide(s->mid, s->mid_t, 0.10000000149011612f);
        shelf_set(&s->tone[1], s->mid, 0.08043426275253296f, -1.9870225191116333f);
    }
    if (st_abs(s->treble_t - s->treble) > 0.009999999776482582f) {
        s->treble = glide(s->treble, s->treble_t, 0.10000000149011612f);
        treble_set(&s->tone[2], s->treble);
    }
    int n = io->frames < BLOCK ? io->frames : BLOCK;
    float w[BLOCK];
    float *p = io->io;
    if (!s->valid) {
        __builtin_memset(w, 0, sizeof w);
    } else {
        if (n <= 0)
            return;
        for (int i = 0; i < n; i++)
            w[i] = p[2 * i] * s->gain;
        if (n == BLOCK) {
            iir_run(s->iir[0], s->is[0], w);
            conv_run(&s->c1, w);
            saturate(s, w);
            iir_run(s->iir[1], s->is[1], w);
            conv_run(&s->c2, w);
        }
        for (int i = 0; i < n; i++)
            w[i] = biquad_run(&s->tone[0], w[i]);
        for (int i = 0; i < n; i++)
            w[i] = biquad_run(&s->tone[1], w[i]);
        for (int i = 0; i < n; i++)
            w[i] = s->vol * biquad_run(&s->tone[2], w[i]);
    }
    for (int i = 0; i < n; i++, p += 2) {
        s->mix = glide(s->mix, s->on, 0.009999999776482582f);
        p[0] = st_fma(w[i] - p[0], s->mix, p[0]);
        p[1] = st_fma(w[i] - p[1], s->mix, p[1]);
    }
}

const struct wvfx_desc clone_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_CLONE, 0x100),
    .menu = WVFX_CLONE,
    .nparams = sizeof params / sizeof params[0],
    .name = "Clone",
    .category = "Clone",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
