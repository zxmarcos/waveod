/*
 * Pitch S (FX): the original's Pitch S (QME-200 V1.2.2, library entry 1.5, id 0x01000055; its code calls it the
 * ClassPS) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A pitch shifter that follows the
 * signal's period, mono (the left input; both outputs the same). Single precision (two compares in double).
 *
 * The shift s = lo + Position (hi - lo) semitones (Range 0-5: 0 .. -24, 0 .. -12, 0 .. 12, 0 .. 24, -12 .. 12,
 * -24 .. 24), the read speed r = 2^(s / 12). x through a fixed high pass and a low pass at 0.4975 fs / r (at most
 * 20 kHz; recomputed when r changes) into a 4000-sample buffer; 220 samples behind the write position (the latency):
 *   peaks and zero crossings: a local extreme of 7 samples there, larger than 0.3 (after an onset 0.05) of the
 *     buffer's peaks in front of and behind it (searched once per block), marks the next zero crossing; each zero
 *     crossing's position (interpolated) is kept, at most 50;
 *   the period: every 64 samples, among the zero crossings 2 (4 after an onset when reading faster) back from the
 *     marked one, every second one 30 or more samples before it (within 4.5 periods, at least 0.4 period apart
 *     after long periods), the one whose 110 samples (every 4th over 440) differ least from the marked one's
 *     (x 0.8 within 2 % of the period) gives the period, unless it is 2, 3 or 4 times the last one (outside the
 *     4410 samples after an onset); a weight, the mean of the last three periods within 3 %, is the jump;
 *   the read: r per sample; reading faster it jumps back a weight when it comes within 220 samples of the write
 *     position, reading slower it jumps forward a weight (repeatedly) when it falls more than a weight + 20
 *     behind; each jump crossfades the old and new reads over half a Hann window (256 r samples at most);
 *   onsets: an envelope of |y| (attack 0.9931, release 0.0025) against its value 100 samples before (3.2 x and
 *     above 0.01, compared in double: the next jump's reference; 1.6 x and above 0.001: 4410 samples unlocked).
 * The buffer moves 256 back when it fills. Out: x + Mix (Level y - x). Level and Mix glide 0.001 per sample, by
 * block, bypassed too. Bypassed it passes the signal.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Range", 0.0f, 5.0f, 1.0f, 2.0f, 0, 0x85, 0x00, 0, 0, 0 },
    { "Position", 0.0f, 100.0f, 1.0f, 0.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Mix", 0.0f, 100.0f, 1.0f, 100.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Level", 0.0f, 100.0f, 1.0f, 100.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

#define BUF 4000                        /* the buffer */
#define LAT 220                         /* the latency: the analysis point behind the write position */
#define HOP 256                         /* the move back when the buffer fills */
#define ZN 50                           /* zero crossings kept */
#define EN 100                          /* the onset envelope's history */

static const int RANGE_LO[6] = { 0, 0, 0, 0, -12, -24 }, RANGE_HI[6] = { -24, -12, 12, 24, 12, 24 };

static const struct sos HIGHPASS = {
    0.9979867935180664f, -1.9959735870361328f, 0.9979867935180664f, 1.0f, -1.995969533920288f, 0.9959776401519775f,
};

struct state {
    float level, mix, scale;
    struct sos lp;
    struct sos_state hp, lps;
    int w;                              /* the write position */
    float read, fade_from, fade_i, fade_time;   /* the read position; the crossfade's old read, index and length */
    int fading;
    float zero[ZN];                     /* the zero crossings' positions */
    int nzero, rim, rim_next;           /* their count; the marked one; the next to mark (-1 none) */
    int large;                          /* a large peak waits for its zero crossing */
    float front, back;                  /* the peaks in front of and behind the analysis point */
    int rest;                           /* samples since the last period search */
    float period, weight, wbuf[3];
    float env[EN];
    int env_i, onset, onset2, onset_zero, unlock;
    float *buf;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    (void)k;
    *s = (struct state){
        .level = 1.0f, .mix = 1.0f, .scale = 1.0f,
        .lp = { 0.8132230639457703f, 1.6264461278915405f, 0.8132230639457703f, 1.0f, 1.5912595987319946f,
                0.6616326570510864f },
        .w = 3744, .read = 3524.0f, .fade_time = 220.0f, .period = 100.0f, .buf = buffer,
    };
    __builtin_memset(s->buf, 0, BUF * sizeof(float));
    return 0;
}

/* the low pass at 0.4975 fs / r, at most 20 kHz (RBJ, Q 0.707) */
static void lp_set(struct state *s, float r)
{
    float fc = (44100.0f * 0.49751242995262146f) / r;
    fc = fc < 20000.0f ? fc : 20000.0f;                                                      /* VMINNM */
    float w = fc * 0.00014247585204429924f;
    float alpha = st_sinf(w) * 0.7072135806083679f, g = 1.0f / (alpha + 1.0f), c = st_cosf(w);
    float b1 = st_fma(-c, g, g);
    s->lp = (struct sos){ b1 * 0.5f, b1, b1 * 0.5f, 1.0f, (c * -2.0f) * g, st_fma(-alpha, g, g) };
}

/* the largest |buf[j]| for j = from, from + 3, ... while j < to: its index (from if none is above 0) */
static int peak_at(const float *b, int from, int to)
{
    int idx = from;
    float m = 0.0f;
    for (int j = from; j < to; j += 3) {
        float a = st_abs(b[j]);
        if (a > m)
            idx = j;
        m = a > m ? a : m;                                                                   /* VMAXNM */
    }
    return idx;
}

/* a local extreme at v = b[c - 1] against b[c - 4 .. c + 2], positive or negative */
static int extreme(const float *b, int c)
{
    float v = b[c - 1];
    if (v > b[c] && v > b[c - 2] && v > b[c - 3] && v > b[c - 4] && v > b[c + 1] && v > b[c + 2] && v > 0.0f)
        return 1;
    return v < b[c] && v < b[c - 2] && v < b[c - 3] && v < b[c - 4] && v < b[c + 1] && v < b[c + 2] && v < 0.0f;
}

/* the period search: a new period, or 0 */
static float period_search(struct state *s)
{
    int back = 2, off = -40;
    if (s->scale > 1.0f && s->onset_zero > 0) {
        back = 4;
        off = -20;
    }
    int low = s->nzero - back - 1 + off;
    low = low < 0 ? 0 : low;
    int j = s->rim - back;
    if (j < low)
        return 0.0f;
    float p = s->period, far = p * 4.5f, near = p * 0.4f, z0 = s->zero[s->rim], best = 0.0f, min = 10000.0f;
    int a = (int)z0 - LAT;
    for (; j >= low; j -= 2) {
        float zj = s->zero[j], d = z0 - zj;
        if (d < 30.0f)
            continue;
        if (zj < (float)LAT)
            break;
        if (s->onset_zero <= 0 && p > 30.0f && d > far)
            break;
        if (s->onset_zero <= 0 && p > 500.0f && d < near)
            continue;
        const float *x = s->buf + a, *y = s->buf + ((int)zj - LAT);
        float sad = 0.0f;
        for (int t = 0; t < 2 * LAT / 4; t++)
            sad = st_abs(x[4 * t] - y[4 * t]) + sad;
        if (0.019999999552965164f > st_abs(d / p + -1.0f))
            sad = sad * 0.800000011920929f;
        if (min > sad)
            best = d;
        min = sad < min ? sad : min;                                                         /* VMINNM */
    }
    if (best == 0.0f)
        return 0.0f;
    if (s->unlock == 0) {
        float r = best / p;
        if (st_abs(r + -2.0f) < 0.03f || st_abs(r + -3.0f) < 0.03f || st_abs(r + -4.0f) < 0.03f)
            return 0.0f;
    }
    return best;
}

/* a sample of the buffer between two points */
static float at(const float *b, int i, float f)
{
    return st_fma(b[i + 1] - b[i], f, b[i]);
}

/* the buffer full: everything moves HOP back, the zero crossings before the latency point dropped */
static void move_back(struct state *s)
{
    __builtin_memmove(s->buf, s->buf + HOP, (BUF - HOP) * sizeof(float));
    s->w -= HOP;
    for (int i = 0; i < s->nzero; i++)
        s->zero[i] = s->zero[i] - 256.0f;
    if (s->onset_zero > 0)
        s->onset_zero -= HOP;
    int n = 0;
    while (n < s->nzero && s->zero[n] < (float)LAT)
        n++;
    __builtin_memmove(s->zero, s->zero + n, (s->nzero - n) * sizeof(float));
    __builtin_memset(s->zero + s->nzero - n, 0, (ZN - (s->nzero - n)) * sizeof(float));
    s->nzero -= n;
    s->rim_next -= n;
    s->rim -= n;
    if (s->rim < 10 && s->nzero > 10)
        s->rim = s->nzero - 1;
    if (s->read >= 256.0f)
        s->read = s->read - 256.0f;
    if (s->fade_from >= 256.0f)
        s->fade_from = s->fade_from - 256.0f;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float step = (float)io->frames * 0.0010000000474974513f;
    s->level = st_glide_by(s->level, k[3] * 0.01f, step);
    s->mix = st_glide_by(s->mix, k[2] * 0.01f, step);
    int r = (int)k[0];
    float shift = st_fma(k[1] * 0.01f, (float)(RANGE_HI[r] - RANGE_LO[r]), (float)RANGE_LO[r]);
    float scale = st_exp2f(shift * 0.0833333358168602f);
    if (scale != s->scale) {
        lp_set(s, scale);
        s->scale = scale;
    }
    if (!io->enabled)
        return;
    float fade_max = scale * 256.0f;
    int first = 1;                      /* the peaks are searched once per block */
    float *p = io->io, *b = s->buf;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float x = p[0];
        b[s->w] = sos_run(&s->lp, &s->lps, sos_run(&HIGHPASS, &s->hp, x));
        int c = s->w - LAT;
        float v = b[c - 1], u = b[c];
        /* a large peak marks the next zero crossing */
        if (!s->large && extreme(b, c)) {
            if (first) {
                s->front = st_abs(b[peak_at(b, s->w + 1 - LAT, s->w + 1)]);
                int from = s->w + 1 - 2 * LAT;
                s->back = st_abs(b[from < s->w + 1 - LAT ? peak_at(b, from, s->w + 1 - LAT) : from]);
            }
            float thr = s->onset_zero > 0 ? 0.05000000074505806f : 0.30000001192092896f;
            s->large = st_abs(v) > thr * s->back || st_abs(v) > s->front * thr;
            if (s->large && s->rim_next >= 1)
                s->rim = s->rim_next;
            s->rim_next = -1;
            first = 0;
        }
        if (s->rest <= 0x3f)
            s->rest++;
        /* a zero crossing */
        if (v * u < 0.0f) {
            float f = v / (v - u);
            s->zero[s->nzero] = (((float)s->w + -1.0f) - (float)LAT) + f;
            if (s->large) {
                s->rim_next = s->nzero;
                s->large = 0;
            }
            if (s->nzero < 49) {
                s->nzero++;
            } else {
                __builtin_memmove(s->zero, s->zero + 1, 49 * sizeof(float));
                s->zero[49] = 0.0f;
                s->rim_next--;
                if (s->rim < 1 || --s->rim < 10)
                    s->rim = s->nzero - 1;
            }
        }
        /* the period */
        float period = s->period;
        if (s->rest >= 0x40) {
            s->rest = 0;
            float np = period_search(s);
            if (np != 0.0f)
                period = s->period = np;
        }
        if (period != s->wbuf[0]) {
            float w0 = s->wbuf[0], w1 = s->wbuf[1];
            s->wbuf[0] = period;
            s->wbuf[1] = w0;
            s->wbuf[2] = w1;
            s->weight = period;
            if (st_abs(period - w0) / period < 0.03f && st_abs(period - w1) / period < 0.03f)
                s->weight = ((w0 + period) + w1) * 0.3333333432674408f;
        }
        s->unlock = s->unlock >= 1 ? s->unlock - 1 : 0;
        /* the read */
        s->read = s->read + scale;
        if (scale == 1.0f && (float)s->w - s->read > 100.0f)
            s->read = s->read + 9.999999747378752e-05f;
        int start = 0;
        if (scale > 1.0f) {
            float dist = (float)s->w - s->read;
            if (dist < (float)LAT) {
                s->fade_from = s->read;
                float jump = s->weight != 0.0f ? -s->weight : -100.0f;
                float t = fade_max < dist ? fade_max : dist;                                  /* VMINNM */
                s->read = jump + s->read;
                s->fading = 1;
                s->fade_time = t < -jump ? t : -jump;                                          /* VMINNM */
                if (!(s->read < (float)s->onset_zero))
                    s->onset_zero = -1;
                start = 1;
            }
        } else if (s->weight > 0.0f && !s->fading &&
                   s->weight + 20.0f < (float)(s->w - (int)s->read)) {
            s->fade_from = s->read;
            s->fading = 1;
            s->fade_time = fade_max;
            float rd = s->read;
            do
                rd = rd + s->weight;
            while (s->weight + 20.0f < (float)(s->w - (int)rd));
            s->read = rd;
            start = 1;
        }
        if (start)
            s->fade_i = 0.0f;
        int ri = (int)s->read;
        float rf = s->read - __builtin_truncf(s->read), y;
        if (!s->fading) {
            y = at(b, ri, rf);
        } else {
            float c2 = st_cosf((s->fade_i * 6.2831854820251465f) / s->fade_time);
            float o = s->fade_from + s->fade_i;
            s->fade_i = s->fade_i + scale;
            float a = at(b, ri, rf), old = at(b, (int)o, o - __builtin_truncf(o));
            y = st_fma(a - old, st_fma(-c2, 0.5f, 0.5f), old);
            if (s->fade_i >= s->fade_time * 0.5f) {
                s->fade_i = 0.0f;
                s->fading = 0;
            }
        }
        /* onsets */
        float e0 = s->env[s->env_i], ay = st_abs(y);
        float coef = ay > e0 ? 0.9931418299674988f : 0.002488076686859131f;
        int ni = s->env_i > 0x62 ? 0 : s->env_i + 1;
        float e = st_fma(coef, ay - e0, e0), ratio = e / s->env[ni];
        int on = (double)ratio > 3.2 && (double)e > 0.01;
        s->env_i = ni;
        if (s->onset != 1 && on)
            s->onset_zero = (int)s->zero[s->nzero - 1];
        s->onset = on;
        int on2 = ratio > 1.600000023841858f && e > 0.0010000000474974513f;
        if (s->onset2 != 1 && on2)
            s->unlock = 4410;
        s->onset2 = on2;
        s->env[ni] = e;
        /* the buffer */
        if (s->w++ >= 0xf9f)
            move_back(s);
        p[0] = p[1] = st_fma(st_fma(s->level, y, -x), s->mix, x);                          /* VFNMS, VFMA */
    }
}

const struct wvfx_desc fx_pitch_s_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_FX, 0x105),
    .menu = WVFX_FX,
    .nparams = sizeof params / sizeof params[0],
    .name = "Pitch S",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUF * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
