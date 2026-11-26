#include "dsp/biquad.h"
#include "dsp/fmath.h"

/* w = hz * 2 pi * (1 / fs), multiplied in this order like the original */
#define TWO_PI  6.2831854820251465f
#define INV_FS  2.2675736545352265e-05f

/* The sum before the a0 division, in this order (the compiler fuses it the same way wherever it is written). */
#define BQ_SUM(x, x1, x2, y1, y2) ((((x) * b0 + (x1) * b1 + (x2) * b2) - (y1) * a1) - (y2) * a2)

/* Both channels side by side with their state in registers (through the pointers the compiler would load and
   store it every sample: a store to buf may be one to the state). With a0 = 1 (the input filter) the division
   is left out: x / 1 = x exactly. (s / a0 as RN(1 / a0) and an FMA correction, Markstein's, gives the same bits
   but for under- / overflow and the sign of -0: guarded per sample it took twice the division's time on the M7,
   the guard moving each sum to a core register.) */
void biquad_stereo(struct biquad_state st[2], const struct biquad_coef *c, float *buf, int frames)
{
    const float b0 = c->b0, b1 = c->b1, b2 = c->b2, a0 = c->a0, a1 = c->a1, a2 = c->a2;
    float lx1 = st[0].x1, lx2 = st[0].x2, ly1 = st[0].y1, ly2 = st[0].y2;
    float rx1 = st[1].x1, rx2 = st[1].x2, ry1 = st[1].y1, ry2 = st[1].y2;
    if (a0 == 1.0f) {
        for (int f = 0; f < frames; f++) {
            float xl = buf[2 * f], xr = buf[2 * f + 1];
            float yl = BQ_SUM(xl, lx1, lx2, ly1, ly2), yr = BQ_SUM(xr, rx1, rx2, ry1, ry2);
            lx2 = lx1; lx1 = xl; ly2 = ly1; ly1 = yl;
            rx2 = rx1; rx1 = xr; ry2 = ry1; ry1 = yr;
            buf[2 * f] = yl;
            buf[2 * f + 1] = yr;
        }
    } else {
        for (int f = 0; f < frames; f++) {
            float xl = buf[2 * f], xr = buf[2 * f + 1];
            float yl = BQ_SUM(xl, lx1, lx2, ly1, ly2) / a0, yr = BQ_SUM(xr, rx1, rx2, ry1, ry2) / a0;
            lx2 = lx1; lx1 = xl; ly2 = ly1; ly1 = yl;
            rx2 = rx1; rx1 = xr; ry2 = ry1; ry1 = yr;
            buf[2 * f] = yl;
            buf[2 * f + 1] = yr;
        }
    }
    st[0] = (struct biquad_state){ lx1, lx2, ly1, ly2 };
    st[1] = (struct biquad_state){ rx1, rx2, ry1, ry2 };
}

/* the gain of the shelving / peaking designs: 10^(dB / 40) */
static float amplitude(float db)
{
    return fm_pow10(db * 0.025f);
}

void biquad_highpass(struct biquad_coef *c, float hz, float q)
{
    float w = hz * TWO_PI * INV_FS, alpha = sinf(w) / (q * 2.0f), cs = cosf(w);
    c->b0 = (cs + 1.0f) * 0.5f;
    c->b1 = -1.0f - cs;
    c->b2 = (cs + 1.0f) * 0.5f;
    c->a0 = alpha + 1.0f;
    c->a1 = cs * -2.0f;
    c->a2 = 1.0f - alpha;
}

void biquad_lowpass(struct biquad_coef *c, float hz, float q)
{
    float w = hz * TWO_PI * INV_FS, alpha = sinf(w) / (q * 2.0f), cs = cosf(w);
    c->b0 = (1.0f - cs) * 0.5f;
    c->b1 = 1.0f - cs;
    c->b2 = (1.0f - cs) * 0.5f;
    c->a0 = alpha + 1.0f;
    c->a1 = cs * -2.0f;
    c->a2 = 1.0f - alpha;
}

void biquad_peak(struct biquad_coef *c, float hz, float q, float db)   /* FUN_0003FBF4 */
{
    float a = amplitude(db), w = hz * TWO_PI * INV_FS, alpha = sinf(w) / (q * 2.0f), cs = cosf(w);
    c->b0 = alpha * a + 1.0f;
    c->b1 = cs * -2.0f;
    c->b2 = 1.0f - alpha * a;
    c->a0 = alpha / a + 1.0f;
    c->a1 = cs * -2.0f;
    c->a2 = 1.0f - alpha / a;
}

/* the shelves take sqrt(A) and the 2 sqrt(A) alpha terms in double precision, like FUN_000400B0 */
void biquad_low_shelf(struct biquad_coef *c, float hz, float q, float db)
{
    float a = amplitude(db), w = hz * TWO_PI * INV_FS, alpha = sinf(w) / (q * 2.0f), cs = cosf(w);
    double k = 2.0 * sqrt((double)a) * (double)alpha;
    float p = a + 1.0f, m = a - 1.0f;
    c->b0 = (float)(((double)(p - m * cs) + k) * a);
    c->b1 = (m - p * cs) * a * 2.0f;
    c->b2 = (float)(((double)(p - m * cs) - k) * a);
    c->a0 = (float)((double)(p + m * cs) + k);
    c->a1 = (m + p * cs) * -2.0f;
    c->a2 = (float)((double)(p + m * cs) - k);
}

void biquad_high_shelf(struct biquad_coef *c, float hz, float q, float db)   /* FUN_0003FEF4 */
{
    float a = amplitude(db), w = hz * TWO_PI * INV_FS, alpha = sinf(w) / (q * 2.0f), cs = cosf(w);
    double k = 2.0 * sqrt((double)a) * (double)alpha;
    float p = a + 1.0f, m = a - 1.0f;
    c->b0 = (float)(((double)(p + m * cs) + k) * a);
    c->b1 = (m + p * cs) * a * -2.0f;
    c->b2 = (float)(((double)(p + m * cs) - k) * a);
    c->a0 = (float)((double)(p - m * cs) + k);
    c->a1 = (m - p * cs) * 2.0f;
    c->a2 = (float)((double)(p - m * cs) - k);
}
