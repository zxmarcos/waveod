/*
 * NAM A2-Lite inference: the 3-channel A2 WaveNet of NeuralAmpModelerCore (NAM/wavenet/a2_fast.cpp, MIT
 * licence), as the Clone captures of waveod run it (engine/nam_block.c, docs/nam.md).
 *
 * The network: 23 dilated convolution layers of 3 channels (kernels 6 x 14, 15, 15, 6 x 7; dilations 1..239),
 * LeakyReLU 0.01, a 1x1 mixing per layer and a 16-tap head over the sum of the activations: 1871 weights,
 * ~1730 multiply-adds per sample. It is computed with fewer operations than the core's code (layer 0 is
 * rank 1 with its mix-in folded into a tap, one kernel per layer keeps 4 frames in registers from the first
 * tap to the residual, the 1x1 biases are folded into the convolution biases, the head's scale into its
 * weights) and differs from it by float rounding only (about -105..-120 dB).
 *
 * Every multiply-add is fused and comes in the same order on the M7 (VFMA, the kernels in assembly) and on
 * the PC (fmaf, the kernels in C): both builds give the same bits (tests/test_nam.c, tools/check_nam.py).
 *
 * An instance is a struct a2 (the six long histories, 67 KB: the state pool), the 16 short histories
 * (A2_LIN_FLOATS, 30 KB, all of it read and written in every block) and its derived weights (A2_DW_FLOATS,
 * read for every frame). The last two belong in the fastest memory there is: the module code pool in the
 * ITCM, which the D-cache's misses do not reach. Several instances may run; there is no other state.
 */
#pragma once

#include <stdint.h>

#define A2_C            3
#define A2_LAYERS       23
#define A2_HEAD_K       16
#define A2_NUM_WEIGHTS  1871                    /* packed as studio/waveod_studio/nam.py packs them */
#define A2_BLOCK        72                      /* largest n per call (the 48 kHz path gives up to 70) */
#define A2_LOOKBACK_SUM 6331                    /* sum over the layers of (kernel - 1) x dilation */

#define A2_B4      ((A2_BLOCK + 3) & ~3)        /* frames a block may compute: whole groups of 4 */
#define A2_SPARE   6                            /* columns after a history: a ring's first 3 again (a group
                                                   reads or writes across its end), a linear buffer's move
                                                   in whole steps of 16 floats */
#define A2_HEAD_LB (A2_HEAD_K - 1)
#define A2_X_LB    5                            /* layer 0: kernel 6, dilation 1 */
#define A2_LB1     (A2_LOOKBACK_SUM - A2_X_LB)  /* lookback of layers 1..22 */
#define A2_HIST_FLOATS ((A2_LB1 + (A2_LAYERS - 1) * (A2_B4 + A2_SPARE)) * A2_C)
/* the layers with a linear history (dilation <= 41 and the two K = 15 ones), and the sum of their lookbacks */
#define A2_LIN_LAYERS  16
#define A2_LIN_LB      1226
#define A2_LIN_FLOATS  ((A2_LIN_LB + A2_LIN_LAYERS * (A2_B4 + A2_SPARE)) * A2_C)
#define A2_RING_FLOATS (A2_HIST_FLOATS - A2_LIN_FLOATS)
/* derived weights: layer 0 b[3] x 4, u[6][3], l1[9], r[3]; layers 1..22 b[3] x 4, conv[K][9], m[3], l1[9];
   head[16][3] hb (head_scale folded in) */
#define A2_DW_FLOATS (42 + 9 * 150 + 24 * (A2_LAYERS - 1) + A2_HEAD_K * 3 + 1)

/* arguments of a layer's kernel; the field offsets are used by the assembly */
struct a2k_args {
    const float *w;         /*  0: the layer's derived weights */
    const float *cond;      /*  4: the input x of the group's first frame */
    float *hsum;            /*  8: head sums of the group's first frame */
    float *dst;             /* 12: the next layer's input column of the group's first frame; 0 for the last layer */
    int ngroups;            /* 16 */
    float slope;            /* 20: 0.01 */
    int k;                  /* 24 */
    const float *tap[15];   /* 28: per tap, the input column of the group's first frame */
};

/* one layer's input history and kernel (layers 1..22) */
struct a2_hist {
    float *h;                                   /* linear: lookback + A2_B4 columns; ring: ring; + A2_SPARE */
    void (*kern)(struct a2k_args *);
    int ring;                                   /* columns of the ring, 0 = linear */
    int cur;                                    /* ring: column of the block's first frame */
    int lb, k, d;                               /* lookback, kernel, dilation */
    int direct;                                 /* linear in, linear out: one call with `a` as set by a2_init */
                                                /* (linear in, ring out: all of `a` but dst and ngroups) */
    int move;                                   /* linear: floats moved down after the block (16 n >= 3 lb) */
    struct a2k_args a;
};

struct a2 {
    const float *w0, *head;                     /* layer 0's and the head's derived weights */
    struct a2_hist L[A2_LAYERS];                /* [0] unused: layer 0 reads x */
    float x[A2_X_LB + A2_B4];                   /* the input, 5 older frames first */
    float hh[(A2_HEAD_LB + A2_B4 + 1) * A2_C];  /* head sums, 15 older columns first (+ 1: moved by 16s) */
    float hist[A2_RING_FLOATS];                 /* the rings */
};

/* Set up from the packed weights (copied: they may live in a flash slot that changes) into `dw`
   (A2_DW_FLOATS, 8-byte aligned), the linear histories in `lin` (A2_LIN_FLOATS), every history at the steady
   state of a silent input (what the core's prewarm converges to). */
void a2_init(struct a2 *s, float *dw, float *lin, const float *weights);

/* n frames (1..A2_BLOCK); in and out may be the same buffer */
void a2_process(struct a2 *s, const float *in, float *out, int n);
