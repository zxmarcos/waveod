/* The Big Muff Pi from its circuit, as four DK stages (tools/circuits/bigmuff.py stages()):
 *   S1 = input network + Q4 (at 2x: the input booster rarely clips, 2x and 4x give the same output),
 *   S2 = sustain network + Q3 + its diodes, S3 = Q2 + its diodes (the tone stack as its load), both at 4x,
 *   S4 = tone stack + Q1 + volume, linear, at 1x.
 * Each stage is solved exactly; S1 and S2 load their collector with a copy of the next coupling network, ending on
 * the next base, whose voltage comes back from the next stage extrapolated from the last two samples (a weak loop).
 * Volts in, volts out (deviations from the operating point; the output is AC-coupled anyway).
 * Before including: sdk/wvdk.h, sdk/wvos.h, a generated bigmuff_<variant>.h, and BM_V defined as its prefix.
 *
 *   bm_init(&b); bm_build(&b, set, work, rot, rate);
 *   per block: b.set = dk_current(&cur); per 1x sample: y = bm_tail(&b, os4_run2(&os, x, bm_os_s1, bm_os_rest, 0, &b));
 */
#pragma once

#define BM_CAT_(a, b) a##b
#define BM_CAT(a, b) BM_CAT_(a, b)
DK_RUN(bm_s1, BM_CAT(BM_V, _S1))
DK_RUN(bm_s2, BM_CAT(BM_V, _S2))
DK_RUN(bm_s3, BM_CAT(BM_V, _S3))
DK_RUN(bm_s4, BM_CAT(BM_V, _S4))

typedef struct { dk_stage s1, s2, s3, s4; float b3, b3_1, b2, b2_1; unsigned set; } bigmuff_t;

static inline void bm_init(bigmuff_t *b)
{
    dk_init(&b->s1, &BM_CAT(BM_V, _S1)); dk_init(&b->s2, &BM_CAT(BM_V, _S2));
    dk_init(&b->s3, &BM_CAT(BM_V, _S3)); dk_init(&b->s4, &BM_CAT(BM_V, _S4));
    b->b3 = b->b3_1 = b->b2 = b->b2_1 = 0.0f;
    b->set = 0;
}

/* into matrix set `set`; rot: sustain, tone, volume (0..1); fs: the base rate (S1 at 2 fs, S2 and S3 at 4 fs, S4 at fs) */
static inline void bm_build(bigmuff_t *b, unsigned set, dk_work *w, const float *rot, float fs)
{
    dk_build(&b->s1, &b->s1.m[set], w, rot, 2.0f * fs); dk_build(&b->s2, &b->s2.m[set], w, rot, 4.0f * fs);
    dk_build(&b->s3, &b->s3.m[set], w, rot, 4.0f * fs); dk_build(&b->s4, &b->s4.m[set], w, rot, fs);
}

/* one 2x sample through S1: the input (volts) to Q4's collector */
static inline float bm_os_s1(void *ctx, float u)
{
    bigmuff_t *b = (bigmuff_t *)ctx;
    float i1[2] = { u, 2.0f * b->b3 - b->b3_1 }, c4;
    bm_s1(&b->s1, &b->s1.m[b->set], i1, &c4);
    b->b3_1 = b->b3;                        /* b3 at S1's rate: the newest value from S2 each time */
    return c4;
}

/* one 4x sample through S2 and S3: Q4's collector to Q2's */
static inline float bm_os_rest(void *ctx, float c4)
{
    bigmuff_t *b = (bigmuff_t *)ctx;
    float i2[2] = { c4, 2.0f * b->b2 - b->b2_1 }, o2[2];
    bm_s2(&b->s2, &b->s2.m[b->set], i2, o2);
    float o3[2];
    bm_s3(&b->s3, &b->s3.m[b->set], o2, o3);
    b->b3 = o2[1];
    b->b2_1 = b->b2; b->b2 = o3[1];
    return o3[0];
}

/* one 1x sample: Q2's collector (band-limited) to the output */
static inline float bm_tail(bigmuff_t *b, float c2)
{
    float y;
    bm_s4(&b->s4, &b->s4.m[b->set], &c2, &y);
    return y;
}
