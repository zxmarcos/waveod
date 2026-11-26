#include "engine/cabir.h"
#include "dsp/fmath.h"
#include "dsp/rfft.h"
#include "engine/cabmic.h"
#include "platform/sdram.h"
#include <string.h>

#define IR_SCALE    1.1920928955078125e-07f     /* 2^-23 */
#define LEVEL       0.27f
#define VOL_SCALE   0.01f
#define VOL_STEP    0.002f
#define VOL_PULL    2.2676000e-4f
#define W_PER_HZ    1.4247585e-4f               /* 2 pi / 44100, as the original rounds it */
#define ALPHA_SIN   0.70721358f                 /* 1 / (2 Q) */
#define CLIP        2.7544100f                  /* 0x40304850: the level is limited to this before the filters */

static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }

static uint8_t in_use = 1;

void cabir_set_in_use(int on)
{
    in_use = (uint8_t)(on != 0);
}

int cabir_in_use(void)
{
    return in_use;
}

/* FFT tables for 2 x block, block = 16, 32, 64 */
static struct rfft ffts[3];
static uint32_t fft_mem[RFFT_MEM_WORDS(32) + RFFT_MEM_WORDS(64) + RFFT_MEM_WORDS(128)];
static int fft_ready;

static const struct rfft *fft_for(int block)
{
    if (!fft_ready) {
        uint32_t *m = fft_mem;
        for (int i = 0; i < 3; i++) {
            int n = 32 << i;
            rfft_init(&ffts[i], n, m);
            m += RFFT_MEM_WORDS(n);
        }
        fft_ready = 1;
    }
    return &ffts[block == 16 ? 0 : block == 32 ? 1 : 2];
}

static int valid_block(int b)
{
    return b == 16 || b == 32 || b == 64;
}

/* the spectra of `ir` (CABIR_LEN floats) in partitions of `block` */
static void spectra(const float *ir, int block, float *h)
{
    const struct rfft *f = fft_for(block);
    float frame[2 * CABIR_BLOCK];
    for (int p = 0; p < CABIR_LEN / block; p++) {
        memcpy(frame, ir + p * block, sizeof(float) * (size_t)block);
        memset(frame + block, 0, sizeof(float) * (size_t)block);
        rfft_forward(f, frame, h + p * (block + 1) * 2);
    }
}

/* the IR spectra and an empty history for partitions of `block` */
static void partition(struct cabir *c, int block)
{
    c->block = block;
    c->parts = CABIR_LEN / block;
    c->head = 0;
    c->pending = c->fading = 0;
    memset(c->in, 0, sizeof c->in);
    memset(c->x, 0, sizeof c->x);
    spectra(c->irf, block, c->h[c->cur]);
}

/* the IR the knobs make of the flash data: as it is, or through the virtual mic */
static void effective_ir(const struct cabir *c, const float *knobs, float *out)
{
    if (!c->ir) {
        memset(out, 0, sizeof(float) * CABIR_LEN);
    } else if (cabmic_neutral(knobs)) {
        for (int k = 0; k < CABIR_LEN; k++)
            out[k] = (float)c->ir[k] * IR_SCALE;
    } else {
        float db[CABMIC_BANDS];
        cabmic_curve(knobs, db);
        cabmic_apply(c->ir, CABIR_LEN, db, out);
    }
}

static void remember_mic(struct cabir *c, const float *knobs)
{
    c->mic[0] = knobs[CABMIC_KNOB_POSITION];
    c->mic[1] = knobs[CABMIC_KNOB_DISTANCE];
    c->mic[2] = knobs[CABMIC_KNOB_ANGLE];
    c->mic[3] = (float)cabmic_type(knobs);
}

static void design_low_cut(struct cabir *c, float hz)
{
    float w = hz * W_PER_HZ, cs = cosf(w), alpha = sinf(w) * ALPHA_SIN;
    c->lo[0] = (cs + 1.0f) * 0.5f;
    c->lo[1] = -(cs + 1.0f);
    c->lo[2] = (cs + 1.0f) * 0.5f;
    c->lo[3] = alpha + 1.0f;
    c->lo[4] = cs * -2.0f;
    c->lo[5] = 1.0f - alpha;
    c->seen_low = hz;
}

static void design_high_cut(struct cabir *c, float hz)
{
    float w = hz * W_PER_HZ, cs = cosf(w), alpha = sinf(w) * ALPHA_SIN;
    c->hi[0] = (1.0f - cs) * 0.5f;
    c->hi[1] = 1.0f - cs;
    c->hi[2] = (1.0f - cs) * 0.5f;
    c->hi[3] = alpha + 1.0f;
    c->hi[4] = cs * -2.0f;
    c->hi[5] = 1.0f - alpha;
    c->seen_high = hz;
}

void cabir_init(struct cabir *c, const float *knobs, const int32_t *ir, int block)
{
    memset(c->lo_s, 0, sizeof c->lo_s);
    memset(c->hi_s, 0, sizeof c->hi_s);
    c->ir = ir;
    c->cur = 0;
    c->gain = knobs[CABIR_KNOB_VOLUME] * VOL_SCALE;
    design_low_cut(c, knobs[CABIR_KNOB_LOW_CUT]);
    design_high_cut(c, knobs[CABIR_KNOB_HIGH_CUT]);
    effective_ir(c, knobs, c->irf);
    remember_mic(c, knobs);
    partition(c, valid_block(block) ? block : CABIR_BLOCK);
}

int cabir_poll(struct cabir *c, const float *knobs)
{
    static float next[CABIR_LEN] SDRAM_BSS;
    if (c->pending || c->fading)
        return 0;
    if (knobs[CABMIC_KNOB_POSITION] == c->mic[0] && knobs[CABMIC_KNOB_DISTANCE] == c->mic[1] &&
        knobs[CABMIC_KNOB_ANGLE] == c->mic[2] && (float)cabmic_type(knobs) == c->mic[3])
        return 0;
    remember_mic(c, knobs);
    effective_ir(c, knobs, next);
    int block = c->block, spare = !c->cur;
    spectra(next, block, c->h[spare]);
    memcpy(c->irf, next, sizeof next);
    barrier();
    if (c->block == block)                      /* the interrupt changed the partitions meanwhile: keep its own */
        c->pending = 1;
    return 1;
}

/* the original's level ramp: a pull towards the knob, then 0.002 more, never past it */
static void level_step(struct cabir *c, float knob)
{
    float t = knob * VOL_SCALE, g = c->gain;
    g = g + (t - g) * VOL_PULL;
    if (g != t) {
        if (t > g) {
            g += VOL_STEP;
            if (g >= t)
                g = t;
        } else {
            g -= VOL_STEP;
            if (g <= t)
                g = t;
        }
    }
    c->gain = g;
}

/* direct form I as the original computes it: (b0 x + b1 x1 + b2 x2 - (a1 y1 + a2 y2)) / a0, multiply-adds */
static inline float biquad(const float *k, float *s, float x)
{
    float p = __builtin_fmaf(k[2], s[1], __builtin_fmaf(k[1], s[0], k[0] * x));
    float q = __builtin_fmaf(k[5], s[3], k[4] * s[2]);
    float y = (p - q) / k[3];
    s[1] = s[0];
    s[0] = x;
    s[3] = s[2];
    s[2] = y;
    return y;
}

/* the newest `block` output samples of the convolution with the spectra `h` */
static void convolve(const struct cabir *c, const float *h, float *out)
{
    int b = c->block, bins = b + 1, P = c->parts;
    float y[2 * (CABIR_BLOCK + 1)], frame[2 * CABIR_BLOCK];
    memset(y, 0, sizeof(float) * (size_t)(2 * bins));
    int slot = c->head;
    for (int p = 0; p < P; p++) {               /* Y = sum over p of X[n - p] H[p] */
        const float *x = c->x + slot * bins * 2, *hp = h + p * bins * 2;
        for (int k = 0; k < bins; k++) {
            float xr = x[2 * k], xi = x[2 * k + 1], hr = hp[2 * k], hi = hp[2 * k + 1];
            y[2 * k] += xr * hr - xi * hi;
            y[2 * k + 1] += xr * hi + xi * hr;
        }
        slot = slot ? slot - 1 : P - 1;
    }
    rfft_inverse(fft_for(b), y, frame);
    memcpy(out, frame + b, sizeof(float) * (size_t)b);
}

void cabir_process(struct cabir *c, float *io, int frames, const float *knobs, int enabled)
{
    if (!enabled || !valid_block(frames))
        return;
    if (frames != c->block)
        partition(c, frames);
    if (c->pending) {                           /* new spectra from cabir_poll: crossfade to them over this block */
        c->cur = !c->cur;
        c->pending = 0;
        c->fading = 1;
    }
    /* a changed cut: below 20 Hz the low cut is 19, above 20 kHz the high cut 20001 (Init takes them as they are) */
    float lo = knobs[CABIR_KNOB_LOW_CUT], hi = knobs[CABIR_KNOB_HIGH_CUT];
    if (lo != c->seen_low)
        design_low_cut(c, lo < 20.0f ? 19.0f : lo);
    if (hi != c->seen_high)
        design_high_cut(c, hi > 20000.0f ? 20001.0f : hi);
    c->seen_low = lo;
    c->seen_high = hi;
    level_step(c, knobs[CABIR_KNOB_VOLUME]);

    int b = frames, bins = b + 1;
    float y[CABIR_BLOCK];
    memmove(c->in, c->in + b, sizeof(float) * (size_t)b);
    for (int i = 0; i < b; i++)
        c->in[b + i] = io[2 * i];
    c->head = c->head + 1 == c->parts ? 0 : c->head + 1;
    rfft_forward(fft_for(b), c->in, c->x + c->head * bins * 2);
    convolve(c, c->h[c->cur], y);
    if (c->fading) {
        float old[CABIR_BLOCK];
        convolve(c, c->h[!c->cur], old);
        for (int i = 0; i < b; i++) {
            float t = (float)(i + 1) / (float)b;
            y[i] = old[i] + (y[i] - old[i]) * t;
        }
        c->fading = 0;
    }

    /* level, the limit, then the high cut and the low cut (that order matters when a cut moves) */
    for (int i = 0; i < b; i++) {
        float v = y[i] * LEVEL * c->gain;
        v = v < -CLIP ? -CLIP : v > CLIP ? CLIP : v;
        v = biquad(c->hi, c->hi_s, v);
        v = biquad(c->lo, c->lo_s, v);
        io[2 * i] = io[2 * i + 1] = v;
    }
}
