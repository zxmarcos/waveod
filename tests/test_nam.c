/*
 * The NAM engine (nam/a2.h, nam/rs48.h) and its Clone block (engine/nam_block.c) on the PC.
 *
 * - The network against a float64 reference written from the model's definition (a2ref-style, whole signal at
 *   once, layer by layer), on pseudo-random weights: rounding only.
 * - Blocks of any size 1..A2_BLOCK give the same bits as blocks of 64 (the groups of 4 and their spare frames
 *   change nothing).
 * - The block: a capture is recognised by its header, refused with a broken CRC; at 44.1 kHz with neutral knobs
 *   it is the network at the input's scale times the loudness factor, Volume 0 mutes; at 48 kHz silence stays the
 *   steady state and a tone comes back at about the level of the 44.1 kHz path. The short histories in the code
 *   pool or in the block's state: the same bits.
 * - The resampler: its FIFO, delay, pass band and images.
 * tools/check_nam.py compares this build with the M7's (the emulator) and with NAM's own code on a real .nam.
 */
#include "engine/blocks.h"
#include "engine/pools.h"
#include "fw/crc32.h"
#include "fw/ir_fs.h"
#include "nam/a2.h"
#include "nam/capture.h"
#include "nam/rs48.h"
#include "test.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IRFS_CAPTURE_SIZE IRFS_CLONE_DATA
#define K_VOLUME_TEST 1                         /* the Clone entry's Volume knob */

static const int kernel[A2_LAYERS] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
static const int dilation[A2_LAYERS] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13,
                                        1, 3, 7, 17, 41, 101, 239};

static uint32_t rng = 12345;

static float urand(float scale)
{
    rng = rng * 1664525u + 1013904223u;
    return ((float)(rng >> 8) / 16777216.0f - 0.5f) * 2.0f * scale;
}

/* plausible weights: conv and 1x1 matrices around 0.3, biases small, the head around 0.3 */
static void make_weights(float *w)
{
    int i = 0;
    for (int c = 0; c < 3; c++)
        w[i++] = urand(1.0f);
    for (int l = 0; l < A2_LAYERS; l++) {
        for (int k = 0; k < kernel[l] * 9; k++)
            w[i++] = urand(0.6f / sqrtf((float)kernel[l]));
        for (int k = 0; k < 3; k++)
            w[i++] = urand(0.05f);
        for (int k = 0; k < 3; k++)
            w[i++] = urand(0.5f);
        for (int k = 0; k < 9; k++)
            w[i++] = urand(0.4f);
        for (int k = 0; k < 3; k++)
            w[i++] = urand(0.05f);
    }
    for (int k = 0; k < A2_HEAD_K * 3; k++)
        w[i++] = urand(0.3f);
    w[i++] = urand(0.05f);
    w[i++] = 0.8f;
    CHECK(i == A2_NUM_WEIGHTS, "%d weights packed", i);
}

#define PRE 64                                  /* silence before the signal (the history is edge-padded) */

/* the model in float64 over x[0..n) with silence before it: y[0..n) */
static void reference(const float *w, const float *x, int n, double *y)
{
    const int T = PRE + n;
    double *h = calloc((size_t)T * 3, sizeof *h), *hs = calloc((size_t)T * 3, sizeof *hs);
    double *z = calloc((size_t)T * 3, sizeof *z), *cond = calloc((size_t)T, sizeof *cond);
    for (int t = 0; t < n; t++)
        cond[PRE + t] = x[t];
    const float *rc = w, *p = w + 3;
    for (int t = 0; t < T; t++)
        for (int c = 0; c < 3; c++)
            h[t * 3 + c] = cond[t] * rc[c];
    for (int l = 0; l < A2_LAYERS; l++) {
        const int K = kernel[l], d = dilation[l];
        const float *conv = p, *b = conv + K * 9, *m = b + 3, *l1 = m + 3, *lb = l1 + 9;
        p = lb + 3;
        for (int t = 0; t < T; t++)
            for (int i = 0; i < 3; i++) {
                double v = b[i];
                for (int k = 0; k < K; k++) {
                    int u = t - (K - 1 - k) * d;
                    if (u < 0)
                        u = 0;
                    for (int j = 0; j < 3; j++)
                        v += h[u * 3 + j] * conv[k * 9 + j * 3 + i];
                }
                v += cond[t] * m[i];
                z[t * 3 + i] = v >= 0 ? v : 0.01 * v;
            }
        for (int t = 0; t < T; t++)
            for (int i = 0; i < 3; i++) {
                hs[t * 3 + i] += z[t * 3 + i];
                if (l + 1 < A2_LAYERS) {
                    double v = lb[i];
                    for (int j = 0; j < 3; j++)
                        v += z[t * 3 + j] * l1[j * 3 + i];
                    h[t * 3 + i] += v;
                }
            }
    }
    const float *head = p, hb = p[A2_HEAD_K * 3], scale = p[A2_HEAD_K * 3 + 1];
    for (int t = 0; t < n; t++) {
        double v = hb;
        for (int k = 0; k < A2_HEAD_K; k++)
            for (int j = 0; j < 3; j++)
                v += hs[(PRE + t - (A2_HEAD_K - 1 - k)) * 3 + j] * head[k * 3 + j];
        y[t] = v * scale;
    }
    free(h);
    free(hs);
    free(z);
    free(cond);
}

static void test_signal(float *x, int n)
{
    for (int t = 0; t < n; t++) {
        const double s = t / 44100.0;
        x[t] = (float)(0.45 * sin(2 * M_PI * 196.0 * s) * exp(-3.0 * fmod(s, 0.25)) +
                       0.2 * sin(2 * M_PI * 1318.5 * s) + urand(0.02f));
    }
}

/* the engine over x in blocks of the sizes in `sizes` (cycled) */
static void run(const float *w, const float *x, float *y, int n, const int *sizes, int nsizes)
{
    static struct a2 net;
    static float dw[A2_DW_FLOATS], lin[A2_LIN_FLOATS + 2];
    lin[A2_LIN_FLOATS] = lin[A2_LIN_FLOATS + 1] = 12345.0f;
    a2_init(&net, dw, lin, w);
    for (int t = 0, i = 0; t < n; i++) {
        int b = sizes[i % nsizes];
        if (b > n - t)
            b = n - t;
        a2_process(&net, x + t, y + t, b);
        t += b;
    }
    CHECK(lin[A2_LIN_FLOATS] == 12345.0f && lin[A2_LIN_FLOATS + 1] == 12345.0f,
          "the linear histories stay within A2_LIN_FLOATS");
}

static double err_db(const double *ref, const float *y, int n)
{
    double e = 0, s = 0;
    for (int t = 0; t < n; t++) {
        e += (y[t] - ref[t]) * (y[t] - ref[t]);
        s += ref[t] * ref[t];
    }
    return e > 0 ? 10 * log10(e / s) : -400.0;
}

#define N 6000

static void test_network(const float *w)
{
    static float x[N], y64[N], ymix[N];
    static double ref[N];
    test_signal(x, N);
    reference(w, x, N, ref);
    const int b64[] = {64}, mixed[] = {1, 7, 72, 70, 69, 3, 4, 5, 64, 2, 71, 13};
    run(w, x, y64, N, b64, 1);
    run(w, x, ymix, N, mixed, (int)(sizeof mixed / sizeof mixed[0]));
    double rms = 0;
    for (int t = 0; t < N; t++)
        rms += ref[t] * ref[t];
    rms = sqrt(rms / N);
    const double e = err_db(ref, y64, N);
    printf("nam: float64 reference rms %.3f, the engine %.1f dB from it\n", rms, e);
    CHECK(rms > 0.01 && e < -95.0, "the engine against float64: %.1f dB (rms %.3f)", e, rms);
    CHECK(memcmp(y64, ymix, sizeof y64) == 0, "blocks of 1..72 give the bits of blocks of 64");
}

/* a capture blob around the weights */
static void make_capture(uint8_t *blob, const float *w, float rate, float loud)
{
    memset(blob, 0, IRFS_CAPTURE_SIZE);
    struct nam_capture h = {0};
    h.magic = NAM_CAPTURE_MAGIC;
    h.version = NAM_CAPTURE_VERSION;
    h.nweights = A2_NUM_WEIGHTS;
    h.loudness = loud;
    h.sample_rate = rate;
    memcpy(blob + NAM_CAPTURE_HEADER, w, A2_NUM_WEIGHTS * 4);
    h.crc = crc32(0, blob + NAM_CAPTURE_HEADER, A2_NUM_WEIGHTS * 4);
    memcpy(blob, &h, sizeof h);
}

static int block_init(const struct wvfx_desc *d, void *state, const uint8_t *blob, const float *knobs)
{
    struct wvfx_env env = {WVFX_ABI, 44100.0f, 64, 0, 0, blob, IRFS_CAPTURE_SIZE};
    return d->init(state, 0, &env, knobs);
}

static void block_run(const struct wvfx_desc *d, void *state, float *io, const float *knobs)
{
    struct wvfx_io b = {io, io, 0, 0, knobs, 500, 64, 1, 0};
    d->process(state, &b);
}

static void test_block(const float *w)
{
    static uint8_t blob[IRFS_CAPTURE_SIZE];
    float knobs[WVFX_KNOBS] = {50, 50, 50, 50, 50};
    make_capture(blob, w, 44100.0f, -12.0f);
    const struct wvfx_desc *d = blocks_clone_engine(blob, IRFS_CAPTURE_SIZE);
    CHECK(d && d->menu == WVFX_CLONE && (d->flags & WVFX_ENGINE), "a capture runs on the NAM block");
    CHECK(!blocks_clone_engine(blob, 0x1000), "not in a slot too small for it");
    uint8_t other[64] = {1, 2, 3};
    CHECK(!blocks_clone_engine(other, sizeof other), "other data: the original's Clone module");
    if (!d)
        return;
    pools_init();
    pools_reset();
    void *state = calloc(1, d->state_size);
    blob[NAM_CAPTURE_HEADER + 100] ^= 1;
    CHECK(block_init(d, state, blob, knobs) == -1, "a broken CRC does not run");
    blob[NAM_CAPTURE_HEADER + 100] ^= 1;
    CHECK(block_init(d, state, blob, knobs) == 0, "the capture loads");

    /* 44.1 kHz, neutral knobs: the network on the mono sum x 0.45659, times 10^(-6/20) for loudness -12 */
    static float x[N], y[N], io[128];
    test_signal(x, N);
    static struct a2 net;
    static float dw[A2_DW_FLOATS], lin[A2_LIN_FLOATS], xs[N];
    a2_init(&net, dw, lin, w);
    for (int t = 0; t < N; t++)
        xs[t] = x[t] * 0.45659f;
    for (int t = 0; t + 64 <= N; t += 64)
        a2_process(&net, xs + t, y + t, 64);
    double e = 0, s = 0;
    const float g = powf(10.0f, -6.0f / 20.0f);
    for (int t = 0; t + 64 <= N; t += 64) {
        for (int f = 0; f < 64; f++)
            io[2 * f] = io[2 * f + 1] = x[t + f];
        block_run(d, state, io, knobs);
        for (int f = 0; f < 64; f++) {
            const double want = y[t + f] * g;
            e += (io[2 * f] - want) * (io[2 * f] - want) + (io[2 * f + 1] - want) * (io[2 * f + 1] - want);
            s += 2 * want * want;
        }
    }
    CHECK(10 * log10(e / s) < -70.0, "44.1 kHz, neutral knobs: %.1f dB from the network x the loudness",
          10 * log10(e / s));
    knobs[K_VOLUME_TEST] = 0;
    for (int f = 0; f < 128; f++)
        io[f] = 0.3f;
    block_run(d, state, io, knobs);                 /* the ramp down */
    block_run(d, state, io, knobs);
    int silent = 1;
    for (int f = 0; f < 128; f++)
        silent &= io[f] == 0.0f;
    CHECK(silent, "Volume 0 mutes");
    knobs[K_VOLUME_TEST] = 50;

    /* 48 kHz: silence settles on the 44.1 kHz path's steady state (the network's silent output; the resampler's
       histories start at zero, so it takes a block to come through) */
    make_capture(blob, w, 44100.0f, -18.0f);
    block_init(d, state, blob, knobs);
    memset(io, 0, sizeof io);
    block_run(d, state, io, knobs);
    const float first = io[0];
    make_capture(blob, w, 48000.0f, -18.0f);
    CHECK(block_init(d, state, blob, knobs) == 0, "a 48 kHz capture loads");
    float lo = 1e9f, hi = -1e9f;
    for (int b = 0; b < 40; b++) {
        memset(io, 0, sizeof io);
        block_run(d, state, io, knobs);
        for (int f = 0; b >= 4 && f < 128; f++) {
            lo = io[f] < lo ? io[f] : lo;
            hi = io[f] > hi ? io[f] : hi;
        }
    }
    CHECK(fabsf(hi - first) < 1e-3f * fabsf(first) && fabsf(lo - first) < 1e-3f * fabsf(first),
          "48 kHz silence: %.6g..%.6g, the 44.1 kHz path's %.6g", lo, hi, first);

    /* a tone at 48 kHz and at 44.1 kHz: the same network, about the same level */
    double r48 = 0, r44 = 0;
    for (int pass = 0; pass < 2; pass++) {
        make_capture(blob, w, pass ? 44100.0f : 48000.0f, -18.0f);
        block_init(d, state, blob, knobs);
        double sum = 0;
        for (int t = 0; t + 64 <= N; t += 64) {
            for (int f = 0; f < 64; f++)
                io[2 * f] = io[2 * f + 1] = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * (float)(t + f) / 44100.0f);
            block_run(d, state, io, knobs);
            for (int f = 0; t >= 1024 && f < 64; f++)
                sum += (io[2 * f] - first) * (io[2 * f] - first);
        }
        if (pass)
            r44 = sum;
        else
            r48 = sum;
    }
    const double ratio = 10 * log10(r48 / r44);
    CHECK(fabs(ratio) < 3.0, "a 440 Hz tone at 48 kHz against 44.1 kHz: %+.2f dB", ratio);

    /* the short histories in the code pool; in the block's state when the pool has no room to spare (pass 1)
       or the preset loads beside the previous one's tail (pass 2) */
    static float got[3][N];
    uint32_t used[3];
    make_capture(blob, w, 48000.0f, -18.0f);
    for (int pass = 0; pass < 3; pass++) {
        pools_reset();
        if (pass == 1)
            pool_alloc(POOL_CODE, pools[POOL_CODE].size - 0x12000);
        if (pass == 2) {
            pools_use(1);
            pool_alloc(POOL_CODE, 64);
            pools_use(0);
        }
        const uint32_t before = pools[POOL_CODE].used;
        block_init(d, state, blob, knobs);
        used[pass] = pools[POOL_CODE].used - before;
        for (int t = 0; t + 64 <= N; t += 64) {
            for (int f = 0; f < 64; f++)
                io[2 * f] = io[2 * f + 1] = x[t + f];
            block_run(d, state, io, knobs);
            for (int f = 0; f < 64; f++)
                got[pass][t + f] = io[2 * f];
        }
    }
    pools_reset();
    CHECK(used[0] >= (A2_DW_FLOATS + A2_LIN_FLOATS) * 4 && used[1] < A2_DW_FLOATS * 4 + 64 &&
          used[2] < A2_DW_FLOATS * 4 + 64,
          "code pool: %u bytes with room to spare, %u with 72 KB left, %u beside a tail", (unsigned)used[0],
          (unsigned)used[1], (unsigned)used[2]);
    CHECK(memcmp(got[0], got[1], (N / 64) * 64 * sizeof(float)) == 0 &&
          memcmp(got[0], got[2], (N / 64) * 64 * sizeof(float)) == 0, "the same bits wherever the short histories are");
    free(state);
}

/* amplitude of the component at f of y[0..n) sampled at fs (Hann window) */
static double tone_level(const float *y, int n, double f, double fs)
{
    double re = 0, im = 0, ws = 0;
    for (int i = 0; i < n; i++) {
        const double win = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        re += win * y[i] * cos(2 * M_PI * f * i / fs);
        im += win * y[i] * sin(2 * M_PI * f * i / fs);
        ws += win;
    }
    return 2 * sqrt(re * re + im * im) / ws;
}

#define RB 400                                  /* blocks per tone */

/* The resampler alone: the up path's count and the FIFO, the delay, the pass band, and what a tone leaves at
   its image (up: 44.1k - f, folded at 48 kHz; down: 48k - f, which lands at |f - 3.9k| at 44.1 kHz). */
static void test_resampler(void)
{
    static struct rs48 r;
    static float in[RB * 64], mid[RB * RS_MAX48 + 8], out[RB * 64];
    rs48_init(&r);
    int lo = 99, hi = -1, cmin = 99, cmax = 0;
    memset(in, 0, sizeof in);
    in[1000] = 1.0f;
    for (int b = 0; b < RB; b++) {
        const int c = rs48_up(&r, in + b * 64, mid);
        rs48_down(&r, mid, c, out + b * 64);
        lo = r.fifo_n < lo ? r.fifo_n : lo;
        hi = r.fifo_n > hi ? r.fifo_n : hi;
        cmin = c < cmin ? c : cmin;
        cmax = c > cmax ? c : cmax;
    }
    CHECK(cmin == 69 && cmax == RS_MAX48 && lo >= 0 && hi <= 2,
          "the up path makes %d..%d samples, the FIFO keeps %d..%d", cmin, cmax, lo, hi);
    double num = 0, den = 0;
    for (int i = 0; i < RB * 64; i++) {
        num += (double)out[i] * out[i] * i;
        den += (double)out[i] * out[i];
    }
    const double delay = num / den - 1000.0;
    printf("nam: resampler delay %.2f samples (%.3f ms)\n", delay, delay / 44.1);
    CHECK(delay > 44.0 && delay < 46.5, "the resampler's delay: %.2f samples", delay);

    double ripple = 0, image_up = -400, image_dn = -400;
    const double fpass[] = {60, 1000, 5000, 10000, 15000, 18000, 20000};
    for (unsigned k = 0; k < sizeof fpass / sizeof fpass[0]; k++) {
        rs48_init(&r);
        for (int i = 0; i < RB * 64; i++)
            in[i] = (float)(0.5 * sin(2 * M_PI * fpass[k] * i / 44100.0));
        for (int b = 0; b < RB; b++) {
            const int c = rs48_up(&r, in + b * 64, mid);
            rs48_down(&r, mid, c, out + b * 64);
        }
        const double db = 20 * log10(tone_level(out + 2048, RB * 64 - 2048, fpass[k], 44100.0) / 0.5);
        ripple = fabs(db) > ripple ? fabs(db) : ripple;
    }
    CHECK(ripple < 0.005, "44.1k -> 48k -> 44.1k is flat to 20 kHz within %.4f dB", ripple);
    const double ftone[] = {1000, 5000, 10000, 16000};
    for (unsigned k = 0; k < sizeof ftone / sizeof ftone[0]; k++) {
        rs48_init(&r);
        int n = 0;
        for (int b = 0; b < RB; b++) {
            for (int i = 0; i < 64; i++)
                in[i] = (float)(0.5 * sin(2 * M_PI * ftone[k] * (b * 64 + i) / 44100.0));
            n += rs48_up(&r, in, mid + n);
        }
        double fi = 44100.0 - ftone[k];
        if (fi > 24000.0)
            fi = 48000.0 - fi;
        double db = 20 * log10(tone_level(mid + 2048, n - 2048, fi, 48000.0) / 0.5 + 1e-12);
        image_up = db > image_up ? db : image_up;

        rs48_init(&r);
        long ph = 0;
        for (int b = 0; b < RB; b++) {
            const int c = rs48_up(&r, in, mid);     /* for the count only */
            for (int i = 0; i < c; i++, ph++)
                mid[i] = (float)(0.5 * sin(2 * M_PI * ftone[k] * (double)ph / 48000.0));
            rs48_down(&r, mid, c, out + b * 64);
        }
        db = 20 * log10(tone_level(out + 2048, RB * 64 - 2048, fabs(ftone[k] - 3900.0), 44100.0) / 0.5 + 1e-12);
        image_dn = db > image_dn ? db : image_dn;
    }
    printf("nam: resampler images of 1..16 kHz tones: up %.1f dB, down %.1f dB\n", image_up, image_dn);
    CHECK(image_up < -85.0 && image_dn < -85.0, "the images of 1..16 kHz tones: up %.1f dB, down %.1f dB", image_up,
          image_dn);
}

void test_nam(void)
{
    static float w[A2_NUM_WEIGHTS];
    make_weights(w);
    test_network(w);
    test_block(w);
    test_resampler();
}
