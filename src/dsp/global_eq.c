#include "dsp/global_eq.h"
#include "dsp/fmath.h"
#include <string.h>

#define LEVEL_STEP 1e-4f            /* DAT_00029ED8 */

enum kind { LOW_CUT, LOW_SHELF, PEAK, HIGH_SHELF, HIGH_CUT };

/* first parameter (the switch) of each stage, in processing order */
static const struct {
    uint8_t first;
    uint8_t kind;
} stages[GEQ_STAGES] = {
    { EQ_LOCUT_ON, LOW_CUT }, { EQ_LOSHELF_ON, LOW_SHELF }, { EQ_B1_ON, PEAK }, { EQ_B2_ON, PEAK },
    { EQ_B3_ON, PEAK },       { EQ_B4_ON, PEAK },           { EQ_HISHELF_ON, HIGH_SHELF }, { EQ_HICUT_ON, HIGH_CUT },
};

static int has_gain(int kind)
{
    return kind == LOW_SHELF || kind == PEAK || kind == HIGH_SHELF;
}

static void design(struct biquad_coef *c, int kind, const float *p)
{
    float hz = p[1], q = p[2], db = has_gain(kind) ? p[3] : 0.0f;
    switch (kind) {
    case LOW_CUT:    biquad_highpass(c, hz, q); break;
    case LOW_SHELF:  biquad_low_shelf(c, hz, q, db); break;
    case PEAK:       biquad_peak(c, hz, q, db); break;
    case HIGH_SHELF: biquad_high_shelf(c, hz, q, db); break;
    default:         biquad_lowpass(c, hz, q); break;
    }
}

void global_eq_init(struct global_eq *eq)
{
    memset(eq, 0, sizeof *eq);
}

static void update(struct global_eq *eq, const float *params)
{
    for (int s = 0; s < GEQ_STAGES; s++) {
        const float *p = params + stages[s].first;
        float *seen = eq->seen + stages[s].first;
        int n = has_gain(stages[s].kind) ? 3 : 2;            /* frequency, Q (, gain) after the switch */
        if (eq->valid && memcmp(seen + 1, p + 1, (size_t)n * sizeof *p) == 0)
            continue;
        memcpy(seen + 1, p + 1, (size_t)n * sizeof *p);
        design(&eq->coef[s], stages[s].kind, p);
    }
    eq->valid = 1;
}

void global_eq_process(struct global_eq *eq, float *io, int frames, const float params[EQ_PARAMS], int on)
{
    update(eq, params);
    if (!on)
        return;
    /* stage by stage over the block (each sample through the stages in the same order as sample by sample; the
       switches are read once per block), then the level */
    for (int s = 0; s < GEQ_STAGES; s++)
        if (params[stages[s].first] != 0.0f)
            biquad_stereo(eq->state[s], &eq->coef[s], io, frames);
    float target = params[EQ_LEVEL] * 0.01f, lv = eq->level;
    for (int f = 0; f < frames; f++) {
        if (lv < target)
            lv = lv + LEVEL_STEP > target ? target : lv + LEVEL_STEP;
        else if (lv > target)
            lv = lv - LEVEL_STEP < target ? target : lv - LEVEL_STEP;
        io[2 * f] *= lv * 2.0f;
        io[2 * f + 1] *= lv * 2.0f;
    }
    eq->level = lv;
}

/* 10 log10(r) without a math library log: mantissa / exponent, ln m = 2 atanh((m - 1) / (m + 1)) */
static float power_db(float r)
{
    if (!(r > 1e-12f))
        return -120.0f;
    int e = 0;
    while (r >= 2.0f) {
        r *= 0.5f;
        e++;
    }
    while (r < 1.0f) {
        r *= 2.0f;
        e--;
    }
    float t = (r - 1.0f) / (r + 1.0f), t2 = t * t;
    float ln = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (0.2f + t2 / 7.0f))) + (float)e * 0.69314718f;
    return 4.3429448f * ln;
}

/* |H(e^jw)|^2 of one biquad, in dB */
static float magnitude_db(const struct biquad_coef *c, float w)
{
    float c1 = cosf(w), c2 = cosf(2.0f * w), s1 = sinf(w), s2 = sinf(2.0f * w);
    float nr = c->b0 + c->b1 * c1 + c->b2 * c2, ni = -(c->b1 * s1 + c->b2 * s2);
    float dr = c->a0 + c->a1 * c1 + c->a2 * c2, di = -(c->a1 * s1 + c->a2 * s2);
    float den = dr * dr + di * di;
    return den > 0.0f ? power_db((nr * nr + ni * ni) / den) : 0.0f;
}

float global_eq_response_db(const float params[EQ_PARAMS], float hz)
{
    float w = hz * 6.2831854820251465f * 2.2675736545352265e-05f;
    float level = params[EQ_LEVEL] * 0.02f, db = power_db(level * level);
    for (int s = 0; s < GEQ_STAGES; s++) {
        const float *p = params + stages[s].first;
        if (p[0] == 0.0f)
            continue;
        struct biquad_coef c;
        design(&c, stages[s].kind, p);
        db += magnitude_db(&c, w);
    }
    return db;
}