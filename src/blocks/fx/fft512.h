/*
 * The original's 512-point real FFT (docs/stock_port.md): CMSIS-DSP's arm_rfft_fast_f32 as the Harmony D module
 * runs it, its operations in the module's order (the compiler fused some products into FMAs; which ones is
 * per line, read from the module). Tables in fft_tables.h (tools/gen_stock_fft.py).
 *
 *   fft512_forward(x, X): x (512 reals, overwritten) -> X (256 complex, X[1] = the Nyquist bin's real part), each
 *                         bin halved (CMSIS's scaling)
 *   fft512_inverse(X, x): X (256 complex, kept) -> x (512 reals): the split, then the 256-point complex inverse
 *                         scaled by 1 / 256
 *
 * The complex FFT: 256 points as radix 8 by 4 (one radix-4 pass over four 64-point columns, each column a 64-point
 * radix-8 FFT in two passes), then the bit reversal; the inverse conjugates before and scales by 1 / 256 after.
 */
#pragma once

#include "../amps/stock.h"
#include "fft_tables.h"

#define FFT_C81 0.7071067690849304f

/* one radix-8 butterfly without twiddles over the 8 points i, i + n, .. i + 7 n (complex indices) */
static inline void fft_r8(float *p, int i, int n)
{
    float *a[8];
    for (int m = 0; m < 8; m++)
        a[m] = p + 2 * (i + m * n);
    float r1 = a[0][0] + a[4][0], r5 = a[0][0] - a[4][0];
    float r2 = a[1][0] + a[5][0], r6 = a[1][0] - a[5][0];
    float r3 = a[2][0] + a[6][0], r7 = a[2][0] - a[6][0];
    float r4 = a[3][0] + a[7][0], r8 = a[3][0] - a[7][0];
    float t1 = r1 - r3;
    r1 = r1 + r3;
    r3 = r2 - r4;
    r2 = r2 + r4;
    a[0][0] = r1 + r2;
    a[4][0] = r1 - r2;
    r1 = a[0][1] + a[4][1];
    float s5 = a[0][1] - a[4][1];
    r2 = a[1][1] + a[5][1];
    float s6 = a[1][1] - a[5][1];
    float s3 = a[2][1] + a[6][1];
    float s7 = a[2][1] - a[6][1];
    r4 = a[3][1] + a[7][1];
    float s8 = a[3][1] - a[7][1];
    float t2 = r1 - s3;
    r1 = r1 + s3;
    s3 = r2 - r4;
    r2 = r2 + r4;
    a[0][1] = r1 + r2;
    a[4][1] = r1 - r2;
    a[2][0] = t1 + s3;
    a[6][0] = t1 - s3;
    a[2][1] = t2 - r3;
    a[6][1] = t2 + r3;
    r1 = (r6 - r8) * FFT_C81;
    r6 = (r6 + r8) * FFT_C81;
    r2 = (s6 - s8) * FFT_C81;
    s6 = (s6 + s8) * FFT_C81;
    t1 = r5 - r1;
    r5 = r5 + r1;
    r8 = r7 - r6;
    r7 = r7 + r6;
    t2 = s5 - r2;
    s5 = s5 + r2;
    s8 = s7 - s6;
    s7 = s7 + s6;
    a[1][0] = r5 + s7;
    a[7][0] = r5 - s7;
    a[5][0] = t1 + s8;
    a[3][0] = t1 - s8;
    a[1][1] = s5 - r7;
    a[7][1] = s5 + r7;
    a[5][1] = t2 - r8;
    a[3][1] = t2 + r8;
}

/* the same with the outputs 1 .. 7 rotated by the twiddles (co, si)[m - 1], each product pair fused as the module
   does: (re, im) x (co, -si) */
static inline void fft_r8_tw(float *p, int i, int n, const float *co, const float *si)
{
    float *a[8];
    for (int m = 0; m < 8; m++)
        a[m] = p + 2 * (i + m * n);
    float r1 = a[0][0] + a[4][0], r5 = a[0][0] - a[4][0];
    float r2 = a[1][0] + a[5][0], r6 = a[1][0] - a[5][0];
    float r3 = a[2][0] + a[6][0], r7 = a[2][0] - a[6][0];
    float r4 = a[3][0] + a[7][0], r8 = a[3][0] - a[7][0];
    float t1 = r1 - r3;
    r1 = r1 + r3;
    r3 = r2 - r4;
    r2 = r2 + r4;
    a[0][0] = r1 + r2;
    r2 = r1 - r2;
    float x1 = a[0][1] + a[4][1], s5 = a[0][1] - a[4][1];
    float x2 = a[1][1] + a[5][1], s6 = a[1][1] - a[5][1];
    float s3 = a[2][1] + a[6][1], s7 = a[2][1] - a[6][1];
    float x4 = a[3][1] + a[7][1], s8 = a[3][1] - a[7][1];
    float t2 = x1 - s3;
    x1 = x1 + s3;
    s3 = x2 - x4;
    x2 = x2 + x4;
    a[0][1] = x1 + x2;
    float s2 = x1 - x2;
    /* point 4: the sine term fused */
    a[4][0] = st_fma(s2, si[3], r2 * co[3]);
    a[4][1] = st_fma(s2, co[3], -(r2 * si[3]));
    r1 = t1 + s3;
    r2 = t1 - s3;
    float s1 = t2 - r3;
    s2 = t2 + r3;
    a[2][0] = st_fma(r1, co[1], s1 * si[1]);
    a[2][1] = st_fma(s1, co[1], -(r1 * si[1]));
    a[6][0] = st_fma(r2, co[5], s2 * si[5]);
    a[6][1] = st_fma(s2, co[5], -(r2 * si[5]));
    r1 = (r6 - r8) * FFT_C81;
    r6 = (r6 + r8) * FFT_C81;
    r2 = (s6 - s8) * FFT_C81;
    s6 = (s6 + s8) * FFT_C81;
    t1 = r5 - r1;
    r5 = r5 + r1;
    r8 = r7 - r6;
    r7 = r7 + r6;
    t2 = s5 - r2;
    s5 = s5 + r2;
    s8 = s7 - s6;
    s7 = s7 + s6;
    r1 = r5 + s7;
    r5 = r5 - s7;
    r6 = t1 + s8;
    t1 = t1 - s8;
    s1 = s5 - r7;
    s5 = s5 + r7;
    s6 = t2 - r8;
    t2 = t2 + r8;
    a[1][0] = st_fma(r1, co[0], s1 * si[0]);
    a[1][1] = st_fma(s1, co[0], -(r1 * si[0]));
    a[7][0] = st_fma(r5, co[6], s5 * si[6]);
    a[7][1] = st_fma(s5, co[6], -(r5 * si[6]));
    a[5][0] = st_fma(r6, co[4], s6 * si[4]);
    a[5][1] = st_fma(s6, co[4], -(r6 * si[4]));
    a[3][0] = st_fma(t1, co[2], t2 * si[2]);
    a[3][1] = st_fma(t2, co[2], -(t1 * si[2]));
}

/* a 64-point radix-8 FFT (natural order in, digit-reversed out), twiddles every 4th of the 256-point table */
static inline void fft_r8_64(float *p)
{
    fft_r8(p, 0, 8);
    for (int j = 1; j < 8; j++) {
        float co[7], si[7];
        for (int m = 0; m < 7; m++) {
            co[m] = FFT_TW256[2 * 4 * j * (m + 1)];
            si[m] = FFT_TW256[2 * 4 * j * (m + 1) + 1];
        }
        fft_r8_tw(p, j, 8, co, si);
    }
    for (int i = 0; i < 64; i += 8)
        fft_r8(p, i, 1);
}

/* the 256-point complex FFT in place (ifft: the inverse, scaled by 1 / 256) */
static inline void fft256(float *pp, int ifft)
{
    if (ifft)
        for (int i = 0; i < 256; i++)
            pp[2 * i + 1] = -pp[2 * i + 1];
    const float *tw = FFT_TW256;
    {   /* the first point, its twiddles 1 */
        float p1r = pp[0], p1i = pp[1], p2r = pp[128], p2i = pp[129];
        float p3r = pp[256], p3i = pp[257], p4r = pp[384], p4i = pp[385];
        pp[256] = (p3r + p1r) - (p2r + p4r);
        pp[257] = (p3i + p1i) - (p2i + p4i);
        pp[384] = ((p1r - p3r) - p2i) + p4i;
        pp[385] = (p2r + (p1i - p3i)) - p4r;
        pp[0] = (p2r + p4r) + (p3r + p1r);
        pp[1] = (p2i + p4i) + (p3i + p1i);
        pp[128] = (p2i + (p1r - p3r)) - p4i;
        pp[129] = ((p1i - p3i) - p2r) + p4r;
    }
    for (int k = 0; k < 32; k++) {
        int j = 2 * k + 2;              /* the top: points 1 .. 32 of each column */
        float p1r = pp[j], p1i = pp[j + 1], p2r = pp[j + 128], p2i = pp[j + 129];
        float p3r = pp[j + 256], p3i = pp[j + 257], p4r = pp[j + 384], p4i = pp[j + 385];
        float t2r = (p2i + (p1r - p3r)) - p4i, t2i = ((p1i - p3i) - p2r) + p4r;
        float t3r = (p3r + p1r) - (p2r + p4r), t3i = (p3i + p1i) - (p2i + p4i);
        float t4r = ((p1r - p3r) - p2i) + p4i, t4i = (p2r + (p1i - p3i)) - p4r;
        pp[j] = (p2r + p4r) + (p3r + p1r);
        pp[j + 1] = (p2i + (p3i + p1i)) + p4i;
        pp[j + 128] = st_fma(tw[2 * k + 3], t2i, tw[2 * k + 2] * t2r);
        pp[j + 129] = st_fma(tw[2 * k + 2], t2i, -(tw[2 * k + 3] * t2r));
        pp[j + 256] = st_fma(tw[4 * k + 5], t3i, tw[4 * k + 4] * t3r);
        pp[j + 257] = st_fma(tw[4 * k + 4], t3i, -(tw[4 * k + 5] * t3r));
        pp[j + 384] = st_fma(tw[6 * k + 7], t4i, tw[6 * k + 6] * t4r);
        pp[j + 385] = st_fma(tw[6 * k + 6], t4i, -(tw[6 * k + 7] * t4r));
        if (k == 31)
            break;
        int e = 126 - 2 * k;            /* the bottom: points 63 .. 33 */
        float q1r = pp[e], q1i = pp[e + 1], q2r = pp[e + 128], q2i = pp[e + 129];
        float q3r = pp[e + 256], q3i = pp[e + 257], q4r = pp[e + 384], q4i = pp[e + 385];
        float u2a = ((q1i - q3i) - q2r) + q4r, u2b = (q2i - q4i) + (q1r - q3r);
        float u3a = (q3i + q1i) - (q2i + q4i), u3b = (q3r + q1r) - (q2r + q4r);
        float u4a = q4r - ((q1i - q3i) + q2r), u4b = (q2i - q4i) - (q1r - q3r);
        pp[e] = (q2r + (q3r + q1r)) + q4r;
        pp[e + 1] = (q2i + q4i) + (q3i + q1i);
        pp[e + 129] = st_fma(tw[2 * k + 3], u2a, -(tw[2 * k + 2] * u2b));
        pp[e + 128] = st_fma(tw[2 * k + 3], u2b, tw[2 * k + 2] * u2a);
        pp[e + 257] = st_fma(-tw[4 * k + 4], u3a, -(tw[4 * k + 5] * u3b));
        pp[e + 256] = st_fma(tw[4 * k + 5], u3a, -(tw[4 * k + 4] * u3b));
        pp[e + 385] = st_fma(tw[6 * k + 7], u4a, -(tw[6 * k + 6] * u4b));
        pp[e + 384] = st_fma(tw[6 * k + 7], u4b, tw[6 * k + 6] * u4a);
    }
    for (int c = 0; c < 4; c++)
        fft_r8_64(pp + 128 * c);
    uint32_t *w = (uint32_t *)pp;
    for (int i = 0; i < 440; i += 2) {
        int a = FFT_BITREV256[i] >> 2, b = FFT_BITREV256[i + 1] >> 2;
        uint32_t t = w[a];
        w[a] = w[b];
        w[b] = t;
        t = w[a + 1];
        w[a + 1] = w[b + 1];
        w[b + 1] = t;
    }
    if (ifft) {
        float s = 1.0f / 256.0f, ns = -s;
        for (int i = 0; i < 256; i++) {
            pp[2 * i] = pp[2 * i] * s;
            pp[2 * i + 1] = pp[2 * i + 1] * ns;
        }
    }
}

static inline void fft512_forward(float *x, float *out)
{
    fft256(x, 0);
    const float *c = FFT_TW_RFFT512;
    float t1a = x[0] + x[0], t1b = x[1] + x[1];
    out[0] = (t1b + t1a) * 0.5f;
    out[1] = (t1a - t1b) * 0.5f;
    for (int i = 1; i < 256; i++) {
        float xar = x[2 * i], xai = x[2 * i + 1], xbr = x[512 - 2 * i], xbi = x[513 - 2 * i];
        float a = xbr - xar, b = xai + xbi;
        float re = st_fma(b, c[2 * i + 1], st_fma(a, c[2 * i], xar + xbr));
        float im = st_fma(a, c[2 * i + 1], xai - st_fma(b, c[2 * i], xbi));
        out[2 * i] = re * 0.5f;
        out[2 * i + 1] = im * 0.5f;
    }
}

static inline void fft512_inverse(const float *X, float *out)
{
    const float *c = FFT_TW_RFFT512;
    out[0] = (X[1] + X[0]) * 0.5f;
    out[1] = (X[0] - X[1]) * 0.5f;
    for (int i = 1; i < 256; i++) {
        float xar = X[2 * i], xai = X[2 * i + 1], xbr = X[512 - 2 * i], xbi = X[513 - 2 * i];
        float a = xar - xbr, b = xai + xbi;
        float re = (xar + xbr) - st_fma(b, c[2 * i + 1], a * c[2 * i]);
        float im = st_fma(a, c[2 * i + 1], xai - st_fma(b, c[2 * i], xbi));
        out[2 * i] = re * 0.5f;
        out[2 * i + 1] = im * 0.5f;
    }
    fft256(out, 1);
}
