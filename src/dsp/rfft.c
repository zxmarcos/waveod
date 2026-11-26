#include "dsp/rfft.h"
#include "dsp/fmath.h"

#define PI 3.14159265358979323846

int rfft_init(struct rfft *f, int n, uint32_t *mem)
{
    if (n < 8 || n > RFFT_MAX || (n & (n - 1)))
        return -1;
    int m = n / 2, bits = 0;
    while ((1 << bits) < m)
        bits++;
    float *w = (float *)mem, *ws = w + m / 2 * 2;
    uint16_t *rev = (uint16_t *)(ws + m * 2);
    for (int k = 0; k < m / 2; k++) {
        double a = -2.0 * PI * k / m;
        w[2 * k] = cosf((float)a);
        w[2 * k + 1] = sinf((float)a);
    }
    for (int k = 0; k < m; k++) {
        double a = -2.0 * PI * k / n;
        ws[2 * k] = cosf((float)a);
        ws[2 * k + 1] = sinf((float)a);
        int r = 0;
        for (int b = 0; b < bits; b++)
            r |= ((k >> b) & 1) << (bits - 1 - b);
        rev[k] = (uint16_t)r;
    }
    f->n = n;
    f->w = w;
    f->ws = ws;
    f->rev = rev;
    return 0;
}

/* in-place complex FFT of m points (interleaved), bit-reversed order already applied */
static void butterflies(const struct rfft *f, float *z, int m)
{
    for (int len = 2; len <= m; len <<= 1) {
        int half = len >> 1, step = m / len;
        for (int i = 0; i < m; i += len) {
            float *a = z + 2 * i, *b = a + 2 * half;
            for (int j = 0; j < half; j++) {
                float wr = f->w[2 * j * step], wi = f->w[2 * j * step + 1];
                float br = b[2 * j] * wr - b[2 * j + 1] * wi;
                float bi = b[2 * j] * wi + b[2 * j + 1] * wr;
                float ar = a[2 * j], ai = a[2 * j + 1];
                a[2 * j] = ar + br;
                a[2 * j + 1] = ai + bi;
                b[2 * j] = ar - br;
                b[2 * j + 1] = ai - bi;
            }
        }
    }
}

void rfft_forward(const struct rfft *f, const float *in, float *out)
{
    int m = f->n / 2;
    /* z[k] = in[2k] + i in[2k + 1], in bit-reversed order */
    for (int k = 0; k < m; k++) {
        int r = f->rev[k];
        out[2 * r] = in[2 * k];
        out[2 * r + 1] = in[2 * k + 1];
    }
    butterflies(f, out, m);
    /* split: X[k] = E + W^k O, X[m - k] = conj(E - W^k O), E = (Z[k] + conj Z[m-k]) / 2, O = -i (Z[k] - conj Z[m-k]) / 2 */
    float z0r = out[0], z0i = out[1];
    for (int k = 1; k <= m / 2; k++) {
        float *p = out + 2 * k, *q = out + 2 * (m - k);
        float zr = p[0], zi = p[1], cr = q[0], ci = -q[1];        /* Z[k], conj Z[m - k] */
        float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
        float dr = zr - cr, di = zi - ci;
        float or_ = 0.5f * di, oi = -0.5f * dr;                    /* -i d / 2 */
        float wr = f->ws[2 * k], wi = f->ws[2 * k + 1];
        float tr = wr * or_ - wi * oi, ti = wr * oi + wi * or_;
        p[0] = er + tr;
        p[1] = ei + ti;
        q[0] = er - tr;                                            /* conj(E - W O) */
        q[1] = -(ei - ti);
    }
    out[0] = z0r + z0i;
    out[1] = 0.0f;
    out[2 * m] = z0r - z0i;
    out[2 * m + 1] = 0.0f;
}

void rfft_inverse(const struct rfft *f, const float *in, float *out)
{
    int m = f->n / 2;
    float z[2];
    /* Z[k] = E + i O, E = (X[k] + conj X[m-k]) / 2, O = (X[k] - conj X[m-k]) conj(W^k) / 2; Z[m-k] = conj E + i conj O.
       Conjugated on the way (the inverse as conj(FFT(conj Z))) and written bit-reversed. */
    for (int k = 0; k <= m / 2; k++) {
        const float *p = in + 2 * k, *q = in + 2 * (m - k);
        float xr = p[0], xi = p[1], cr = q[0], ci = -q[1];        /* X[k], conj X[m - k] */
        float er = 0.5f * (xr + cr), ei = 0.5f * (xi + ci);
        float dr = 0.5f * (xr - cr), di = 0.5f * (xi - ci);
        float wr = f->ws[2 * k], wi = -f->ws[2 * k + 1];           /* conj W^k */
        float or_ = dr * wr - di * wi, oi = dr * wi + di * wr;
        z[0] = er - oi;                                            /* E + i O */
        z[1] = ei + or_;
        int r = f->rev[k];
        out[2 * r] = z[0];
        out[2 * r + 1] = -z[1];
        if (k && k != m - k) {
            float zr2 = er + oi, zi2 = -ei + or_;                  /* conj E + i conj O */
            int r2 = f->rev[m - k];
            out[2 * r2] = zr2;
            out[2 * r2 + 1] = -zi2;
        }
    }
    butterflies(f, out, m);
    float s = 1.0f / (float)m;
    for (int k = 0; k < m; k++) {
        out[2 * k] *= s;
        out[2 * k + 1] *= -s;
    }
}
