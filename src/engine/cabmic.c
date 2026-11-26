#include "engine/cabmic.h"
#include "dsp/fmath.h"
#include "dsp/rfft.h"
#include "platform/sdram.h"
#include <string.h>

#define N           2048                        /* FFT of the filter design and the convolution */
#define FS          44100.0f
#define IR_SCALE    1.1920928955078125e-07f
#define LN10_20     0.11512925f                 /* ln(10) / 20: dB -> natural log of the magnitude */
#define FADE        64                          /* samples faded out at the end of the new IR */
#define MAX_DB      18.0f

static struct rfft fft;
static uint32_t fft_mem[RFFT_MEM_WORDS(N)] SDRAM_BSS;
static float a[N + 2] SDRAM_BSS, b[N + 2] SDRAM_BSS, c[N + 2] SDRAM_BSS;
static int ready;

float cabmic_step(float knob, int steps)
{
    /* -100 -> 0, 0 -> the step of the IR as captured, +100 -> the last step */
    const float ref = CABMIC_REF;
    float s = knob < 0 ? ref + knob / 100.0f * ref : ref + knob / 100.0f * ((float)steps - 1.0f - ref);
    return s < 0 ? 0 : s > (float)(steps - 1) ? (float)(steps - 1) : s;
}

float cabmic_knob(float step, int steps)
{
    const float ref = CABMIC_REF;
    float k = step < ref ? (step - ref) / ref * 100.0f : (step - ref) / ((float)steps - 1.0f - ref) * 100.0f;
    return k < -100.0f ? -100.0f : k > 100.0f ? 100.0f : k;
}

static float row_at(const float (*rows)[CABMIC_BANDS], int steps, float s, int band)
{
    int i = (int)s;
    if (i >= steps - 1)
        return rows[steps - 1][band];
    float t = s - (float)i;
    return rows[i][band] * (1.0f - t) + rows[i + 1][band] * t;
}

int cabmic_type(const float *knobs)
{
    float t = knobs[CABMIC_KNOB_TYPE];
    int i = t > 0.0f ? (int)(t + 0.5f) : 0;
    return i > CABMIC_TYPES ? 0 : i;
}

int cabmic_neutral(const float *knobs)
{
    return knobs[CABMIC_KNOB_POSITION] == 0.0f && knobs[CABMIC_KNOB_DISTANCE] == 0.0f &&
           knobs[CABMIC_KNOB_ANGLE] == 0.0f && cabmic_type(knobs) == 0;
}

void cabmic_curve(const float *knobs, float db[CABMIC_BANDS])
{
    float sp = cabmic_step(knobs[CABMIC_KNOB_POSITION], CABMIC_POSITIONS);
    float sd = cabmic_step(knobs[CABMIC_KNOB_DISTANCE], CABMIC_DISTANCES);
    float an = knobs[CABMIC_KNOB_ANGLE] / 45.0f;
    an = an < 0 ? 0 : an > 1 ? 1 : an;
    int t = cabmic_type(knobs);
    const float (*pos)[CABMIC_BANDS] = t ? cabmic_type_position[t - 1] : cabmic_position;
    const float (*dist)[CABMIC_BANDS] = t ? cabmic_type_distance[t - 1] : cabmic_distance;
    const float (*ang)[CABMIC_BANDS] = t ? cabmic_type_angle[t - 1] : cabmic_angle;
    for (int k = 0; k < CABMIC_BANDS; k++) {
        float v = row_at(pos, CABMIC_POSITIONS, sp, k) - pos[CABMIC_REF][k] +
                  row_at(dist, CABMIC_DISTANCES, sd, k) - dist[CABMIC_REF][k] +
                  an * ang[1][k] + (t ? cabmic_type_curve[t - 1][k] : 0.0f);
        db[k] = v < -MAX_DB ? -MAX_DB : v > MAX_DB ? MAX_DB : v;
    }
}

float cabmic_db_at(const float db[CABMIC_BANDS], float hz)
{
    if (hz <= cabmic_bands[0])
        return db[0];
    if (hz >= cabmic_bands[CABMIC_BANDS - 1])
        return db[CABMIC_BANDS - 1];
    /* the bands are a third of an octave apart: position = 3 log2(hz / 25) */
    float pos = 3.0f * logf(hz / cabmic_bands[0]) * 1.44269504f;
    int i = (int)pos;
    if (i >= CABMIC_BANDS - 1)
        return db[CABMIC_BANDS - 1];
    float t = pos - (float)i;
    return db[i] * (1.0f - t) + db[i + 1] * t;
}

void cabmic_apply(const int32_t *ir, int n, const float db[CABMIC_BANDS], float *out)
{
    if (!ready) {
        rfft_init(&fft, N, fft_mem);
        ready = 1;
    }
    if (n > N / 2)
        n = N / 2;
    /* log magnitude of the change on the FFT bins (a real, even spectrum) -> real cepstrum */
    for (int k = 0; k <= N / 2; k++) {
        a[2 * k] = cabmic_db_at(db, (float)k * FS / (float)N) * LN10_20;
        a[2 * k + 1] = 0.0f;
    }
    rfft_inverse(&fft, a, b);
    /* fold onto positive quefrencies: the minimum-phase filter with that magnitude */
    for (int i = 1; i < N / 2; i++)
        b[i] *= 2.0f;
    for (int i = N / 2 + 1; i < N; i++)
        b[i] = 0.0f;
    rfft_forward(&fft, b, a);
    for (int k = 0; k <= N / 2; k++) {
        float m = expf(a[2 * k]), ph = a[2 * k + 1];
        a[2 * k] = m * cosf(ph);
        a[2 * k + 1] = m * sinf(ph);
    }
    /* the IR through it */
    for (int i = 0; i < N; i++)
        c[i] = i < n && ir ? (float)ir[i] * IR_SCALE : 0.0f;
    rfft_forward(&fft, c, b);
    for (int k = 0; k <= N / 2; k++) {
        float xr = b[2 * k], xi = b[2 * k + 1], hr = a[2 * k], hi = a[2 * k + 1];
        b[2 * k] = xr * hr - xi * hi;
        b[2 * k + 1] = xr * hi + xi * hr;
    }
    rfft_inverse(&fft, b, c);
    for (int i = 0; i < n; i++) {
        float g = 1.0f;
        if (i >= n - FADE)
            g = 0.5f + 0.5f * cosf(3.14159265f * (float)(i - (n - FADE)) / (float)FADE);
        out[i] = c[i] * g;
    }
}
