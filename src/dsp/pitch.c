#include "dsp/pitch.h"
#include <stdint.h>
#include <string.h>

#define WINDOW     512                      /* integration window */
#define TAU_MIN    20                       /* 1.1 kHz */
#define TAU_MAX    424                      /* 52 Hz; WINDOW + TAU_MAX + 1 <= PITCH_N */
#define THRESHOLD  0.15f                    /* first dip of the normalised difference below this */
#define MIN_LEVEL  0.0005f                  /* RMS below this is silence */

float pitch_log2(float x)
{
    union { float f; uint32_t u; } v = { x };
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;            /* mantissa in [1, 2) */
    float m = v.f;
    if (m > 1.41421356f) {                              /* centre the range on 1 for faster convergence */
        m *= 0.5f;
        e++;
    }
    /* ln(m) = 2 atanh(t), t = (m - 1) / (m + 1), |t| < 0.172 */
    float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    float ln = 2.0f * t * (1.0f + t2 * (1.0f / 3 + t2 * (1.0f / 5 + t2 * (1.0f / 7 + t2 * (1.0f / 9)))));
    return (float)e + ln * 1.44269504f;
}

void pitch_detect(const float *x, struct pitch_result *out)
{
    static float d[TAU_MAX + 2];
    memset(out, 0, sizeof *out);

    float energy = 0;
    for (int j = 0; j < PITCH_N; j++)
        energy += x[j] * x[j];
    out->level = energy / PITCH_N;
    if (out->level < MIN_LEVEL * MIN_LEVEL)
        return;
    /* out->level holds the mean square until here */
    float ms = out->level;
    out->level = 0;
    for (int i = 0; i < 20; i++)                        /* sqrt by Newton, enough for a meter */
        out->level = i ? 0.5f * (out->level + ms / out->level) : ms > 1 ? ms : 1;

    /* difference function and its cumulative mean normalised form */
    d[0] = 1.0f;
    float running = 0;
    for (int tau = 1; tau <= TAU_MAX + 1; tau++) {
        float sum = 0;
        for (int j = 0; j < WINDOW; j++) {
            float diff = x[j] - x[j + tau];
            sum += diff * diff;
        }
        running += sum;
        d[tau] = running > 0 ? sum * (float)tau / running : 1.0f;
    }

    /* first dip under the threshold (then down to its bottom), else the global minimum */
    int best = -1;
    for (int tau = TAU_MIN; tau <= TAU_MAX; tau++) {
        if (d[tau] < THRESHOLD) {
            while (tau + 1 <= TAU_MAX && d[tau + 1] < d[tau])
                tau++;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = TAU_MIN;
        for (int tau = TAU_MIN + 1; tau <= TAU_MAX; tau++)
            if (d[tau] < d[best])
                best = tau;
    }
    out->clarity = 1.0f - d[best];
    if (out->clarity < 0)
        out->clarity = 0;

    /* the period with sub-sample precision: measured over k whole periods (as many as the analysed range
       holds), where the parabolic fit of the dip is k times finer, then divided by k */
    int k = (TAU_MAX - 1) / best;
    if (k < 1)
        k = 1;
    int centre = best * k, lo = centre - k / 2 - 1, hi = centre + k / 2 + 1;
    if (lo < 2)
        lo = 2;
    if (hi > TAU_MAX)
        hi = TAU_MAX;
    int m = centre > hi ? hi : centre;
    for (int tau = lo; tau <= hi; tau++)
        if (d[tau] < d[m])
            m = tau;
    float tau = (float)m;
    float a = d[m - 1], b = d[m], c = d[m + 1];
    float den = a - 2 * b + c;
    if (den > 0)
        tau += 0.5f * (a - c) / den;
    out->hz = PITCH_RATE * (float)k / tau;
}