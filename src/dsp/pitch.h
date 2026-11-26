/*
 * Monophonic pitch detection for the tuner: YIN (de Cheveigné & Kawahara, 2002) on a window of the guitar
 * input decimated to 22.05 kHz. Covers 52 Hz (below a drop-A low string) to 1.1 kHz.
 */
#pragma once

#define PITCH_RATE   22050.0f
#define PITCH_N      1024                   /* samples analysed per estimate (46 ms) */

struct pitch_result {
    float hz;                               /* 0 = no pitch */
    float clarity;                          /* 1 - YIN aperiodicity, 0..1 */
    float level;                            /* RMS of the window */
};

/* Analyse PITCH_N samples (oldest first). */
void pitch_detect(const float *x, struct pitch_result *out);

/* log2 for positive x, accurate to well under 0.01 cent (no libm on the pedal). */
float pitch_log2(float x);
