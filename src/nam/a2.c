/*
 * NAM A2-Lite inference (nam/a2.h).
 *
 * - One kernel per layer runs groups of 4 frames with the accumulators in registers from the first tap to the
 *   residual: no partial sums go back to memory and there is no separate post pass.
 * - Layer 0 sees a rank-1 input (rechannel x the input): each tap is a 3-vector on x, 3 multiply-adds instead
 *   of 9, and its mix-in (also on x) is added to the newest tap's vector.
 * - The 1x1 biases are not added: the residual stream is kept without what they have added so far (B, a
 *   constant per layer), and each layer's convolution bias takes its taps' sum times B instead. The residual
 *   is one chain of multiply-adds seeded with s. (Against NAM's own code this moves the rounding by a few dB,
 *   around -110 dB.)
 * - head_scale is folded into the head weights.
 *
 * Histories: layer l >= 1 keeps its input columns. A block runs ceil(n / 4) groups of 4 frames (up to 3 extra
 * frames, computed from stale but finite data and written only where nothing is read later).
 * - Layers with a lookback up to 205 columns (dilation <= 41, and the two K = 15 layers) use a linear buffer
 *   (lookback + A2_B4 columns) whose last lookback columns move to the front as soon as the layer has run:
 *   one kernel call, no wrap. All of these buffers is read and written in every block; they are the caller's
 *   (`lin`), to put in memory the D-cache does not have to fetch.
 * - The dilation 101 and 239 layers use a ring (lookback + A2_B4 columns) plus spare columns after its end,
 *   so a group can read or write across the end: before the layer reads, the first 3 columns are copied
 *   after the end, and what its writer put past the end is copied back to the start. The layer runs in one
 *   call when nothing reaches an end in the block, otherwise in segments cut only where a tap pointer (or its
 *   writer's pointer) passes the end, at group boundaries.
 */
#include "nam/a2.h"
#include "platform/sdram.h"

#ifdef __clang__
#pragma clang fp contract(off)
#endif

#if defined(__arm__)
#define A2_M7 1                                 /* the kernels in assembly */
#endif

#define LIN_MAX 205                             /* longest lookback kept in a linear buffer */
#define PLD_MIN 41                              /* dilations whose older taps read what the cache has lost */

static const unsigned char a2_kernel[A2_LAYERS] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
static const unsigned char a2_dilation[A2_LAYERS] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239,
                                                     1, 13, 1, 3, 7, 17, 41, 101, 239};

_Static_assert(A2_LIN_LB == 3 * (5 + 15 + 35 + 85 + 205) - 5 + 14 * (1 + 13) && A2_LIN_LAYERS == 3 * 5 - 1 + 2,
               "the lookbacks up to LIN_MAX: three sets of dilations 1..41 without layer 0, the two K = 15 layers");
_Static_assert(A2_SPARE * A2_C >= 15, "a linear buffer's move may read 15 floats past its columns");

static inline float fmadd(float a, float b, float c)    /* a * b + c, rounded once */
{
#ifdef A2_M7
    __asm__("vfma.f32 %0, %1, %2" : "+t"(c) : "t"(a), "t"(b));
    return c;
#else
    return __builtin_fmaf(a, b, c);
#endif
}

static inline float leaky(float x)
{
    /* == (x >= 0 ? x : 0.01x) for every finite x, incl. -0 and denormals; VMAXNM on the M7 */
    return __builtin_fmaxf(x, x * 0.01f);
}

/* a layer's derived weights: b[3] x 4 (the 4 frames' accumulators start from one load), conv[K][9], m[3], l1[9] */
#define W_CONV 12

struct a2k0_args {
    const float *x;         /*  0: x of the group's first frame - 5 */
    const float *w;         /*  4: layer 0's derived weights: b[3] x 4, u[6][3], l1[9], r[3] */
    float *hsum;            /*  8 */
    float *dst;             /* 12: layer 1's input column of the group's first frame */
    int ngroups;            /* 16 */
    float slope;            /* 20 */
};

struct a2h_args {
    const float *hh;        /*  0: head sums, column of the group's first frame - 15 */
    const float *w;         /*  4: head[16][3], hb */
    float *out;             /*  8 */
    int ngroups;            /* 12 */
};

#ifdef A2_M7
/*
 * Cortex-M7 (Thumb-2, FPv5) kernels, 4 frames per group.
 * Layers (a2m7_k6 / a2m7_k6p: K = 6, tap pointers in r1-r6, without / with PLD on the 5 older taps and the
 * last layer's case; a2m7_k15: K = 15 at equal steps from tap 0, a linear buffer):
 *   r0 the newest tap again (the residual's s), r7 weights, r8 cond, r9 hsum, r10 dst, r11 groups, r12 weights
 *   s0-s11  accumulators, frame f channel c in s(3f + c)
 *   s12-s20 one tap's weights w[j * 3 + c] (in j -> out c), s21-s26 two frames of its input columns
 *   post: s12-s14 mix-in, s15-s23 l1x1, s24-s29 temporaries, s31 the LeakyReLU slope
 * Layer 0 (a2m7_k0): s12-s20 x of frames -5..3 of the group, s21-s23 one tap's 3-vector;
 *   residual: s12-s20 l1x1, s21-s23 rechannel, s27-s29 the frame's output, s30 its x.
 * Head (a2m7_head): s0-s3 four outputs, s4-s6 one tap's weights, s8-s19 four columns (a tap moves on by one:
 *   the oldest column's registers take the next one), s7 bias.
 * Per accumulator the multiply-adds come in the order of the C kernels below, so both builds give the same bits.
 */
void a2m7_k6(struct a2k_args *a);
void a2m7_k6p(struct a2k_args *a);
void a2m7_k15(struct a2k_args *a);
void a2m7_k0(struct a2k0_args *a);
void a2m7_head(struct a2h_args *a);

__asm__(
    ".syntax unified\n"
    ".arch armv7e-m\n"                            /* module-level assembly does not get the target's features (LTO) */
    ".fpu fpv5-d16\n"
    ".thumb\n"
    ".macro A2_HALF a0, a1, a2, b0, b1, b2\n"     /* frames A (\\a*) and B (\\b*) += tap . columns s21-s26 */
    "    vfma.f32 \\a0, s12, s21\n"
    "    vfma.f32 \\a1, s13, s21\n"
    "    vfma.f32 \\a2, s14, s21\n"
    "    vfma.f32 \\b0, s12, s24\n"
    "    vfma.f32 \\b1, s13, s24\n"
    "    vfma.f32 \\b2, s14, s24\n"
    "    vfma.f32 \\a0, s15, s22\n"
    "    vfma.f32 \\a1, s16, s22\n"
    "    vfma.f32 \\a2, s17, s22\n"
    "    vfma.f32 \\b0, s15, s25\n"
    "    vfma.f32 \\b1, s16, s25\n"
    "    vfma.f32 \\b2, s17, s25\n"
    "    vfma.f32 \\a0, s18, s23\n"
    "    vfma.f32 \\a1, s19, s23\n"
    "    vfma.f32 \\a2, s20, s23\n"
    "    vfma.f32 \\b0, s18, s26\n"
    "    vfma.f32 \\b1, s19, s26\n"
    "    vfma.f32 \\b2, s20, s26\n"
    ".endm\n"
    ".macro A2_TAP rk, pld\n"
    "    .if \\pld\n"
    "    pld      [\\rk, #192]\n"                   /* the D-cache has no prefetcher: 4 groups ahead, */
    "    pld      [\\rk, #224]\n"                   /* every line of the 48 bytes a group moves on by */
    "    .endif\n"
    "    vldmia   r12!, {s12-s20}\n"
    "    vldmia   \\rk!, {s21-s26}\n"
    "    A2_HALF  s0, s1, s2, s3, s4, s5\n"
    "    vldmia   \\rk!, {s21-s26}\n"
    "    A2_HALF  s6, s7, s8, s9, s10, s11\n"
    ".endm\n"
    ".macro A2_RES a0, a1, a2, b0, b1, b2\n"      /* the next layer's columns: s of frames A, B += l1x1 . a */
    "    vldmia   r0!, {s24-s29}\n"
    "    vfma.f32 s24, s15, \\a0\n"
    "    vfma.f32 s25, s16, \\a0\n"
    "    vfma.f32 s26, s17, \\a0\n"
    "    vfma.f32 s27, s15, \\b0\n"
    "    vfma.f32 s28, s16, \\b0\n"
    "    vfma.f32 s29, s17, \\b0\n"
    "    vfma.f32 s24, s18, \\a1\n"
    "    vfma.f32 s25, s19, \\a1\n"
    "    vfma.f32 s26, s20, \\a1\n"
    "    vfma.f32 s27, s18, \\b1\n"
    "    vfma.f32 s28, s19, \\b1\n"
    "    vfma.f32 s29, s20, \\b1\n"
    "    vfma.f32 s24, s21, \\a2\n"
    "    vfma.f32 s25, s22, \\a2\n"
    "    vfma.f32 s26, s23, \\a2\n"
    "    vfma.f32 s27, s21, \\b2\n"
    "    vfma.f32 s28, s22, \\b2\n"
    "    vfma.f32 s29, s23, \\b2\n"
    "    vstmia   r10!, {s24-s29}\n"
    ".endm\n"
    ".macro A2_LK6 a0, a1, a2, a3, a4, a5\n"      /* LeakyReLU = maxnm(a, 0.01 a) */
    "    vmul.f32 s24, \\a0, s31\n"
    "    vmul.f32 s25, \\a1, s31\n"
    "    vmul.f32 s26, \\a2, s31\n"
    "    vmul.f32 s27, \\a3, s31\n"
    "    vmul.f32 s28, \\a4, s31\n"
    "    vmul.f32 s29, \\a5, s31\n"
    "    vmaxnm.f32 \\a0, \\a0, s24\n"
    "    vmaxnm.f32 \\a1, \\a1, s25\n"
    "    vmaxnm.f32 \\a2, \\a2, s26\n"
    "    vmaxnm.f32 \\a3, \\a3, s27\n"
    "    vmaxnm.f32 \\a4, \\a4, s28\n"
    "    vmaxnm.f32 \\a5, \\a5, s29\n"
    ".endm\n"
    ".macro A2_LEAKY6 a0, a1, a2, a3, a4, a5\n"   /* LeakyReLU, then head sums += a */
    "    A2_LK6   \\a0, \\a1, \\a2, \\a3, \\a4, \\a5\n"
    "    vldmia   r9, {s24-s29}\n"
    "    vadd.f32 s24, s24, \\a0\n"
    "    vadd.f32 s25, s25, \\a1\n"
    "    vadd.f32 s26, s26, \\a2\n"
    "    vadd.f32 s27, s27, \\a3\n"
    "    vadd.f32 s28, s28, \\a4\n"
    "    vadd.f32 s29, s29, \\a5\n"
    "    vstmia   r9!, {s24-s29}\n"
    ".endm\n"
    /* r12 = mix-in, r8 cond, r9 hsum, r10 dst, r0 the newest tap's columns; last: r10 = 0 is the last layer */
    ".macro A2_POST last\n"
    "    vldmia   r12, {s12-s23}\n"
    "    vldmia   r8!, {s24-s27}\n"
    "    vfma.f32 s0, s12, s24\n"
    "    vfma.f32 s1, s13, s24\n"
    "    vfma.f32 s2, s14, s24\n"
    "    vfma.f32 s3, s12, s25\n"
    "    vfma.f32 s4, s13, s25\n"
    "    vfma.f32 s5, s14, s25\n"
    "    vfma.f32 s6, s12, s26\n"
    "    vfma.f32 s7, s13, s26\n"
    "    vfma.f32 s8, s14, s26\n"
    "    vfma.f32 s9, s12, s27\n"
    "    vfma.f32 s10, s13, s27\n"
    "    vfma.f32 s11, s14, s27\n"
    "    A2_LEAKY6 s0, s1, s2, s3, s4, s5\n"
    "    A2_LEAKY6 s6, s7, s8, s9, s10, s11\n"
    "    .if \\last\n"
    "    cmp      r10, #0\n"
    "    beq      8f\n"
    "    .endif\n"
    "    A2_RES   s0, s1, s2, s3, s4, s5\n"
    "    A2_RES   s6, s7, s8, s9, s10, s11\n"
    "    .if \\last\n"
    "8:\n"
    "    .endif\n"
    ".endm\n"

    ".macro A2_K6 name, pld\n"
    "    .section .text.\\name,\"ax\",%progbits\n"
    "    .global \\name\n"
    "    .type \\name,%function\n"
    "    .thumb_func\n"
    "    .p2align 2\n"
    "\\name:\n"
    "    push     {r4, r5, r6, r7, r8, r9, r10, r11, lr}\n"
    "    vpush    {s16-s31}\n"
    "    ldmia    r0, {r7, r8, r9, r10, r11}\n"   /* w cond hsum dst ngroups */
    "    vldr     s31, [r0, #20]\n"
    "    add      r12, r0, #28\n"
    "    ldmia    r12, {r1, r2, r3, r4, r5, r6}\n"
    "    mov      r0, r6\n"
    "1:\n"
    "    mov      r12, r7\n"
    "    vldmia   r12!, {s0-s11}\n"                /* the bias, four times */
    "    A2_TAP   r1, \\pld\n"
    "    A2_TAP   r2, \\pld\n"
    "    A2_TAP   r3, \\pld\n"
    "    A2_TAP   r4, \\pld\n"
    "    A2_TAP   r5, \\pld\n"
    "    A2_TAP   r6, 0\n"                         /* the newest tap: what the layer before just wrote */
    "    A2_POST  \\pld\n"
    "    subs     r11, r11, #1\n"
    "    bne      1b\n"
    "    vpop     {s16-s31}\n"
    "    pop      {r4, r5, r6, r7, r8, r9, r10, r11, pc}\n"
    "    .size \\name, .-\\name\n"
    ".endm\n"
    "A2_K6 a2m7_k6, 0\n"
    "A2_K6 a2m7_k6p, 1\n"

    ".section .text.a2m7_k15,\"ax\",%progbits\n"
    ".global a2m7_k15\n"
    ".type a2m7_k15,%function\n"
    ".thumb_func\n"
    ".p2align 2\n"
    "a2m7_k15:\n"
    "    push     {r4, r5, r6, r7, r8, r9, r10, r11, lr}\n"
    "    vpush    {s16-s31}\n"
    "    ldmia    r0, {r7, r8, r9, r10, r11}\n"
    "    vldr     s31, [r0, #20]\n"
    "    ldr      r1, [r0, #28]\n"                 /* tap 0 */
    "    ldr      r6, [r0, #32]\n"
    "    subs     r6, r6, r1\n"
    "    subs     r6, r6, #48\n"                   /* from the end of a tap's group to the next tap's */
    "    ldr      r0, [r0, #84]\n"                 /* tap 14 */
    "1:\n"
    "    mov      r12, r7\n"
    "    vldmia   r12!, {s0-s11}\n"
    "    mov      r3, r1\n"
    "    movs     r2, #3\n"
    "2:\n"
    "    .rept 5\n"
    "    A2_TAP   r3, 0\n"
    "    add      r3, r3, r6\n"
    "    .endr\n"
    "    subs     r2, r2, #1\n"
    "    bne      2b\n"
    "    A2_POST  0\n"
    "    adds     r1, r1, #48\n"
    "    subs     r11, r11, #1\n"
    "    bne      1b\n"
    "    vpop     {s16-s31}\n"
    "    pop      {r4, r5, r6, r7, r8, r9, r10, r11, pc}\n"
    ".size a2m7_k15, .-a2m7_k15\n"

    /* layer 0: acc(f, c) += v(c) * x(f + k), v in s21-s23, x(i) in s(12 + i) */
    ".macro A2_K0TAP x0, x1, x2, x3\n"
    "    vldmia   r7!, {s21-s23}\n"
    "    vfma.f32 s0, s21, \\x0\n"
    "    vfma.f32 s1, s22, \\x0\n"
    "    vfma.f32 s2, s23, \\x0\n"
    "    vfma.f32 s3, s21, \\x1\n"
    "    vfma.f32 s4, s22, \\x1\n"
    "    vfma.f32 s5, s23, \\x1\n"
    "    vfma.f32 s6, s21, \\x2\n"
    "    vfma.f32 s7, s22, \\x2\n"
    "    vfma.f32 s8, s23, \\x2\n"
    "    vfma.f32 s9, s21, \\x3\n"
    "    vfma.f32 s10, s22, \\x3\n"
    "    vfma.f32 s11, s23, \\x3\n"
    ".endm\n"
    /* one frame's output: rechannel * x + l1x1 . a; x at [r1 + off] */
    ".macro A2_K0RES a0, a1, a2, off\n"
    "    vldr     s30, [r1, #\\off]\n"
    "    vmul.f32 s27, s21, s30\n"
    "    vmul.f32 s28, s22, s30\n"
    "    vmul.f32 s29, s23, s30\n"
    "    vfma.f32 s27, s12, \\a0\n"
    "    vfma.f32 s28, s13, \\a0\n"
    "    vfma.f32 s29, s14, \\a0\n"
    "    vfma.f32 s27, s15, \\a1\n"
    "    vfma.f32 s28, s16, \\a1\n"
    "    vfma.f32 s29, s17, \\a1\n"
    "    vfma.f32 s27, s18, \\a2\n"
    "    vfma.f32 s28, s19, \\a2\n"
    "    vfma.f32 s29, s20, \\a2\n"
    "    vstmia   r4!, {s27-s29}\n"
    ".endm\n"
    ".section .text.a2m7_k0,\"ax\",%progbits\n"
    ".global a2m7_k0\n"
    ".type a2m7_k0,%function\n"
    ".thumb_func\n"
    ".p2align 2\n"
    "a2m7_k0:\n"
    "    push     {r4, r5, r6, r7, lr}\n"
    "    vpush    {s16-s31}\n"
    "    ldmia    r0, {r1, r2, r3, r4, r5}\n"     /* x w hsum dst ngroups */
    "    vldr     s31, [r0, #20]\n"
    "1:\n"
    "    vldmia   r1, {s12-s20}\n"
    "    mov      r7, r2\n"
    "    vldmia   r7!, {s0-s11}\n"                 /* the bias, four times */
    "    A2_K0TAP s12, s13, s14, s15\n"
    "    A2_K0TAP s13, s14, s15, s16\n"
    "    A2_K0TAP s14, s15, s16, s17\n"
    "    A2_K0TAP s15, s16, s17, s18\n"
    "    A2_K0TAP s16, s17, s18, s19\n"
    "    A2_K0TAP s17, s18, s19, s20\n"            /* the newest tap, the mix-in added to it */
    "    A2_LK6   s0, s1, s2, s3, s4, s5\n"
    "    A2_LK6   s6, s7, s8, s9, s10, s11\n"
    "    vstmia   r3!, {s0-s11}\n"                 /* layer 0 starts the head sums */
    "    vldmia   r7, {s12-s23}\n"                 /* l1x1, rechannel */
    "    A2_K0RES s0, s1, s2, 20\n"
    "    A2_K0RES s3, s4, s5, 24\n"
    "    A2_K0RES s6, s7, s8, 28\n"
    "    A2_K0RES s9, s10, s11, 32\n"
    "    adds     r1, r1, #16\n"
    "    subs     r5, r5, #1\n"
    "    bne      1b\n"
    "    vpop     {s16-s31}\n"
    "    pop      {r4, r5, r6, r7, pc}\n"
    ".size a2m7_k0, .-a2m7_k0\n"

    /* one head tap over the columns a (frame 0) .. d (frame 3); then a's registers take the next column */
    ".macro A2_HTAP a0, a1, a2, b0, b1, b2, c0, c1, c2, d0, d1, d2, load\n"
    "    vldmia   r5!, {s4-s6}\n"
    "    vfma.f32 s0, s4, \\a0\n"
    "    vfma.f32 s1, s4, \\b0\n"
    "    vfma.f32 s2, s4, \\c0\n"
    "    vfma.f32 s3, s4, \\d0\n"
    "    vfma.f32 s0, s5, \\a1\n"
    "    vfma.f32 s1, s5, \\b1\n"
    "    vfma.f32 s2, s5, \\c1\n"
    "    vfma.f32 s3, s5, \\d1\n"
    "    vfma.f32 s0, s6, \\a2\n"
    "    vfma.f32 s1, s6, \\b2\n"
    "    vfma.f32 s2, s6, \\c2\n"
    "    vfma.f32 s3, s6, \\d2\n"
    "    .if \\load\n"
    "    vldmia   r6!, {\\a0-\\a2}\n"
    "    .endif\n"
    ".endm\n"
    ".macro A2_HTAP4 last\n"
    "    A2_HTAP  s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, 1\n"
    "    A2_HTAP  s11, s12, s13, s14, s15, s16, s17, s18, s19, s8, s9, s10, 1\n"
    "    A2_HTAP  s14, s15, s16, s17, s18, s19, s8, s9, s10, s11, s12, s13, 1\n"
    "    A2_HTAP  s17, s18, s19, s8, s9, s10, s11, s12, s13, s14, s15, s16, \\last\n"
    ".endm\n"
    ".section .text.a2m7_head,\"ax\",%progbits\n"
    ".global a2m7_head\n"
    ".type a2m7_head,%function\n"
    ".thumb_func\n"
    ".p2align 2\n"
    "a2m7_head:\n"
    "    push     {r4, r5, r6, lr}\n"
    "    vpush    {s16-s19}\n"
    "    ldmia    r0, {r1, r2, r3, r4}\n"         /* hh w out ngroups */
    "    vldr     s7, [r2, #192]\n"
    "1:\n"
    "    vmov.f32 s0, s7\n"
    "    vmov.f32 s1, s7\n"
    "    vmov.f32 s2, s7\n"
    "    vmov.f32 s3, s7\n"
    "    mov      r5, r2\n"
    "    mov      r6, r1\n"
    "    vldmia   r6!, {s8-s19}\n"
    "    A2_HTAP4 1\n"
    "    A2_HTAP4 1\n"
    "    A2_HTAP4 1\n"
    "    A2_HTAP4 0\n"
    "    vstmia   r3!, {s0-s3}\n"
    "    adds     r1, r1, #48\n"
    "    subs     r4, r4, #1\n"
    "    bne      1b\n"
    "    vpop     {s16-s19}\n"
    "    pop      {r4, r5, r6, pc}\n"
    ".size a2m7_head, .-a2m7_head\n"
    ".text\n");
#else
/* the kernels in C: the PC build, and the reference for the operation order */
static void kern_c(struct a2k_args *a)
{
    const int K = a->k;
    const float *cb = a->w, *w = cb + W_CONV, *m = w + K * 9, *l1 = m + 3;
    const float *cond = a->cond, *tap[15];
    float *hs = a->hsum, *dst = a->dst;
    for (int k = 0; k < K; k++)
        tap[k] = a->tap[k];
    for (int g = 0; g < a->ngroups; g++, cond += 4, hs += 12) {
        float z[4][3];
        for (int f = 0; f < 4; f++)
            for (int c = 0; c < 3; c++)
                z[f][c] = cb[c];
        for (int k = 0; k < K; k++) {
            const float *x = tap[k], *wk = w + k * 9;
            for (int f = 0; f < 4; f++)
                for (int j = 0; j < 3; j++)
                    for (int c = 0; c < 3; c++)
                        z[f][c] = fmadd(wk[j * 3 + c], x[f * 3 + j], z[f][c]);
            tap[k] = x + 12;
        }
        for (int f = 0; f < 4; f++)
            for (int c = 0; c < 3; c++) {
                const float v = leaky(fmadd(m[c], cond[f], z[f][c]));
                z[f][c] = v;
                hs[f * 3 + c] = hs[f * 3 + c] + v;
            }
        if (dst) {
            const float *s = tap[K - 1] - 12;
            for (int f = 0; f < 4; f++)
                for (int c = 0; c < 3; c++) {
                    float v = s[f * 3 + c];
                    for (int j = 0; j < 3; j++)
                        v = fmadd(l1[j * 3 + c], z[f][j], v);
                    dst[f * 3 + c] = v;
                }
            dst += 12;
        }
    }
}

static void k0_c(struct a2k0_args *a)
{
    const float *b = a->w, *u = b + 12, *l1 = u + 18, *r = l1 + 9;
    const float *x = a->x;
    float *hs = a->hsum, *dst = a->dst;
    for (int f = 0; f < a->ngroups * 4; f++, x++, hs += 3, dst += 3) {
        float v[3];
        for (int c = 0; c < 3; c++) {
            float z = b[c];
            for (int k = 0; k < 6; k++)
                z = fmadd(u[k * 3 + c], x[k], z);
            v[c] = leaky(z);
            hs[c] = v[c];
        }
        for (int c = 0; c < 3; c++) {
            float y = r[c] * x[A2_X_LB];
            for (int j = 0; j < 3; j++)
                y = fmadd(l1[j * 3 + c], v[j], y);
            dst[c] = y;
        }
    }
}
#endif

/* the head over n frames (the M7 kernel takes whole groups, this the rest) */
static void head_c(const float *hh, const float *hw, float *out, int n)
{
    for (int f = 0; f < n; f++, hh += 3) {
        float y = hw[A2_HEAD_K * 3];
        for (int k = 0; k < A2_HEAD_K; k++)
            for (int j = 0; j < 3; j++)
                y = fmadd(hw[k * 3 + j], hh[k * 3 + j], y);
        out[f] = y;
    }
}

/* h[0 .. n) = h[off .. off + n), forward (off > 0, n a multiple of 16) */
static inline void move_down(float *h, int off, int n)
{
    const float *s = h + off;
#ifdef A2_M7
    for (; n >= 32; n -= 32)
        __asm__ volatile("vldmia %0!, {d0-d15}\n\tvstmia %1!, {d0-d15}"
                         : "+r"(s), "+r"(h)
                         :
                         : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", "d12", "d13",
                           "d14", "d15", "memory");
    if (n)
        __asm__ volatile("vldmia %0, {d0-d7}\n\tvstmia %1, {d0-d7}"
                         :
                         : "r"(s), "r"(h)
                         : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "memory");
#else
    for (; n > 0; n--)
        *h++ = *s++;
#endif
}

/* A layer with a linear history that writes into a ring, over G groups: its taps, cond and hsum as set by
   a2_init; a second call when the writer passes the ring's end. */
static void layer_to_ring(struct a2_hist *L, struct a2_hist *nx, int G)
{
    struct a2k_args *a = &L->a;
    const int NR = nx->ring;
    int dc = nx->cur;
    const int m = (NR - dc + 3) >> 2;               /* groups until the writer starts past the end */
    a->dst = nx->h + dc * 3;
    if (m >= G) {
        a->ngroups = G;
        L->kern(a);
        dc += 4 * G - NR;
    } else {
        a->ngroups = m;
        L->kern(a);
        dc += 4 * m - NR;
    }
    for (int i = 0; i < dc * 3; i++)                /* written past the end (up to 3 columns): back to the start */
        nx->h[i] = nx->h[NR * 3 + i];
    if (m < G) {
        struct a2k_args b = *a;
        for (int k = 0; k < L->k; k++)
            b.tap[k] += 12 * m;
        b.cond += 4 * m;
        b.hsum += 12 * m;
        b.dst = nx->h + dc * 3;
        b.ngroups = G - m;
        L->kern(&b);
    }
}

/* A layer with a ring history (K = 6) over G groups: one call when nothing reaches an end in this block,
   otherwise in segments cut where a tap pointer or the writer's passes its ring's end. */
static void layer_ring(struct a2 *s, struct a2_hist *L, struct a2_hist *nx, int G)
{
    const int R = L->ring, NR = nx ? nx->ring : 0;
    struct a2k_args *a = &L->a;
    float *h = L->h;
    int col[6], top = 0, dc = nx ? (NR ? nx->cur : nx->lb) : 0;
    for (int k = 5, c = L->cur; k >= 0; k--) {      /* the newest tap at the block's first frame, then back */
        col[k] = c;
        a->tap[k] = h + c * 3;
        if (c > top)
            top = c;
        c -= L->d;
        if (c < 0)
            c += R;
    }
    a->cond = s->x + A2_X_LB;
    a->hsum = s->hh + A2_HEAD_LB * 3;
    a->dst = nx ? nx->h + dc * 3 : 0;
    if (top + 4 * G <= R && (!NR || dc + 4 * G <= NR)) {
        a->ngroups = G;
        L->kern(a);
        return;
    }
    for (int i = 0; i < 9; i++)                     /* the first 3 columns again after the end */
        h[R * 3 + i] = h[i];
    for (int g = 0;;) {
        int m = G - g;
        for (int k = 0; k < 6; k++) {
            const int q = (R - col[k] + 3) >> 2;    /* groups until this tap starts past the end */
            if (q < m)
                m = q;
        }
        if (NR) {
            const int q = (NR - dc + 3) >> 2;
            if (q < m)
                m = q;
        }
        a->ngroups = m;
        L->kern(a);
        g += m;
        dc += 4 * m;
        if (NR && dc >= NR) {                       /* written past the end: back to the start */
            dc -= NR;
            for (int i = 0; i < dc * 3; i++)
                nx->h[i] = nx->h[NR * 3 + i];
        }
        if (g == G)
            return;
        for (int k = 0; k < 6; k++) {
            col[k] += 4 * m;
            if (col[k] >= R)
                col[k] -= R;
            a->tap[k] = h + col[k] * 3;
        }
        a->cond = s->x + A2_X_LB + g * 4;
        a->hsum = s->hh + (A2_HEAD_LB + g * 4) * 3;
        a->dst = nx ? nx->h + dc * 3 : 0;
    }
}
void a2_process(struct a2 *s, const float *in, float *out, int n)
{
    const int G = (n + 3) >> 2;
    float *x = s->x + A2_X_LB;
    for (int f = 0; f < n; f++)
        x[f] = in[f];

    struct a2k0_args a0 = {s->x, s->w0, s->hh + A2_HEAD_LB * 3, s->L[1].h + s->L[1].lb * 3, G, 0.01f};
#ifdef A2_M7
    a2m7_k0(&a0);
#else
    k0_c(&a0);
#endif
    for (int l = 1; l < A2_LAYERS; l++) {
        struct a2_hist *L = &s->L[l];
        if (L->direct) {
            L->a.ngroups = G;
            L->kern(&L->a);
        } else if (L->ring) {
            layer_ring(s, L, l + 1 < A2_LAYERS ? L + 1 : 0, G);
        } else {
            layer_to_ring(L, L + 1, G);
        }
        if (L->ring) {
            L->cur += n;
            if (L->cur >= L->ring)
                L->cur -= L->ring;
        } else {
            move_down(L->h, n * 3, L->move);        /* now: the layer has just read all of it */
        }
    }

    /* head: K = 16 over the head sums, head_scale folded in */
    const int g4 = n >> 2;
#ifdef A2_M7
    struct a2h_args ah = {s->hh, s->head, out, g4};
    if (g4)
        a2m7_head(&ah);
#else
    head_c(s->hh, s->head, out, g4 * 4);
#endif
    if (n & 3)
        head_c(s->hh + g4 * 12, s->head, out + g4 * 4, n & 3);
    move_down(s->hh, n * 3, (A2_HEAD_LB * 3 + 15) & ~15);
    for (int i = 0; i < A2_X_LB; i++)
        s->x[i] = s->x[n + i];
}

SDRAM_FUNC static void fill(float *p, int cols, const float *col)
{
    for (int c = 0; c < cols; c++) {
        p[c * 3] = col[0];
        p[c * 3 + 1] = col[1];
        p[c * 3 + 2] = col[2];
    }
}

SDRAM_FUNC static int lookback(int l)
{
    return (a2_kernel[l] - 1) * a2_dilation[l];
}

SDRAM_FUNC void a2_init(struct a2 *s, float *dw, float *lin, const float *w)
{
    /* derived weights and histories */
    const float *rc = w;
    w += 3;
    float *o = dw, *p = s->hist;
    double B[3] = {0.0, 0.0, 0.0};                  /* what the 1x1 biases have added to the stream so far */
    for (int l = 0; l < A2_LAYERS; l++) {
        const int K = a2_kernel[l];
        const float *conv = w, *b = w + K * 9, *m = b + 3, *l1 = m + 3, *lb = l1 + 9;
        w = lb + 3;
        if (l == 0) {
            s->w0 = o;
            for (int i = 0; i < 12; i++)
                o[i] = b[i % 3];
            o += 12;
            for (int k = 0; k < K; k++)
                for (int c = 0; c < 3; c++) {
                    float v = conv[k * 9 + c] * rc[0];
                    v = fmadd(conv[k * 9 + 3 + c], rc[1], v);
                    v = fmadd(conv[k * 9 + 6 + c], rc[2], v);
                    o[k * 3 + c] = k == K - 1 ? v + m[c] : v;
                }
            o += K * 3;
            for (int i = 0; i < 9; i++)
                o[i] = l1[i];
            o += 9;
            for (int i = 0; i < 3; i++)
                o[i] = rc[i];
            o += 3;
        } else {
            struct a2_hist *L = &s->L[l];
            L->k = K;
            L->d = a2_dilation[l];
            L->lb = lookback(l);
            L->ring = L->lb > LIN_MAX ? L->lb + A2_B4 : 0;
            L->cur = 0;
            L->move = (L->lb * 3 + 15) & ~15;
            L->direct = !L->ring && !(l + 1 < A2_LAYERS && lookback(l + 1) > LIN_MAX);
            if (L->ring) {
                L->h = p;
                p += (L->ring + A2_SPARE) * 3;
            } else {
                L->h = lin;
                lin += (L->lb + A2_B4 + A2_SPARE) * 3;
            }
#ifdef A2_M7
            L->kern = K == 15 ? a2m7_k15 : L->d >= PLD_MIN ? a2m7_k6p : a2m7_k6;
#else
            L->kern = kern_c;
#endif
            L->a.w = o;
            L->a.slope = 0.01f;
            L->a.k = K;
            for (int c = 0; c < 3; c++) {
                double v = b[c];
                for (int k = 0; k < K; k++)
                    for (int j = 0; j < 3; j++)
                        v += (double)conv[k * 9 + j * 3 + c] * B[j];
                o[c] = o[3 + c] = o[6 + c] = o[9 + c] = (float)v;
            }
            o += W_CONV;
            for (int i = 0; i < K * 9; i++)
                o[i] = conv[i];
            o += K * 9;
            for (int i = 0; i < 12; i++)                /* m, l1 as packed */
                o[i] = m[i];
            o += 12;
        }
        for (int c = 0; c < 3; c++)
            B[c] += lb[c];
    }
    const float hscale = w[A2_HEAD_K * 3 + 1];
    s->head = o;
    for (int i = 0; i < A2_HEAD_K * 3; i++)
        o[i] = w[i] * hscale;
    o[A2_HEAD_K * 3] = w[A2_HEAD_K * 3] * hscale;

    for (int l = 1; l < A2_LAYERS; l++) {           /* what never changes for the layers with a linear history */
        struct a2_hist *L = &s->L[l], *nx = l + 1 < A2_LAYERS ? L + 1 : 0;
        if (L->ring)
            continue;
        for (int k = 0; k < L->k; k++)
            L->a.tap[k] = L->h + k * L->d * 3;
        L->a.cond = s->x + A2_X_LB;
        L->a.hsum = s->hh + A2_HEAD_LB * 3;
        L->a.dst = L->direct ? nx->h + nx->lb * 3 : 0;
    }

    /* steady state of a silent input (what the core's prewarm converges to), with the kernels' operations */
    for (int i = 0; i < A2_X_LB + A2_B4; i++)
        s->x[i] = 0.0f;
    float col[3], hs[3];
    {
        const float *b = s->w0, *u = b + 12, *l1 = u + 18, *r = l1 + 9;
        float a[3];
        for (int c = 0; c < 3; c++) {
            float z = b[c];
            for (int k = 0; k < 6; k++)
                z = fmadd(u[k * 3 + c], 0.0f, z);
            a[c] = leaky(z);
            hs[c] = a[c];
        }
        for (int c = 0; c < 3; c++) {
            float v = r[c] * 0.0f;
            for (int j = 0; j < 3; j++)
                v = fmadd(l1[j * 3 + c], a[j], v);
            col[c] = v;
        }
    }
    for (int l = 1; l < A2_LAYERS; l++) {
        struct a2_hist *L = &s->L[l];
        fill(L->h, (L->ring ? L->ring : L->lb + A2_B4) + A2_SPARE, col);
        const float *b = L->a.w, *cw = b + W_CONV, *m = cw + L->k * 9, *l1 = m + 3;
        float a[3];
        for (int c = 0; c < 3; c++) {
            float z = b[c];
            for (int k = 0; k < L->k; k++)
                for (int j = 0; j < 3; j++)
                    z = fmadd(cw[k * 9 + j * 3 + c], col[j], z);
            a[c] = leaky(fmadd(m[c], 0.0f, z));
            hs[c] = hs[c] + a[c];
        }
        if (l + 1 < A2_LAYERS) {
            float nxc[3];
            for (int c = 0; c < 3; c++) {
                float v = col[c];
                for (int j = 0; j < 3; j++)
                    v = fmadd(l1[j * 3 + c], a[j], v);
                nxc[c] = v;
            }
            col[0] = nxc[0];
            col[1] = nxc[1];
            col[2] = nxc[2];
        }
    }
    fill(s->hh, A2_HEAD_LB + A2_B4 + 1, hs);
}
