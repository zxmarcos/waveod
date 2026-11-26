/*
 * Spectrum of a signal for the analyser (docs/spectrum.md): the last SPEC_N samples through a Hann window and a
 * real FFT, in dB of the converters' full scale, on a logarithmic frequency axis of `cols` columns.
 *
 * Levels: a sine of the chain's full-scale amplitude (SPEC_FULL_SCALE, the converters' +-2^31) reads 0 dB at its
 * frequency; the floor is SPEC_FLOOR_DB. Columns: column c spans f_lo..f_hi on a log scale; a column wider than a
 * bin shows the loudest bin in it (a tone reads its level whatever the column's width), a narrower one the power
 * interpolated at its centre (the bass end, below ~200 Hz with 4096 points and ~460 columns).
 */
#pragma once

#include "dsp/rfft.h"

#define SPEC_N          4096                    /* points: 10.8 Hz bins at 44.1 kHz, a 93 ms window */
#define SPEC_MAX_COLS   480
#define SPEC_FLOOR_DB   (-120.0f)
#define SPEC_FULL_SCALE 2.18998f                /* the chain's float for the converters' full scale (2^31 / 980586112) */

struct spectrum {
    struct rfft fft;
    uint32_t tables[RFFT_MEM_WORDS(SPEC_N)];
    float win[SPEC_N];
    float x[SPEC_N], re[SPEC_N + 2];
    int cols;
    float f_lo, ratio, rate;                    /* the axis: f(c) = f_lo * ratio^c, c in 0..cols */
    uint16_t lo[SPEC_MAX_COLS], hi[SPEC_MAX_COLS];   /* the bins of a wide column (hi >= lo), else hi < lo */
    float at[SPEC_MAX_COLS];                    /* a narrow column: the fractional bin of its centre */
    float offset_db;                            /* power in dB -> dB of full scale */
};

/* Set up `cols` columns (<= SPEC_MAX_COLS) from f_lo to f_hi Hz at `rate`. 0, or -1 on bad arguments. */
int spectrum_init(struct spectrum *s, int cols, float f_lo, float f_hi, float rate);

/* The frequency at x (0..cols, fractional: x = c + 0.5 is column c's centre) and the x of a frequency. */
float spectrum_hz(const struct spectrum *s, float x);
float spectrum_x(const struct spectrum *s, float hz);

/* x = the last SPEC_N samples, oldest first -> db[cols] */
void spectrum_run(struct spectrum *s, const float *x, float *db);

/* 10 log10(v) for v > 0 without libm (error < 1e-4 dB); v <= 0 gives SPEC_FLOOR_DB */
float spectrum_db10(float v);
