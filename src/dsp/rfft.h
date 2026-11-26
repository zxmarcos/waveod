/*
 * Real FFT of n = 2^k points (8..RFFT_MAX), computed as an n/2-point complex FFT (radix 2, decimation in time)
 * and the split step that separates the spectra of the even and the odd samples.
 *
 * Spectrum layout: the n/2 + 1 bins 0..n/2 as interleaved {re, im} (n + 2 floats); bins 0 and n/2 have im 0.
 * rfft_inverse(rfft_forward(x)) == x (the inverse carries the 1 / n).
 */
#pragma once

#include <stdint.h>

#define RFFT_MAX 4096                           /* the spectrum analyser's (dsp/spectrum.h) */

struct rfft {
    int n;                      /* real points */
    const float *w;             /* n/4 complex: e^(-2 pi i k / (n/2)), the half-size FFT's twiddles */
    const float *ws;            /* n/2 complex: e^(-2 pi i k / n), the split step's */
    const uint16_t *rev;        /* n/2: bit reversal of the half-size FFT */
};

/* Words (4 bytes) of memory the tables of an n-point FFT take. */
#define RFFT_MEM_WORDS(n) ((n) / 2 + (n) + (n) / 4)

/* Fill the tables in `mem` (RFFT_MEM_WORDS(n) words). 0, or -1 when n is not a power of two in range. */
int rfft_init(struct rfft *f, int n, uint32_t *mem);

/* in: n reals -> out: n + 2 floats (out may not alias in) */
void rfft_forward(const struct rfft *f, const float *in, float *out);

/* in: n + 2 floats (the spectrum, left intact) -> out: n reals (out may not alias in) */
void rfft_inverse(const struct rfft *f, const float *in, float *out);
