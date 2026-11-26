#include "dsp/spectrum.h"
#include "dsp/fmath.h"
#include "platform/sdram_code.h"           /* last: main-loop code, SDRAM (dsp/rfft.c stays in the ITCM) */

#define TWO_PI_F 6.283185307f

float spectrum_db10(float v)
{
    return v > 0.0f ? 4.342944819f * logf(v) : SPEC_FLOOR_DB;
}

int spectrum_init(struct spectrum *s, int cols, float f_lo, float f_hi, float rate)
{
    if (cols < 2 || cols > SPEC_MAX_COLS || !(f_lo > 0.0f) || !(f_hi > f_lo) || f_hi > rate * 0.5f)
        return -1;
    if (rfft_init(&s->fft, SPEC_N, s->tables) != 0)
        return -1;
    float sum = 0.0f;
    for (int i = 0; i < SPEC_N; i++) {
        s->win[i] = 0.5f - 0.5f * cosf(TWO_PI_F * (float)i / (float)SPEC_N);
        sum += s->win[i];
    }
    /* a sine of amplitude A peaks at |X| = A sum(w) / 2: dB of full scale = 10 log10(|X|^2) + this */
    float k = 2.0f / (sum * SPEC_FULL_SCALE);
    s->offset_db = spectrum_db10(k * k);
    s->cols = cols;
    s->f_lo = f_lo;
    s->rate = rate;
    s->ratio = expf(logf(f_hi / f_lo) / (float)cols);
    const float bin = rate / (float)SPEC_N;
    for (int c = 0; c < cols; c++) {
        float a = spectrum_hz(s, (float)c) / bin, b = spectrum_hz(s, (float)c + 1.0f) / bin;
        int lo = (int)a + (a > (float)(int)a), hi = (int)b;         /* the bins inside [a, b] */
        if (hi > SPEC_N / 2)
            hi = SPEC_N / 2;
        s->lo[c] = (uint16_t)lo;
        s->hi[c] = (uint16_t)(hi >= lo ? hi : 0);
        if (hi < lo) {
            s->lo[c] = 1;                                         /* marks a narrow column (hi 0 < lo 1) */
            s->at[c] = spectrum_hz(s, (float)c + 0.5f) / bin;
        }
    }
    return 0;
}

float spectrum_hz(const struct spectrum *s, float x)
{
    return s->f_lo * expf(x * logf(s->ratio));
}

float spectrum_x(const struct spectrum *s, float hz)
{
    return hz > 0.0f ? logf(hz / s->f_lo) / logf(s->ratio) : 0.0f;
}

static float power(const float *re, int k)
{
    return re[2 * k] * re[2 * k] + re[2 * k + 1] * re[2 * k + 1];
}

void spectrum_run(struct spectrum *s, const float *x, float *db)
{
    for (int i = 0; i < SPEC_N; i++)
        s->x[i] = x[i] * s->win[i];
    rfft_forward(&s->fft, s->x, s->re);
    for (int c = 0; c < s->cols; c++) {
        float p;
        if (s->hi[c] >= s->lo[c]) {
            p = 0.0f;
            for (int k = s->lo[c]; k <= s->hi[c]; k++) {
                float q = power(s->re, k);
                if (q > p)
                    p = q;
            }
        } else {
            int k = (int)s->at[c];
            float t = s->at[c] - (float)k;
            p = power(s->re, k) * (1.0f - t) + power(s->re, k + 1 <= SPEC_N / 2 ? k + 1 : k) * t;
        }
        float v = spectrum_db10(p) + s->offset_db;
        db[c] = v > SPEC_FLOOR_DB ? v : SPEC_FLOOR_DB;
    }
}
