/*
 * The few math library functions the DSP code needs. The freestanding firmware has no C library:
 * board/qme200/libm.c provides these symbols.
 */
#pragma once

#ifdef WAVEOD_HOST
#include <math.h>
#else
float sinf(float x);
float cosf(float x);
float expf(float x);
float logf(float x);
double sqrt(double x);
#endif

/* 10^x */
static inline float fm_pow10(float x)
{
    return expf(x * 2.302585093f);
}
