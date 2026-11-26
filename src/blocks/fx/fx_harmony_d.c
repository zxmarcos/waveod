/*
 * Harmony D (FX): the original's Harmony D (QME-200 V1.2.2, library entry 1.4, id 0x0100004E; its code calls it the
 * Harmonizer2) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A two-voice harmonizer that
 * follows the played note in a key: the wet voices from the left input, the dry from both. Single precision.
 *
 *   pitch     the left input through a 3rd-order low pass, every 4th sample into a 282-sample ring; every 4 calls
 *             one step of the estimate: the ring (oldest first, zero-padded to 512) through the 512-point real FFT
 *             (fft512.h, CMSIS-DSP's), its power (DC and Nyquist cleared), back through the inverse: the
 *             autocorrelation; its largest peak at lags 8 .. 157 (larger than both neighbours), refined to the
 *             largest of the cubic segments on either side (Akima slopes) where their slope is 0, is the period
 *             at 11025 Hz;
 *   note      v = offset(Mode) - Key + 12 log2(f / 32.70) semitones (f = 11025 / period); without Smooth its nearest
 *             step (v mod 12, rounded) picks each voice's shift from the scale table (harmonic minor for Mode 2,
 *             major otherwise) by Interval 1 and 2; with Smooth the shift moves between v's step and the next by
 *             v's fraction;
 *   voices    two shifters (a 2210-sample line read by two taps half a line apart, each moving 1 - r per sample
 *             (r = 2^(shift / 12)), weighted by sin(pi d / 2210), then a 2nd-order low pass);
 *   out       Mix below 0.5: x + 2 Mix (v1 + v2) each channel; from 0.5: (v1 + v2) + (2 - 2 Mix) x.
 * Mix glides 0.001 per sample, by block, bypassed too. Knob word 5 is unused. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "fft512.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Key", 0.0f, 11.0f, 1.0f, 0.0f, 1, 0x5a, 0x00, 0, 0, 0 },
    { "Mode", 0.0f, 7.0f, 1.0f, 0.0f, 2, 0x9b, 0x00, 0, 0, 0 },
    { "Interval 1", 0.0f, 13.0f, 1.0f, 8.0f, 3, 0x5c, 0x00, 0, 0, 0 },
    { "Interval 2", 0.0f, 13.0f, 1.0f, 10.0f, 4, 0x5c, 0x00, 0, 0, 0 },
    { "Smooth", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

#define RING 282                        /* the decimated input kept */
#define LAGS 159                        /* the autocorrelation kept (0 .. 158) */
#define LINE 2210                       /* a shifter's line */

/* the semitone offset of each Mode's key */
static const int MODE_OFFSET[8] = { 0, 9, 0, 2, 4, 5, 7, 11 };

/* the shifts by interval (rows, 0 .. 13) and the note's step in the key (columns, 0 .. 11) */
static const int32_t HARMONIC_MINOR[14][12] = {
    { -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12 },
    { -10, -10, -11, -10, -10, -10, -10, -11, -9, -9, -11, -11 },
    { -9, -9, -9, -8, -8, -9, -9, -8, -8, -8, -9, -9 },
    { -7, -7, -7, -7, -7, -6, -6, -7, -6, -6, -6, -8 },
    { -5, -5, -6, -4, -4, -5, -5, -5, -5, -5, -5, -6 },
    { -4, -4, -3, -3, -3, -3, -3, -4, -3, -3, -4, -4 },
    { -1, -1, -2, -1, -1, -2, -2, -2, -1, -1, -3, -3 },
    { 2, 2, 1, 2, 2, 2, 2, 1, 3, 3, 1, 1 },
    { 3, 3, 3, 4, 4, 3, 3, 4, 4, 4, 3, 3 },
    { 5, 5, 5, 5, 5, 6, 6, 5, 6, 6, 4, 4 },
    { 7, 7, 6, 8, 8, 7, 7, 7, 7, 7, 7, 6 },
    { 8, 8, 9, 9, 9, 9, 9, 8, 9, 9, 8, 8 },
    { 11, 10, 10, 11, 11, 10, 10, 10, 11, 11, 9, 9 },
    { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 },
};
static const int32_t MAJOR[14][12] = {
    { -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12, -12 },
    { -10, -10, -10, -10, -11, -10, -10, -10, -10, -10, -10, -11 },
    { -8, -8, -9, -9, -9, -8, -8, -8, -8, -9, -9, -9 },
    { -7, -7, -7, -7, -7, -6, -7, -7, -7, -7, -7, -7 },
    { -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -6 },
    { -3, -3, -3, -3, -4, -3, -3, -3, -3, -4, -4, -4 },
    { -1, -1, -2, -2, -2, -1, -1, -2, -2, -2, -2, -2 },
    { 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1 },
    { 4, 4, 3, 3, 3, 4, 4, 4, 4, 3, 3, 3 },
    { 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5 },
    { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6 },
    { 9, 9, 9, 9, 8, 9, 9, 9, 9, 8, 8, 8 },
    { 11, 11, 10, 10, 10, 11, 11, 10, 10, 10, 10, 10 },
    { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 },
};

static const struct sos VOICE_LP = {
    0.6631820201873779f, 1.3263640403747559f, 0.6631820201873779f, 1.0f, 1.2095284461975098f, 0.44319963455200195f,
};

struct voice {
    float line[LINE];
    int w;
    float d0, d1;                       /* the taps' delays */
    struct sos_state lp;
};

struct mem {                            /* in the block's buffer */
    float ring[RING];
    float lag[LAGS + 5];                /* the autocorrelation (the peak search reads 3 past it: zeros) */
    float spec[512];                    /* the spectrum between the steps */
    float work[512];                    /* the FFT's input / output */
    struct voice v[2];
};

struct state {
    float mix, period;
    int step, down, wp;
    float x1, x2, x3, y1, y2, y3;       /* the low pass */
    struct mem *m;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    (void)k;
    *s = (struct state){ .mix = 0.5f, .m = buffer };
    __builtin_memset(s->m, 0, sizeof *s->m);
    s->m->v[0].d1 = s->m->v[1].d1 = 1105.0f;
    return 0;
}

/* x mod 12 for finite x >= 0, exact (as any correct fmodf) */
static float fmod12(float x)
{
    float q = __builtin_truncf(x / 12.0f), r = x - q * 12.0f;
    if (r < 0.0f)
        r = r + 12.0f;
    else if (r >= 12.0f)
        r = r - 12.0f;
    return r;
}

/* the Akima slope between the differences b, c from its neighbours a, d */
static float slope(float a, float b, float c, float d)
{
    if (a == b && c == d)
        return (b + c) * 0.5f;
    float wc = st_abs(d - c), wb = st_abs(b - a);
    return st_fma(wb, c, wc * b) / (wb + wc);
}

/* the roots of 3 a3 t^2 + 2 a2 t + m = 0 (both 0 when there are none) */
static void roots(float m, float a2, float a3, float *t1, float *t2)
{
    float b = a2 + a2, disc = st_fma(b, b, (m * -12.0f) * a3);
    *t1 = *t2 = 0.0f;
    if (disc >= 0.0f) {
        float q = st_sqrt(disc), d = a3 * 3.0f;
        *t1 = ((q - b) * 0.5f) / d;
        *t2 = ((q + b) * -0.5f) / d;
    }
}

/* the period from the autocorrelation: the peak refined to the cubic segments' maxima */
static float pick(const float *a)
{
    int best = 0x9f;
    for (int i = 8; i < 0x9e; i++)
        if (a[i] > a[i - 1] && a[i] > a[i + 1] && a[i] > a[best])
            best = i;
    const float *c = a + best;
    float d0 = c[-2] - c[-3], d1 = c[-1] - c[-2], d2 = c[0] - c[-1];
    float d3 = c[1] - c[0], d4 = c[2] - c[1], d5 = c[3] - c[2];
    float m1 = slope(d0, d1, d2, d3), m2 = slope(d1, d2, d3, d4), m3 = slope(d2, d3, d4, d5);
    /* the right segment [best, best + 1]: m2 + 2 a2 t + 3 a3 t^2, the left one [best - 1, best] */
    float a3 = m3 + st_fma(-d3, 2.0f, m2), a2 = st_fma(d3, 3.0f, -st_fma(m2, 2.0f, m3));
    float b3 = m2 + st_fma(-d2, 2.0f, m1), b2 = st_fma(d2, 3.0f, -st_fma(m1, 2.0f, m2));
    float t1, t2, t3, t4;
    roots(m2, a2, a3, &t1, &t2);
    roots(m1, b2, b3, &t3, &t4);
    /* the largest value above 0 at a stationary point inside a segment: its position */
    float fb = (float)best, fl = fb + -1.0f, top = 0.0f, p = 0.0f, val;
#define IN01(u) ((u) >= 0.0f && (u) <= 1.0f)
#define RIGHT(u) st_fma(st_fma(st_fma(u, a3, a2), u, m2), u, c[0])
#define LEFT(u) st_fma(st_fma(st_fma(u, b3, b2), u, m1), u, c[-1])
    if (IN01(t1) && (val = RIGHT(t1)) > top) {
        p = t1 + fb;
        top = val;
    }
    if (IN01(t2) && (val = RIGHT(t2)) > top) {
        p = t2 + fb;
        top = val;
    }
    if (IN01(t3) && (val = LEFT(t3)) > top) {
        p = t3 + fl;
        top = val;
    }
    if (IN01(t4) && (val = LEFT(t4)) > top)
        p = t4 + fl;
#undef IN01
#undef RIGHT
#undef LEFT
    return p != 0.0f ? p : fb;
}

/* one step of the estimate per call */
static void estimate(struct state *s)
{
    struct mem *m = s->m;
    switch (s->step) {
    case 0:
        for (int i = 0; i < RING; i++)
            m->work[i] = m->ring[(s->wp + i) % RING];
        for (int i = RING; i < 512; i++)
            m->work[i] = 0.0f;
        fft512_forward(m->work, m->spec);
        s->step = 1;
        break;
    case 1:
        m->spec[0] = m->spec[1] = 0.0f;
        for (int i = 1; i < 256; i++) {
            float re = m->spec[2 * i], im = m->spec[2 * i + 1];
            m->spec[2 * i] = st_fma(im, im, re * re);
            m->spec[2 * i + 1] = 0.0f;
        }
        s->step = 2;
        break;
    case 2:
        fft512_inverse(m->spec, m->work);
        __builtin_memcpy(m->lag, m->work, LAGS * sizeof(float));
        s->step = 3;
        break;
    default:
        s->period = pick(m->lag);
        s->step = 0;
        break;
    }
}

static float voice_run(struct voice *v, float x, float ratio)
{
    float step = 1.0f - ratio;
    v->line[v->w] = x;
    int w = v->w > LINE - 2 ? 0 : v->w + 1;
    v->w = w;
    float d[2] = { v->d0 + step, v->d1 + step }, t[2];
    for (int j = 0; j < 2; j++) {
        if (d[j] > 2210.0f)
            d[j] = d[j] + -2210.0f;
        else if (d[j] < 0.0f)
            d[j] = d[j] + 2210.0f;
        int r = w - (int)d[j];
        if (r < 0)
            r += LINE;
        float a = v->line[r], b = v->line[r < 1 ? r + LINE - 1 : r - 1];
        t[j] = st_fma(d[j] - __builtin_truncf(d[j]), b - a, a);
    }
    v->d0 = d[0];
    v->d1 = d[1];
    float y = t[0] * st_sinf(d[0] * 0.0014215351548045874f);
    y = st_fma(t[1], st_sinf(d[1] * 0.0014215351548045874f), y);
    return sos_run(&VOICE_LP, &v->lp, y);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    struct mem *m = s->m;
    int key = (int)k[1], mode = (int)k[2], iv0 = (int)k[3], iv1 = (int)k[4], smooth = (int)k[6];
    s->mix = st_glide_by(s->mix, k[0] * 0.01f, (float)io->frames * 0.0010000000474974513f);
    if (!io->enabled)
        return;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++) {
        float x0 = p[2 * i];
        float y = (s->x2 + s->x1) * 0.001043535303324461f;
        y = st_fma(s->y1, 2.774888038635254f, y);
        y = st_fma(s->x3 + x0, 0.0007334143156185746f, y);
        y = st_fma(s->y2, -2.5997326374053955f, y);
        y = st_fma(s->y3, 0.8212907314300537f, y);
        s->x3 = s->x2;
        s->x2 = s->x1;
        s->x1 = x0;
        s->y3 = s->y2;
        s->y2 = s->y1;
        s->y1 = y;
        if (s->down == 3) {
            m->ring[s->wp] = y;
            s->wp = s->wp + 1 >= RING ? 0 : s->wp + 1;
            s->down = 0;
        } else {
            s->down++;
        }
    }
    estimate(s);
    /* the voices' shifts */
    float sh1 = 0.0f, sh2 = 0.0f;
    float f = s->period != 0.0f ? 11025.0f / s->period : 0.0f;
    if (f != 0.0f) {
        float l = st_log10f(f * 0.030578050762414932f);
        float v = (float)MODE_OFFSET[mode] - st_fma(l, -39.863136291503906f, (float)key);
        const int32_t (*tab)[12] = mode == 2 ? HARMONIC_MINOR : MAJOR;
        if (smooth == 0) {
            float r = fmod12(v);
            int n = (int)((r < 0.0f ? -0.5f : 0.5f) + r);
            if (n == 12)
                n = 0;
            sh1 = (float)tab[iv0][n];
            sh2 = (float)tab[iv1][n];
        } else {
            int n = (int)v % 12, nx = (n + 1) % 12;
            float fr = v - __builtin_truncf(v);
            float a = (float)tab[iv0][n];
            sh1 = st_fma((float)tab[iv0][nx] - a, fr, a);
            float b = (float)tab[iv1][n];
            sh2 = st_fma((float)tab[iv1][nx] - b, fr, b);
        }
    }
    float r1 = st_exp2f(sh1 * 0.0833333358168602f), r2 = st_exp2f(sh2 * 0.0833333358168602f);
    if (s->mix >= 0.5f) {
        float gr = st_fma(-s->mix, 2.0f, 2.0f), gl = 1.0f - s->mix;
        for (int i = 0; i < io->frames; i++, p += 2) {
            float a = voice_run(&m->v[0], p[0], r1);
            float w = voice_run(&m->v[1], p[0], r2) + a;
            p[1] = st_fma(gr, p[1], w);
            p[0] = st_fma(p[0] + p[0], gl, w);
        }
    } else {
        float g = s->mix + s->mix;
        for (int i = 0; i < io->frames; i++, p += 2) {
            float a = voice_run(&m->v[0], p[0], r1);
            float w = (voice_run(&m->v[1], p[0], r2) + a) * g;
            p[0] = w + p[0];
            p[1] = p[1] + w;
        }
    }
}

const struct wvfx_desc fx_harmony_d_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x104),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Harmony D",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = sizeof(struct mem),
    .init = init,
    .process = process,
    .poll = 0,
};
