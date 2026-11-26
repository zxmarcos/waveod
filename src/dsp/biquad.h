/*
 * Stereo biquad in the original's form (FUN_00023C9C / FUN_00023BB4 / the global EQ stages): direct form I on
 * interleaved L / R, with the a0 division done per sample:
 *
 *   y = (b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2) / a0
 */
#pragma once

struct biquad_coef {
    float b0, b1, b2, a0, a1, a2;
};

struct biquad_state {
    float x1, x2, y1, y2;           /* per channel */
};

/* Filter `frames` interleaved stereo frames in place; st[0] = L, st[1] = R. */
void biquad_stereo(struct biquad_state st[2], const struct biquad_coef *c, float *buf, int frames);

/* Audio EQ Cookbook designs, computed like the original's global EQ (fs = 44.1 kHz, gain in dB). */
void biquad_highpass(struct biquad_coef *c, float hz, float q);
void biquad_lowpass(struct biquad_coef *c, float hz, float q);
void biquad_peak(struct biquad_coef *c, float hz, float q, float db);
void biquad_low_shelf(struct biquad_coef *c, float hz, float q, float db);
void biquad_high_shelf(struct biquad_coef *c, float hz, float q, float db);
