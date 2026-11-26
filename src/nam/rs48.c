/*
 * 44.1 kHz <-> 48 kHz resampler (nam/rs48.h).
 *
 * A stage is a rational polyphase resampler on one prototype h[N] (sum 1, N = Pi K), with time counted in
 * prototype samples: an input sample every Pi, an output every Po.
 *   y[n] = Pi * sum_j x[b - j] * h[p + Pi j],  b = floor(n Po / Pi), p = n Po - b Pi
 * Phase p's taps are stored reversed and contiguous (tab[p][i] = Pi * h[p + Pi (K-1-i)]), so an output is one
 * forward dot product over x[b-K+1 .. b]. h is symmetric, so tab[Pi-1-p][i] = tab[p][K-1-i]: only the phases
 * up to the middle are stored, the others run the same row backwards. The prototypes are only needed to build
 * the tables and stay in the SDRAM.
 * A stage makes every output whose newest input has arrived; the down stage feeds a FIFO that hands out
 * RS_FRAMES samples per call. Both stages start in phase, so after B blocks the up stage has made
 * ceil(64 B 160 / 147) samples and the down stage at least 64 B of them: the FIFO never runs dry and keeps
 * 0..2 samples after each pop (tests/test_nam.c).
 */
#include "nam/rs48.h"
#include "platform/sdram.h"

#ifdef __clang__
#pragma clang fp contract(off)
#endif

#define UP_PI 160                       /* 44.1k -> 48k: an input every 160, an output every 147 */
#define UP_PO 147
#define DN_PI 147                       /* 48k -> 44.1k */
#define DN_PO 160
#define UP_N (UP_PI * RS_UP_K)          /* 5120 */
#define DN_N (DN_PI * RS_DN_K)          /* 9408 */
#define UP_ROWS ((UP_PI + 1) / 2)
#define DN_ROWS ((DN_PI + 1) / 2)

_Static_assert(RS_UP_K == 32 && RS_DN_K == 64, "the dot products below are written for these lengths");

static float pu[UP_N] SDRAM_BSS, pd[DN_N] SDRAM_BSS;

/* written once (rs48_init, main loop), then only read */
static float t_up[UP_ROWS * RS_UP_K] __attribute__((aligned(32)));
static float t_dn[DN_ROWS * RS_DN_K] __attribute__((aligned(32)));
static int designed;

SDRAM_FUNC static double sqrt_d(double x)
{
#if defined(__arm__)
    double r;
    __asm__("vsqrt.f64 %P0, %P1" : "=w"(r) : "w"(x));
    return r;
#else
    return __builtin_sqrt(x);
#endif
}

SDRAM_FUNC static double bessel_i0(double x)
{
    double s = 1.0, t = 1.0, y = x * x * 0.25;
    for (int k = 1; k < 64; k++) {
        t *= y / ((double)k * (double)k);
        s += t;
        if (t < 1e-17 * s)
            break;
    }
    return s;
}

/* Kaiser-windowed sinc (N even), normalised to sum 1. Built outward from the centre with a rotation recurrence,
   so only sin / cos of small angles are needed (Taylor is exact there). */
SDRAM_FUNC static void design(float *h, int N, double fs, double fc, double atten)
{
    const double w = 2.0 * 3.14159265358979323846 * fc / fs, beta = 0.1102 * (atten - 8.7);
    const double c = (N - 1) * 0.5, i0b = bessel_i0(beta);
    double s2 = w * w * 0.25;
    double sn = w * 0.5 * (1.0 - s2 / 6.0 * (1.0 - s2 / 20.0 * (1.0 - s2 / 42.0)));
    double cs = 1.0 - s2 / 2.0 * (1.0 - s2 / 12.0 * (1.0 - s2 / 30.0 * (1.0 - s2 / 56.0)));
    s2 = w * w;
    const double sw = w * (1.0 - s2 / 6.0 * (1.0 - s2 / 20.0 * (1.0 - s2 / 42.0)));
    const double cw = 1.0 - s2 / 2.0 * (1.0 - s2 / 12.0 * (1.0 - s2 / 30.0 * (1.0 - s2 / 56.0)));
    double sum = 0.0;
    for (int k = 0; k < N / 2; k++) {
        const double d = k + 0.5, r = d / c;
        const double v = sn / (w * d) * bessel_i0(beta * sqrt_d(1.0 - r * r)) / i0b;
        h[N / 2 + k] = (float)v;
        sum += 2.0 * v;
        const double t = sn * cw + cs * sw;
        cs = cs * cw - sn * sw;
        sn = t;
    }
    for (int k = 0; k < N / 2; k++)
        h[N / 2 + k] = h[N / 2 - 1 - k] = (float)(h[N / 2 + k] / sum);
}

SDRAM_FUNC static void build(float *tab, const float *h, int Pi, int K)
{
    for (int p = 0; p < (Pi + 1) / 2; p++)
        for (int i = 0; i < K; i++)
            tab[p * K + i] = (float)Pi * h[p + Pi * (K - 1 - i)];
}

SDRAM_FUNC static void stage_init(struct rs48_stage *s, float *x, int nx)
{
    s->x = x;
    s->b = s->p = 0;
    for (int i = 0; i < nx; i++)
        x[i] = 0.0f;
}

/*
 * The dot products: eight accumulators, the first products start them, summed at the end as
 * ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7)). `more` further steps of 8, then 4 more taps if `tail`.
 * Forward: sum x[i] h[i]. Backward: sum x[i] h[-1 - i], h = the row's end.
 * VLDM takes two floats per cycle on the M7's 64-bit path.
 */
#if defined(__arm__)
#define DOT_SUM                                                                                                 \
    "vadd.f32 s16, s16, s17\n\t"                                                                                \
    "vadd.f32 s18, s18, s19\n\t"                                                                                \
    "vadd.f32 s20, s20, s21\n\t"                                                                                \
    "vadd.f32 s22, s22, s23\n\t"                                                                                \
    "vadd.f32 s16, s16, s18\n\t"                                                                                \
    "vadd.f32 s20, s20, s22\n\t"                                                                                \
    "vadd.f32 %[r], s16, s20\n\t"
#define DOT_REGS                                                                                                \
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16", \
        "s17", "s18", "s19", "s20", "s21", "s22", "s23", "memory"

#define DOT_FWD(name, more, tail)                                                                               \
    static inline float name(const float *x, const float *h)                                                    \
    {                                                                                                           \
        float r;                                                                                                \
        __asm__ volatile("vldmia   %[x]!, {s0-s7}\n\t"                                                          \
                         "vldmia   %[h]!, {s8-s15}\n\t"                                                         \
                         "vmul.f32 s16, s0, s8\n\t"                                                             \
                         "vmul.f32 s17, s1, s9\n\t"                                                             \
                         "vmul.f32 s18, s2, s10\n\t"                                                            \
                         "vmul.f32 s19, s3, s11\n\t"                                                            \
                         "vmul.f32 s20, s4, s12\n\t"                                                            \
                         "vmul.f32 s21, s5, s13\n\t"                                                            \
                         "vmul.f32 s22, s6, s14\n\t"                                                            \
                         "vmul.f32 s23, s7, s15\n\t"                                                            \
                         ".rept " #more "\n\t"                                                                  \
                         "vldmia   %[x]!, {s0-s7}\n\t"                                                          \
                         "vldmia   %[h]!, {s8-s15}\n\t"                                                         \
                         "vfma.f32 s16, s0, s8\n\t"                                                             \
                         "vfma.f32 s17, s1, s9\n\t"                                                             \
                         "vfma.f32 s18, s2, s10\n\t"                                                            \
                         "vfma.f32 s19, s3, s11\n\t"                                                            \
                         "vfma.f32 s20, s4, s12\n\t"                                                            \
                         "vfma.f32 s21, s5, s13\n\t"                                                            \
                         "vfma.f32 s22, s6, s14\n\t"                                                            \
                         "vfma.f32 s23, s7, s15\n\t"                                                            \
                         ".endr\n\t"                                                                            \
                         ".if " #tail "\n\t"                                                                    \
                         "vldmia   %[x], {s0-s3}\n\t"                                                           \
                         "vldmia   %[h], {s8-s11}\n\t"                                                          \
                         "vfma.f32 s16, s0, s8\n\t"                                                             \
                         "vfma.f32 s17, s1, s9\n\t"                                                             \
                         "vfma.f32 s18, s2, s10\n\t"                                                            \
                         "vfma.f32 s19, s3, s11\n\t"                                                            \
                         ".endif\n\t"                                                                           \
                         DOT_SUM                                                                                \
                         : [r] "=t"(r), [x] "+r"(x), [h] "+r"(h)                                                \
                         :                                                                                      \
                         : DOT_REGS);                                                                           \
        return r;                                                                                               \
    }

#define DOT_REV(name, more, tail)                                                                               \
    static inline float name(const float *x, const float *h)                                                    \
    {                                                                                                           \
        float r;                                                                                                \
        __asm__ volatile("vldmia   %[x]!, {s0-s7}\n\t"                                                          \
                         "vldmdb   %[h]!, {s8-s15}\n\t"                                                         \
                         "vmul.f32 s16, s0, s15\n\t"                                                            \
                         "vmul.f32 s17, s1, s14\n\t"                                                            \
                         "vmul.f32 s18, s2, s13\n\t"                                                            \
                         "vmul.f32 s19, s3, s12\n\t"                                                            \
                         "vmul.f32 s20, s4, s11\n\t"                                                            \
                         "vmul.f32 s21, s5, s10\n\t"                                                            \
                         "vmul.f32 s22, s6, s9\n\t"                                                             \
                         "vmul.f32 s23, s7, s8\n\t"                                                             \
                         ".rept " #more "\n\t"                                                                  \
                         "vldmia   %[x]!, {s0-s7}\n\t"                                                          \
                         "vldmdb   %[h]!, {s8-s15}\n\t"                                                         \
                         "vfma.f32 s16, s0, s15\n\t"                                                            \
                         "vfma.f32 s17, s1, s14\n\t"                                                            \
                         "vfma.f32 s18, s2, s13\n\t"                                                            \
                         "vfma.f32 s19, s3, s12\n\t"                                                            \
                         "vfma.f32 s20, s4, s11\n\t"                                                            \
                         "vfma.f32 s21, s5, s10\n\t"                                                            \
                         "vfma.f32 s22, s6, s9\n\t"                                                             \
                         "vfma.f32 s23, s7, s8\n\t"                                                             \
                         ".endr\n\t"                                                                            \
                         ".if " #tail "\n\t"                                                                    \
                         "vldmia   %[x], {s0-s3}\n\t"                                                           \
                         "vldmdb   %[h]!, {s8-s11}\n\t"                                                         \
                         "vfma.f32 s16, s0, s11\n\t"                                                            \
                         "vfma.f32 s17, s1, s10\n\t"                                                            \
                         "vfma.f32 s18, s2, s9\n\t"                                                             \
                         "vfma.f32 s19, s3, s8\n\t"                                                             \
                         ".endif\n\t"                                                                           \
                         DOT_SUM                                                                                \
                         : [r] "=t"(r), [x] "+r"(x), [h] "+r"(h)                                                \
                         :                                                                                      \
                         : DOT_REGS);                                                                           \
        return r;                                                                                               \
    }
#else
static inline float dot_c(const float *x, const float *h, int step, int more, int tail)
{
    float a[8];
    for (int j = 0; j < 8; j++)
        a[j] = x[j] * h[j * step];
    int i = 8;
    for (int m = 0; m < more; m++, i += 8)
        for (int j = 0; j < 8; j++)
            a[j] = __builtin_fmaf(x[i + j], h[(i + j) * step], a[j]);
    if (tail)
        for (int j = 0; j < 4; j++)
            a[j] = __builtin_fmaf(x[i + j], h[(i + j) * step], a[j]);
    return ((a[0] + a[1]) + (a[2] + a[3])) + ((a[4] + a[5]) + (a[6] + a[7]));
}

#define DOT_FWD(name, more, tail)                                                                               \
    static inline float name(const float *x, const float *h)                                                    \
    {                                                                                                           \
        return dot_c(x, h, 1, more, tail);                                                                      \
    }
#define DOT_REV(name, more, tail)                                                                               \
    static inline float name(const float *x, const float *h)                                                    \
    {                                                                                                           \
        return dot_c(x, h - 1, -1, more, tail);                                                                 \
    }
#endif

DOT_FWD(dot32f, 3, 0)
DOT_REV(dot32r, 3, 0)
DOT_FWD(dot64f, 7, 0)
DOT_REV(dot64r, 7, 0)

/* The run of one stage. K, Pi, Po are constants where it is expanded. */
#define STAGE_RUN(name, K, Pi, Po, tab, dotf, dotr)                                                             \
    static int name(struct rs48_stage *s, const float *in, int nin, float *out)                                 \
    {                                                                                                           \
        float *x = s->x + (K - 1);              /* x[0] = first new input, x[-1..-(K-1)] = history */           \
        for (int i = 0; i < nin; i++)                                                                           \
            x[i] = in[i];                                                                                       \
        int c = 0, b = s->b, p = s->p;                                                                          \
        while (b < nin) {                                                                                       \
            const float *xx = x + b - (K - 1);                                                                  \
            const int back = p >= (Pi + 1) / 2;                                                                 \
            const float *h = tab + (back ? Pi - p : p) * K;                                                     \
            p += Po;                                                                                            \
            while (p >= Pi) {                                                                                   \
                p -= Pi;                                                                                        \
                b++;                                                                                            \
            }                                                                                                   \
            const float *next = tab + (p >= (Pi + 1) / 2 ? Pi - 1 - p : p) * K;                                 \
            for (int i = 0; i < K; i += 8)      /* the cache has lost the tables since the last block */        \
                __builtin_prefetch(next + i);                                                                   \
            out[c++] = back ? dotr(xx, h) : dotf(xx, h);                                                        \
        }                                                                                                       \
        s->b = b - nin;                                                                                         \
        s->p = p;                                                                                               \
        for (int i = 0; i < K - 1; i++)                                                                         \
            s->x[i] = x[nin - (K - 1) + i];                                                                     \
        return c;                                                                                               \
    }

STAGE_RUN(run_up, RS_UP_K, UP_PI, UP_PO, t_up, dot32f, dot32r)
STAGE_RUN(run_dn, RS_DN_K, DN_PI, DN_PO, t_dn, dot64f, dot64r)

SDRAM_FUNC void rs48_init(struct rs48 *r)
{
    if (!designed) {
        design(pu, UP_N, 44100.0 * UP_PI, 24000.0, 90.0);   /* pass 20k, stop 28k */
        design(pd, DN_N, 48000.0 * DN_PI, 22050.0, 86.0);   /* pass 20k, stop 24.1k */
        build(t_up, pu, UP_PI, RS_UP_K);
        build(t_dn, pd, DN_PI, RS_DN_K);
        designed = 1;
    }
    stage_init(&r->up, r->x_up, sizeof r->x_up / 4);
    stage_init(&r->dn, r->x_dn, sizeof r->x_dn / 4);
    r->fifo_n = 0;
}

int rs48_up(struct rs48 *r, const float *in44, float *out48)
{
    return run_up(&r->up, in44, RS_FRAMES, out48);
}

void rs48_down(struct rs48 *r, const float *in48, int c, float *out44)
{
    r->fifo_n += run_dn(&r->dn, in48, c, r->fifo + r->fifo_n);
    for (int i = 0; i < RS_FRAMES; i++)
        out44[i] = r->fifo[i];
    r->fifo_n -= RS_FRAMES;
    for (int i = 0; i < r->fifo_n; i++)
        r->fifo[i] = r->fifo[RS_FRAMES + i];
}
