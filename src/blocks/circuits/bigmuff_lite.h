/* The Big Muff Pi, Lite model (tools/circuits/bigmuff.py stages_lite): the same circuit as bigmuff.h with cheaper
 * cuts. Each stage is solved exactly; no coupling back between the stages. S1 = input network + Q4 (linear: at 1x,
 * ahead of the oversampling), S2 = sustain network + Q3 + its diodes, S3 = Q2 + its diodes, both at 4x with their
 * diode solves tabulated per pot setting; S4 = tone stack + Q1 + volume at 1x.
 * Before including: sdk/wvdk.h, sdk/wvos.h, a generated bigmuff_<variant>_lite.h, and BML_V defined as its prefix. */
#pragma once

#define BML_CAT_(a, b) a##b
#define BML_CAT(a, b) BML_CAT_(a, b)
DK_RUN(bml_s1, BML_CAT(BML_V, _S1))
DK_RUN(bml_s2, BML_CAT(BML_V, _S2))
DK_RUN(bml_s3, BML_CAT(BML_V, _S3))
DK_RUN(bml_s4, BML_CAT(BML_V, _S4))

#define BML_NTAB 512                    /* the clipping stages' diode solves, tabulated */
#define BML_QMAX 6.0f                   /* volts of q covered by the tables (beyond: the last slope) */
#define BML_OS   4                      /* the clipping stages' oversampling */

#if BML_Q4_NONLINEAR || BML_CAT(BML_V, _S2_NP) != 1
#error "the Lite block runs the generated cut l2 (linear Q4 and Q3)"
#endif

typedef struct {
    dk_stage s1, s2, s3, s4;
    float tab2[2][BML_NTAB + 1], tab3[2][BML_NTAB + 1];    /* per matrix set */
    unsigned set;
} bigmuff_lite_t;

static inline void bml_init(bigmuff_lite_t *b)
{
    dk_init(&b->s1, &BML_CAT(BML_V, _S1)); dk_init(&b->s2, &BML_CAT(BML_V, _S2));
    dk_init(&b->s3, &BML_CAT(BML_V, _S3)); dk_init(&b->s4, &BML_CAT(BML_V, _S4));
    b->set = 0;
}

/* into matrix set `set`; rot: sustain, tone, volume (0..1); fs: the base rate */
static inline void bml_build(bigmuff_lite_t *b, unsigned set, dk_work *w, const float *rot, float fs)
{
    dk_build(&b->s1, &b->s1.m[set], w, rot, fs);
    dk_build(&b->s2, &b->s2.m[set], w, rot, (float)BML_OS * fs);
    dk_build(&b->s3, &b->s3.m[set], w, rot, (float)BML_OS * fs);
    dk_build(&b->s4, &b->s4.m[set], w, rot, fs);
    dk_tab_build(&b->s2, &b->s2.m[set], b->tab2[set], BML_NTAB, BML_QMAX);
    dk_tab_build(&b->s3, &b->s3.m[set], b->tab3[set], BML_NTAB, BML_QMAX);
}

/* the input (volts) to Q4's collector, at 1x */
static inline float bml_s1_run(bigmuff_lite_t *b, float u)
{
    float c4;
    bml_s1(&b->s1, &b->s1.m[b->set], &u, &c4);
    return c4;
}

/* one 4x sample through S2 and S3: Q4's collector to Q2's */
static inline float bml_os_rest(void *ctx, float c4)
{
    bigmuff_lite_t *b = (bigmuff_lite_t *)ctx;
    float c3, c2;
    bml_s2(&b->s2, &b->s2.m[b->set], &c4, &c3);
    bml_s3(&b->s3, &b->s3.m[b->set], &c3, &c2);
    return c2;
}

/* one 1x sample: Q2's collector (band-limited) to the output */
static inline float bml_tail(bigmuff_lite_t *b, float c2)
{
    float y;
    bml_s4(&b->s4, &b->s4.m[b->set], &c2, &y);
    return y;
}
