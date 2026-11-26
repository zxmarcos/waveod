/*
 * The fixed filters around the effect chain (V1.2.2 coefficients at 0x47040 / 0x47058): the guitar and the FX
 * return go through H(z), the main outputs and the FX send through 1 / H(z), about -13 dB at the top of the band
 * and back. The stock effects were made for the signal in between, so waveod keeps both.
 */
#pragma once

#include "dsp/biquad.h"

extern const struct biquad_coef io_filter_in, io_filter_out;