/*
 * The original Clone's FFTs (docs/stock_port.md): CMSIS-DSP's 128-point complex FFT (arm_cfft_f32: radix 8 by 2,
 * two 64-point radix-8 FFTs, the bit reversal; the inverse conjugates before and scales by 1 / 128 after) and its
 * 128-point real FFT (forward only: a 64-point complex FFT and the real split). The library is the one every
 * module of the original links (its radix-8 butterfly is the same code in Harmony D, the Cab and the User IR), so
 * the butterflies are fft512.h's; the radix 8 by 2 pass is read from the Clone's module. Tables in clone_tables.h
 * (tools/gen_clone_tables.py).
 *
 *   clone_cfft128(p, ifft)   p: 128 complex in place, natural order in and out
 *   clone_rfft128(x, X)      x: 128 reals (overwritten) -> X: 64 complex, X[1] = the Nyquist bin's real part, each
 *                            bin halved (CMSIS's scaling)
 */
#pragma once

#include "../fx/fft512.h"
#include "clone_tables.h"

/* a 64-point radix-8 FFT (natural order in, digit-reversed out), twiddles every `step`-th of the table `tw` */
static inline void clone_r8_64(float *p, const float *tw, int step)
{
    fft_r8(p, 0, 8);
    for (int j = 1; j < 8; j++) {
        float co[7], si[7];
        for (int m = 0; m < 7; m++) {
            co[m] = tw[2 * step * j * (m + 1)];
            si[m] = tw[2 * step * j * (m + 1) + 1];
        }
        fft_r8_tw(p, j, 8, co, si);
    }
    for (int i = 0; i < 64; i += 8)
        fft_r8(p, i, 1);
}

/* the swaps of a bit reversal table (pairs of byte offsets, 8 per complex point) */
static inline void clone_bitrev(float *pp, const uint16_t *t, int n)
{
    uint32_t *w = (uint32_t *)pp;
    for (int i = 0; i < n; i += 2) {
        int a = t[i] >> 2, b = t[i + 1] >> 2;
        uint32_t x = w[a];
        w[a] = w[b];
        w[b] = x;
        x = w[a + 1];
        w[a + 1] = w[b + 1];
        w[b + 1] = x;
    }
}

static inline void clone_cfft128(float *pp, int ifft)
{
    if (ifft)
        for (int i = 0; i < 128; i++)
            pp[2 * i + 1] = -pp[2 * i + 1];
    /* radix 8 by 2: columns p1 (points 0..31), p2 (64..95), the middles m1 (32..63), m2 (96..127), two points of
       each per step; p2 and m2 get the differences rotated by the twiddles 2k, 2k + 1 */
    for (int k = 0; k < 16; k++) {
        float *p1 = pp + 4 * k, *p2 = pp + 128 + 4 * k, *m1 = pp + 64 + 4 * k, *m2 = pp + 192 + 4 * k;
        const float *tw = CLONE_TW128 + 4 * k;
        float a[4], b[4];
        for (int i = 0; i < 4; i++) {
            float x1 = p1[i], x2 = p2[i], y1 = m1[i], y2 = m2[i];
            p1[i] = x2 + x1;
            m1[i] = y2 + y1;
            a[i] = x1 - x2;
            b[i] = y2 - y1;
        }
        for (int h = 0; h < 2; h++) {
            float tr = tw[2 * h], ti = tw[2 * h + 1];
            float a0 = a[2 * h], a1 = a[2 * h + 1], b0 = b[2 * h], b1 = b[2 * h + 1];
            p2[2 * h] = st_fma(ti, a1, tr * a0);
            p2[2 * h + 1] = st_fma(tr, a1, -(ti * a0));
            m2[2 * h] = st_fma(ti, b0, -(tr * b1));
            m2[2 * h + 1] = st_fma(ti, b1, tr * b0);
        }
    }
    clone_r8_64(pp, CLONE_TW128, 2);
    clone_r8_64(pp + 128, CLONE_TW128, 2);
    clone_bitrev(pp, CLONE_BITREV128, 208);
    if (ifft) {
        float s = 1.0f / 128.0f, ns = -s;
        for (int i = 0; i < 128; i++) {
            pp[2 * i] = pp[2 * i] * s;
            pp[2 * i + 1] = pp[2 * i + 1] * ns;
        }
    }
}

static inline void clone_rfft128(float *x, float *out)
{
    clone_r8_64(x, CLONE_TW64, 1);
    clone_bitrev(x, CLONE_BITREV64, 56);
    const float *c = CLONE_TW_RFFT128;
    float t1a = x[0] + x[0], t1b = x[1] + x[1];
    out[0] = (t1b + t1a) * 0.5f;
    out[1] = (t1a - t1b) * 0.5f;
    for (int i = 1; i < 64; i++) {
        float xar = x[2 * i], xai = x[2 * i + 1], xbr = x[128 - 2 * i], xbi = x[129 - 2 * i];
        float a = xbr - xar, b = xai + xbi;
        float re = st_fma(b, c[2 * i + 1], st_fma(a, c[2 * i], xar + xbr));
        float im = st_fma(a, c[2 * i + 1], xai - st_fma(b, c[2 * i], xbi));
        out[2 * i] = re * 0.5f;
        out[2 * i + 1] = im * 0.5f;
    }
}
