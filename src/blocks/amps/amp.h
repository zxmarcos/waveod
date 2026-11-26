/*
 * The pieces the original's amps share (docs/stock_port.md), as the original computes them: its filter sections in
 * their summing orders, the soft clippers, the 4-points-per-sample stage with its 40-tap anti-aliasing FIR, the
 * 3rd-order section the tone stacks and gain stages use. The families' own pieces are in brit.h, calif.h.
 */
#pragma once
#include "stock.h"

/* ---------------------------------------------------------------- filter sections */

/* a 2nd-order section: y = (b0 x + b1 x1 + b2 x2) - (a1 y1 + a2 y2), divided by a0 where the original does */
struct sos { float b0, b1, b2, a0, a1, a2; };
struct sos_state { float x1, x2, y1, y2; };

static inline float sos_out(struct sos_state *s, float x, float y)
{
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

/* each sum one product then the others fused, from b0 */
static inline float sos_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = c->b0 * x;
    n = st_fma(c->b1, s->x1, n);
    n = st_fma(c->b2, s->x2, n);
    float d = c->a1 * s->y1;
    d = st_fma(c->a2, s->y2, d);
    return sos_out(s, x, n - d);
}

/* sos_run over a whole buffer in place, the coefficients and the state in registers: when a chain of sections has
   the same coefficients for the block, section by section over the block gives the samples of sample by sample */
static inline void sos_block(const struct sos *c, struct sos_state *s, float *x, int frames)
{
    const float b0 = c->b0, b1 = c->b1, b2 = c->b2, a1 = c->a1, a2 = c->a2;
    float x1 = s->x1, x2 = s->x2, y1 = s->y1, y2 = s->y2;
    for (int i = 0; i < frames; i++) {
        float in = x[i];
        float n = b0 * in;
        n = st_fma(b1, x1, n);
        n = st_fma(b2, x2, n);
        float d = a1 * y1;
        d = st_fma(a2, y2, d);
        float y = n - d;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = y;
        x[i] = y;
    }
    s->x1 = x1;
    s->x2 = x2;
    s->y1 = y1;
    s->y2 = y2;
}

/* sos_run_div (below) over a whole buffer in place, as sos_block */
static inline void sos_block_div(const struct sos *c, struct sos_state *s, float *x, int frames)
{
    const float b0 = c->b0, b1 = c->b1, b2 = c->b2, a0 = c->a0, a1 = c->a1, a2 = c->a2;
    float x1 = s->x1, x2 = s->x2, y1 = s->y1, y2 = s->y2;
    for (int i = 0; i < frames; i++) {
        float in = x[i];
        float n = b0 * in;
        n = st_fma(b1, x1, n);
        n = st_fma(b2, x2, n);
        float d = a1 * y1;
        d = st_fma(a2, y2, d);
        float y = (n - d) / a0;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = y;
        x[i] = y;
    }
    s->x1 = x1;
    s->x2 = x2;
    s->y1 = y1;
    s->y2 = y2;
}

/* the same divided by a0 */
static inline float sos_run_div(const struct sos *c, struct sos_state *s, float x)
{
    float n = c->b0 * x;
    n = st_fma(c->b1, s->x1, n);
    n = st_fma(c->b2, s->x2, n);
    float d = c->a1 * s->y1;
    d = st_fma(c->a2, s->y2, d);
    return sos_out(s, x, (n - d) / c->a0);
}

/* divided by a0, the numerator summed from b1 (the presence shelf) */
static inline float sos_run_div_b1(const struct sos *c, struct sos_state *s, float x)
{
    float n = c->b1 * s->x1;
    n = st_fma(c->b0, x, n);
    n = st_fma(c->b2, s->x2, n);
    float d = c->a1 * s->y1;
    d = st_fma(c->a2, s->y2, d);
    return sos_out(s, x, (n - d) / c->a0);
}

/* the numerator summed from b1, not divided */
static inline float sos_run_b1(const struct sos *c, struct sos_state *s, float x)
{
    float n = c->b1 * s->x1;
    n = st_fma(c->b0, x, n);
    n = st_fma(c->b2, s->x2, n);
    float d = c->a1 * s->y1;
    d = st_fma(c->a2, s->y2, d);
    return sos_out(s, x, n - d);
}

/* a 1st-order section y = b0 x + b1 x1 - a1 y1, all fused after the first product */
struct fos { float b0, b1, a1; };
struct fos_state { float x1, y1; };

static inline float fos_run(const struct fos *c, struct fos_state *s, float x)
{
    float y = c->b0 * x;
    y = st_fma(c->b1, s->x1, y);
    y = st_fms(c->a1, s->y1, y);
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* the same divided by a0 */
static inline float fos_run_div(const struct fos *c, float a0, struct fos_state *s, float x)
{
    float y = c->b0 * x;
    y = st_fma(c->b1, s->x1, y);
    y = st_fms(c->a1, s->y1, y) / a0;
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* the same with the numerator summed from b1 */
static inline float fos_run_b1(const struct fos *c, struct fos_state *s, float x)
{
    float y = c->b1 * s->x1;
    y = st_fma(c->b0, x, y);
    y = st_fms(c->a1, s->y1, y);
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* the same divided by a0 */
static inline float fos_run_b1_div(const struct fos *c, float a0, struct fos_state *s, float x)
{
    float y = c->b1 * s->x1;
    y = st_fma(c->b0, x, y);
    y = st_fms(c->a1, s->y1, y) / a0;
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* x times a constant in double, as the original's scalings (0.15, 0.02) */
static inline float st_scale(float x, double k)
{
    return (float)((double)x * k);
}

/* ---------------------------------------------------------------- the gain stage */

/* the gain stage's numerator: the glided Gain times fixed factors, in double */
static inline void gain_set_d(float *b, int n, float g, const double *k)
{
    for (int i = 0; i < n; i++)
        b[i] = (float)((double)g * k[i]);
}

/* a 2nd-order gain section from the glided Gain g (the Voks 30TB and 15TB, the Bog XT Red): b0 = g^2 + k0 p,
   b1 = 2 (g - g^2), b2 = g^2 + k1 p, a0 = k2 p + k3 + g^2, a1 = b1 + k4, a2 = k5 p + k6 + g^2 (p = g; the Voks 15TB's
   Init: the taper before x 0.99, the factors x 0.99) */
static inline void gain2_set(struct sos *c, float g, float p, const float *k)
{
    float g2 = g * g;
    c->b0 = st_fma(p, k[0], g2);
    c->b1 = (g - g2) + (g - g2);
    c->b2 = st_fma(p, k[1], g2);
    c->a0 = st_fma(p, k[2], k[3]) + g2;
    c->a1 = c->b1 + k[4];
    c->a2 = st_fma(p, k[5], k[6]) + g2;
}

/* a 3rd-order section without a0 (the choruses' and delays' low passes): */
/* y = (b0 x + b1 x1 + b2 x2 + b3 x3) - (a1 y1 + a2 y2 + a3 y3), each sum from its first product */
struct lp3 { float b0, b1, b2, b3, a1, a2, a3; };
struct lp3_state { float x1, x2, x3, y1, y2, y3; };

static inline float lp3_run(const struct lp3 *c, struct lp3_state *s, float x)
{
    float n = c->b0 * x, d = c->a1 * s->y1;
    n = st_fma(c->b1, s->x1, n);
    n = st_fma(c->b2, s->x2, n);
    n = st_fma(c->b3, s->x3, n);
    d = st_fma(c->a2, s->y2, d);
    d = st_fma(c->a3, s->y3, d);
    float y = n - d;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

/* ---------------------------------------------------------------- the clipping stages */

/*
 * The clippers below compute both sides' formula for every x and pick, instead of branching: the same operations on
 * the same values. The picks look at x's bits with integer operations, not at float compares: a float compare reaches
 * a select through the FPSCR flags (VCMP, VMRS), which orders every point after the one before it; the integer
 * ones let the core overlap the points a stage clips (each a chain of dependent products).
 *   - the side: x's sign bit instead of x > 0. They differ only at +0, where both sides give +0.
 *   - beyond the limits (x >= lp: lp, x <= -ln: -ln): |x|'s bits against the limit's (positive floats order as
 *     their bits do).
 * A NaN x comes out as the limit (the original: NaN); the stages never see one.
 */
static inline float clip_pick2(float x, float pos, float neg, float lp, float ln)
{
    uint32_t xb = st_word(x), m = (uint32_t)((int32_t)xb >> 31);           /* all ones when the sign is set */
    uint32_t y = (st_word(pos) & ~m) | (st_word(neg) & m);
    uint32_t lim = (st_word(lp) & ~m) | (st_word(ln) & m);                  /* the side's limit, positive */
    uint32_t in = (uint32_t)((int32_t)((xb & 0x7fffffffu) - lim) >> 31);   /* all ones when |x| < it */
    return st_bits((y & in) | ((lim | (xb & 0x80000000u)) & ~in));
}

/* w < 0 ? -r : r, by w's bits (the sign bit of a nonzero w; at -0 r stays as it is) */
static inline float clip_sign(float r, float w)
{
    uint32_t wb = st_word(w), neg = (wb & 0x7fffffffu) != 0 ? wb & 0x80000000u : 0u;
    return st_bits(st_word(r) ^ neg);
}

static inline float clip_pick(float x, float pos, float neg, float L)
{
    return clip_pick2(x, pos, neg, L, L);
}

/* clip_exp's e^a for a <= 0 within the clipper's range (a > -84): st_expf's normal path (a's sign is set: the
   rounding's -0.5), its tiny arguments (|a| < 2^-24: a + 1) picked by a's bits; below -84 a is held there (VMAXNM;
   the clipper drops that point anyway) */
static inline float clip_expf(float a)
{
    a = __builtin_fmaxf(a, -84.0f);
    int k = (int)(a * 5.77078009f + -0.5f);
    float kf = (float)k;
    float r = a - kf * 0.173278809f;
    r = r - kf * 7.98654582e-6f;
    float p = 0.166729242f + r * 0.0416823104f;
    p = 0.49999997f + r * p;
    p = 0.99999994f + r * p;
    float e = st_exp_finish(k, p * r, 0);
    return (st_word(a) & 0x7fffffffu) < 0x33800000u ? a + 1.0f : e;
}

/* stage 1's clipper: L - L e^(-k |x|), odd, flat beyond +-L (L 50, k 0.3 in the Brit 50s; k L < 84) */
static inline float clip_exp(float x, float L, float k)
{
    float e = clip_expf(st_abs(x) * -k) * L;
    return clip_pick(x, L - e, e + -L, L);
}

/* stage 2's clipper: L - L (1 - k |x|)^1024 (about L - L e^(-1024 k |x|)), odd, flat beyond +-L (L 2, k 1 / 40.96
   in the Brit 50s; the original's L + L there is L x 2 to the bit) */
static inline float clip_pow(float x, float L, float k)
{
    float p = st_fma(st_abs(x), -k, 1.0f);
    for (int i = 0; i < 10; i++)
        p = p * p;
    p = p * L;
    return clip_pick(x, L - p, p + -L, L);
}

/* the anti-aliasing FIR: 40 taps, symmetric (tap k = tap 39 - k), the pairs summed in the original's order */
static const float AMP_FIR[20] = {
    -0.0011f, -0.0007f, 0.0001f, 0.0015f, 0.0032f, 0.0048f, 0.0053f, 0.0034f, -0.0016f, -0.0092f,
    -0.0175f, -0.0233f, -0.0227f, -0.0123f, 0.0097f, 0.0421f, 0.0807f, 0.1193f, 0.1504f, 0.1678f,
};
static const uint8_t AMP_FIR_ORDER[20] = { 0, 1, 2, 3, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };
/* the order of the Calif Star CL and Calif Bass */
static const uint8_t AMP_FIR_ORDER2[20] = { 0, 2, 1, 3, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

/* a clipping stage: the clipper on 4 points per sample (the previous sample, 1/3, 2/3, the current one; the
   previous one is the last sample's current one again), the FIR over the last 40 points */
struct stage {
    float prev;                                 /* the stage's input one sample ago */
    float pts[80];                              /* newest first from pts[at], twice over: the 40 always contiguous */
    uint32_t at;                                /* 0, 4, .. 36 */
};

/* the 4 new points (c, newest first) in, the window of the last 40 out (newest first): a ring stored twice instead of
   moving 36 points every sample, the same values */
static inline float *stage_push(struct stage *s, const float *c)
{
    s->at = s->at - 4u <= 32u ? s->at - 4u : 36u;   /* 0 (or anything else) wraps to 36: always inside pts */
    float *p = &s->pts[s->at];
#pragma clang loop unroll(full)
    for (int i = 0; i < 4; i++)
        p[i] = p[i + 40] = c[i];
    return p;
}

/* the 4 points of x after the previous input, in the order they enter the FIR, unclipped */
static inline void stage_points(struct stage *s, float x, float *q)
{
    float prev = s->prev, d = x - prev;
    q[0] = x;
    q[1] = st_fma(d, 0.6666667f, prev);
    q[2] = st_fma(d, 0.33333334f, prev);
    q[3] = prev;
    s->prev = x;
}

/* the FIR over the last 40 points after the 4 new ones (c), the pairs summed in `order` (most models:
   AMP_FIR_ORDER) */
static inline float stage_fir_ord(struct stage *s, const float *c, const uint8_t *order)
{
    const float *p = stage_push(s, c);
    int k0 = order[0];
    float acc = (p[k0] + p[39 - k0]) * AMP_FIR[k0];
#pragma clang loop unroll(full)                     /* `order` is a constant table: the indices become constants */
    for (int i = 1; i < 20; i++) {
        int k = order[i];
        acc = st_fma(p[k] + p[39 - k], AMP_FIR[k], acc);
    }
    return acc;
}

static inline float stage_fir(struct stage *s, const float *c)
{
    return stage_fir_ord(s, c, AMP_FIR_ORDER);
}

/* the clipper: +-L, (1 - k |x|)^1024 each side with its own k; L - L p above zero, L p - L below (both fused) */
static inline float clip_pow_asym(float x, float L, float kneg, float kpos)
{
    /* the side's k by the sign bit too (at +0 either k gives p = 1) */
    float p = st_fma(st_abs(x), (st_word(x) & 0x80000000u) ? -kneg : -kpos, 1.0f);
    for (int i = 0; i < 10; i++)
        p = p * p;
    return clip_pick(x, st_fms(p, L, L), st_fma(p, L, -L), L);
}

/* one bilinear shelf from p (the Bog SV's and B-Man's presence): e = 2^(x p), (1 + K e, c, 1 - K e) / (1 + K / e,
   c, 1 - K / e) */
static inline void shelf_set(struct sos *s, float p, float x, float K, float c)
{
    float e = st_exp2f(p * x);
    float n = e * K, d = K / e;
    s->b0 = n + 1.0f;
    s->b1 = c;
    s->b2 = 1.0f - n;
    s->a0 = d + 1.0f;
    s->a1 = c;
    s->a2 = 1.0f - d;
}

/* two bilinear shelves multiplied out (the Bog XT's, Dizzy's and Sol's presence): e1 = 2^x1, e2 = 2^x2, their
   corners K and G */
static inline void shelves_set_x(struct sos *c, float x1, float x2, float K, float G)
{
    float e1 = st_exp2f(x1), e2 = st_exp2f(x2);
    float a = 2.0f / (e1 + 1.0f), b = 2.0f / (1.0f / e1 + 1.0f);
    float cc = G / (1.0f / e2 + 1.0f), d = G / (e2 + 1.0f);
    float bk = K - b, bp = b + K, cm = cc + -1.0f, cp = cc + 1.0f;
    float ak = K - a, ap = a + K, dp = d + 1.0f, dm = d + -1.0f;
    float den = ap * dp;
    c->b0 = (bp * cm) / den;
    c->b1 = st_fma(bp, cp, bk * cm) / den;
    c->b2 = (bk * cp) / den;
    c->a0 = 1.0f;
    c->a1 = st_fma(ap, dm, ak * dp) / den;
    c->a2 = (ak * dm) / den;
}

/* the same from p: x1 = c1 p, x2 = c2 p */
static inline void shelves_set(struct sos *c, float p, float c1, float c2, float K, float G)
{
    shelves_set_x(c, p * c1, p * c2, K, G);
}

/* a power clipper with its own limit and k each side: lp - lp (1 - kp |x|)^1024 above zero, ln (1 - kn |x|)^1024 - ln
   below (fused), held at +lp / -ln; with a limit of 1 the original fuses the last squaring instead (1 - q q above,
   q q - 1 below, q the 512th power) */
static inline float clip_pow2(float x, float lp, float ln, float kp, float kn)
{
    /* both ends computed (p the 512th power, its square the 1024th), each side's picked by its limit (at +0
       every one gives +0) */
    int neg = (st_word(x) & 0x80000000u) != 0;
    float p = st_fma(st_abs(x), neg ? -kn : -kp, 1.0f);
    for (int i = 0; i < 9; i++)
        p = p * p;
    float p2 = p * p;
    float pos = lp == 1.0f ? st_fma(-p, p, 1.0f) : st_fms(p2, lp, lp);
    float nv = ln == 1.0f ? st_fma(p, p, -1.0f) : st_fma(p2, ln, -ln);
    return clip_pick2(x, pos, nv, lp, ln);
}

/*
 * A stage over a whole block, in place: y[i] becomes what the stage gives sample by sample (stage_points, the clipper
 * on each point, stage_fir: the same points, the same FIR sums), when nothing else changes the clipper during the
 * block. In two passes over a buffer of points
 * in time order (the 40 the ring holds, then 4 per sample: prev, 1/3, 2/3, x):
 *   STAGE_BLOCK_POINTS  every sample's points clipped (3 per sample, the previous x's again for prev; the block's
 *                       first prev clipped again, so CLIP may depend on knobs that change between blocks), two
 *                       samples per pass of the loop: six independent clipper chains for the core to overlap;
 *   stage_block_fir     the FIR of four samples at a time, interleaved (each one's sum in its own order), and the
 *                       last 40 points back into the ring.
 * The ring is left as stage_push leaves it.
 */
#define STAGE_BLOCK_PTS (40 + 4 * WVFX_MAX_FRAMES)

#define STAGE_BLOCK_POINTS(st, y, n, buf, CLIP) do {                                                                \
        struct stage *s_ = (st);                                                                                    \
        const float *r_ = &s_->pts[s_->at];                                                                         \
        for (int j_ = 0; j_ < 40; j_++)                                                                             \
            (buf)[39 - j_] = r_[j_];                                                                                \
        float prev_ = s_->prev, c_, v = prev_;                                                                      \
        c_ = (CLIP);                    /* the first prev clipped here: the clipper may have changed since */      \
        _Pragma("clang loop unroll_count(2)")                                                                       \
        for (int i_ = 0; i_ < (n); i_++) {                                                                          \
            float x_ = (y)[i_], d_ = x_ - prev_, *o_ = (buf) + 40 + 4 * i_;                                         \
            o_[0] = c_;                                                                                             \
            v = st_fma(d_, 0.33333334f, prev_);                                                                     \
            o_[1] = (CLIP);                                                                                         \
            v = st_fma(d_, 0.6666667f, prev_);                                                                      \
            o_[2] = (CLIP);                                                                                         \
            v = x_;                                                                                                 \
            c_ = (CLIP);                                                                                            \
            o_[3] = c_;                                                                                             \
            prev_ = x_;                                                                                             \
        }                                                                                                           \
        s_->prev = prev_;                                                                                           \
    } while (0)

/* the FIR at the point e (the sample's newest), the pairs summed in `order` */
static inline float stage_block_fir1(const float *e, const uint8_t *order)
{
    int k0 = order[0];
    float acc = (e[-k0] + e[k0 - 39]) * AMP_FIR[k0];
#pragma clang loop unroll(full)
    for (int j = 1; j < 20; j++) {
        int k = order[j];
        acc = st_fma(e[-k] + e[k - 39], AMP_FIR[k], acc);
    }
    return acc;
}

static inline void stage_block_fir_ord(struct stage *s, const float *buf, float *y, int n, const uint8_t *order)
{
    enum { W = 4 };                             /* on the pedal 2 and 8 cost about the same, 1 (one at a time) 80 % more */
    int i = 0;
    for (; i + W <= n; i += W) {
        const float *e = buf + 43 + 4 * i;
        float a[W];
        int k0 = order[0];
#pragma clang loop unroll(full)
        for (int w = 0; w < W; w++)
            a[w] = (e[4 * w - k0] + e[4 * w + k0 - 39]) * AMP_FIR[k0];
#pragma clang loop unroll(full)
        for (int j = 1; j < 20; j++) {
            int k = order[j];
            float f = AMP_FIR[k];
#pragma clang loop unroll(full)
            for (int w = 0; w < W; w++)
                a[w] = st_fma(e[4 * w - k] + e[4 * w + k - 39], f, a[w]);
        }
#pragma clang loop unroll(full)
        for (int w = 0; w < W; w++)
            y[i + w] = a[w];
    }
    for (; i < n; i++)
        y[i] = stage_block_fir1(buf + 43 + 4 * i, order);
    /* the newest 40 points, newest first, at at = 0 (and again 40 on) */
    const float *last = buf + 4 * n + 39;
    for (int j = 0; j < 40; j++)
        s->pts[j] = s->pts[j + 40] = last[-j];
    s->at = 0;
}

static inline void stage_block_fir(struct stage *s, const float *buf, float *y, int n)
{
    stage_block_fir_ord(s, buf, y, n, AMP_FIR_ORDER);
}

/* the same with every pair fused onto acc[i], the first one too (the drives' FIR, summed onto their clean signal) */
static inline void stage_block_fir_acc(struct stage *s, const float *buf, float *y, int n, const uint8_t *order,
                                       const float *acc)
{
    enum { W = 4 };
    int i = 0;
    for (; i + W <= n; i += W) {
        const float *e = buf + 43 + 4 * i;
        float a[W];
#pragma clang loop unroll(full)
        for (int w = 0; w < W; w++)
            a[w] = acc[i + w];
#pragma clang loop unroll(full)
        for (int j = 0; j < 20; j++) {
            int k = order[j];
            float f = AMP_FIR[k];
#pragma clang loop unroll(full)
            for (int w = 0; w < W; w++)
                a[w] = st_fma(e[4 * w - k] + e[4 * w + k - 39], f, a[w]);
        }
#pragma clang loop unroll(full)
        for (int w = 0; w < W; w++)
            y[i + w] = a[w];
    }
    for (; i < n; i++) {
        const float *e = buf + 43 + 4 * i;
        float a = acc[i];
#pragma clang loop unroll(full)
        for (int j = 0; j < 20; j++) {
            int k = order[j];
            a = st_fma(e[-k] + e[k - 39], AMP_FIR[k], a);
        }
        y[i] = a;
    }
    const float *last = buf + 4 * n + 39;
    for (int j = 0; j < 40; j++)
        s->pts[j] = s->pts[j + 40] = last[-j];
    s->at = 0;
}

/* the stages with clip_exp and clip_pow over a block */
static inline void stage_exp_block(struct stage *s, float *y, int n, float L, float k)
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(s, y, n, buf, clip_exp(v, L, k));
    stage_block_fir(s, buf, y, n);
}

static inline void stage_pow_block(struct stage *s, float *y, int n, float L, float k)
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(s, y, n, buf, clip_pow(v, L, k));
    stage_block_fir(s, buf, y, n);
}

/* with clip_pow2 (the Dizzy VH's two stages, the Dragon CL's +3 / -2.8) and clip_pow_asym (the Flyman's) */
static inline void stage_pow2_block(struct stage *s, float *y, int n, float lp, float ln, float kp, float kn)
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(s, y, n, buf, clip_pow2(v, lp, ln, kp, kn));
    stage_block_fir(s, buf, y, n);
}

static inline void stage_pow_asym_block(struct stage *s, float *y, int n, float L, float kneg, float kpos)
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(s, y, n, buf, clip_pow_asym(v, L, kneg, kpos));
    stage_block_fir(s, buf, y, n);
}

/* with a clipper function over a block */
static inline void stage_clip_block(struct stage *s, float *y, int n, float (*clip)(float))
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(s, y, n, buf, clip(v));
    stage_block_fir(s, buf, y, n);
}

/* an envelope: |x| through a 2nd-order low pass y = b0 |x| - (a1 y1 + a2 y2), the last product fused */
struct env_state { float y1, y2; };

static inline float env_run(struct env_state *e, float x, float b0, float a1, float a2)
{
    float d = e->y1 * a1;
    d = st_fma(e->y2, a2, d);
    float y = st_fma(b0, st_abs(x), -d);
    e->y2 = e->y1;
    e->y1 = y;
    return y;
}

/* the envelope the B-Man and the Dark Double share */
static inline float amp_env_run(struct env_state *e, float x)
{
    return env_run(e, x, 2.4700448193470947e-05f, -1.9900600910186768f, 0.9900847673416138f);
}

/* the envelope-driven clipper: a - a e^(-k |x|) (fused, odd) */
static inline float clip_env(float x, float k, float a)
{
    if (x > 0.0f)
        return st_fma(-st_expf(x * -k), a, a);
    return st_fma(st_expf(x * k), a, -a);
}

/*
 * clip_env without branches, for the stage's 4 points to overlap: e^(-|x| k) by clip_expf (x * -k above zero and
 * x * k below are both -(|x| k), and at +-0 e = 1 either way), the side by x's sign bit (at +-0 both sides give 0).
 * clip_expf holds only for -84 < -|x| k <= 0: outside it (a huge x, a k below zero, NaN) *off is set and the caller
 * takes clip_env.
 */
static inline float clip_env_fast(float x, float k, float a, uint32_t *off)
{
    float t = st_abs(x) * -k;
    uint32_t tb = st_word(t);
    *off |= (uint32_t)((tb & 0x7fffffffu) >= 0x42a80000u) | (uint32_t)((int32_t)tb > 0);
    float e = clip_expf(t);
    uint32_t m = (uint32_t)((int32_t)st_word(x) >> 31);
    return st_bits((st_word(st_fma(-e, a, a)) & ~m) | (st_word(st_fma(e, a, -a)) & m));
}

/* e^(-k |q|) as st_expf(q > 0 ? -k q : k q) gives it (the arguments agree but for the sign of a zero, where e = 1
   either way), by clip_expf, st_expf itself where clip_expf does not hold */
static inline float exp_negabs(float q, float k)
{
    float t = st_abs(q) * -k;
    uint32_t tb = st_word(t);
    if (__builtin_expect((tb & 0x7fffffffu) >= 0x42a80000u || (int32_t)tb > 0, 0))
        return st_expf(t);
    return clip_expf(t);
}

/* st_expf(t) for any t, by clip_expf where it holds (-84 < t <= 0) */
static inline float exp_np(float t)
{
    uint32_t tb = st_word(t);
    if (__builtin_expect((tb & 0x7fffffffu) >= 0x42a80000u || (int32_t)tb > 0, 0))
        return st_expf(t);
    return clip_expf(t);
}

/* x > 0 ? pos : neg by x's sign bit (they differ at +0 only: for clippers whose sides both give 0 there) */
static inline float pick_sign(float x, float pos, float neg)
{
    uint32_t m = (uint32_t)((int32_t)st_word(x) >> 31);
    return st_bits((st_word(pos) & ~m) | (st_word(neg) & m));
}

/* c with q's sign bit (q > 0 ? c : -c but at +0, where the clippers using it give 0 with either) */
static inline float sign_of(float c, float q)
{
    return st_bits(st_word(c) ^ (st_word(q) & 0x80000000u));
}

/* clip_env by clip_env_fast, clip_env itself where that does not hold */
static inline float clip_env1(float x, float k, float a)
{
    uint32_t off = 0;
    float y = clip_env_fast(x, k, a, &off);
    if (__builtin_expect(off != 0, 0))
        y = clip_env(x, k, a);
    return y;
}

/* L - L e^(-k |x|) without the limits (the Calif Bass's: L 40, k 0.25), the same way */
static inline float clip_exp_open(float x, float L, float k)
{
    float t = st_abs(x) * -k;
    if (__builtin_expect((st_word(t) & 0x7fffffffu) >= 0x42a80000u || (int32_t)st_word(t) > 0, 0)) {
        float e = st_expf(t) * L;
        return x > 0.0f ? L - e : e + -L;
    }
    float e = clip_expf(t) * L;
    uint32_t m = (uint32_t)((int32_t)st_word(x) >> 31);
    return st_bits((st_word(L - e) & ~m) | (st_word(e + -L) & m));
}

/* the envelope stage over a block in place, k[i] the sample's k (set from an envelope by the caller): each point
   through clip_env, then x M = max(1 / (1 + 3 k), floor) (the Star OD: k = 5 + 17 env, floor 0.04, a 0.1; the Voks
   30N: k = 0.3 + 1.5 env, floor 0.17; the Bog SV CL: k = 1 + env, floor 0.1, a 0.05). k changes every sample: all 4
   points are clipped (none is the last one's again); then the FIR four samples at a time */
static inline void stage_env_block(struct stage *s, float *y, const float *k, int n, float floor, float a)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, ki = k[i];
        float q[4] = { x, st_fma(d, 0.6666667f, prev), st_fma(d, 0.33333334f, prev), prev }, c[4];
        uint32_t off = 0;
        prev = x;
#pragma clang loop unroll(full)
        for (int j = 0; j < 4; j++)
            c[j] = clip_env_fast(q[j], ki, a, &off);
        if (__builtin_expect(off != 0, 0)) {
#pragma clang loop unroll(full)
            for (int j = 0; j < 4; j++)
                c[j] = clip_env(q[j], ki, a);
        }
        float m = 1.0f / st_fma(ki, 3.0f, 1.0f);
        m = m > floor ? m : floor;
        float *o = buf + 40 + 4 * i;            /* in time order: prev, 1/3, 2/3, x */
        o[0] = c[3] * m;
        o[1] = c[2] * m;
        o[2] = c[1] * m;
        o[3] = c[0] * m;
    }
    s->prev = prev;
    stage_block_fir(s, buf, y, n);
}

/*
 * The 8x oversampled stage (the Supero's and Superb's): each sample, 8 points interpolated from the last 5 inputs
 * (its 40-tap FIR as a polyphase filter), each through the model's clipper, the same FIR over the last 40 clipped
 * points back to one sample. Its FIR is another set than AMP_FIR.
 */
static const float OS8_FIR[20] = {
    -4.17e-05f, -0.0005088f, -0.0011711f, -0.0021369f, -0.0034084f, -0.0048301f, -0.006067f, -0.0066209f,
    -0.0058887f, -0.0032559f, 0.0017899f, 0.0095427f, 0.0199836f, 0.0327264f, 0.0470175f, 0.0617957f,
    0.0758057f, 0.0877509f, 0.0964619f, 0.1010551f,
};

static inline float os8_tap(int n) { return OS8_FIR[n < 20 ? n : 39 - n]; }

struct os8 {
    float in[5];        /* the inputs, newest first */
    float pts[80];      /* the clipped points, newest first from pts[at], twice over (as struct stage's) */
    uint32_t at;        /* 0, 8, .. 32 */
};

/*
 * The stage over a whole block in place: each sample's 8 points (point k of 8, k = 0 the oldest: the inputs through
 * taps k, 8 + k, ..., summed from the middle one), clipped, into a buffer in time order after the 40 the ring holds;
 * then the FIR over the last 40 points of four samples at a time, each sum from the pair (newest, oldest) in the
 * original's order; the last inputs and points back.
 */
static inline void os8_block(struct os8 *s, float *y, int n, float (*clip)(float))
{
    float buf[40 + 8 * WVFX_MAX_FRAMES], in[4 + WVFX_MAX_FRAMES];
    if (n <= 0)
        return;
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    for (int j = 0; j < 4; j++)
        in[j] = s->in[3 - j];
    for (int i = 0; i < n; i++)
        in[4 + i] = y[i];
    for (int i = 0; i < n; i++) {
        const float *c = in + 4 + i;            /* c[0] the input, c[-1] .. c[-4] the ones before */
        float *o = buf + 40 + 8 * i;
#pragma clang loop unroll(full)
        for (int k = 0; k < 8; k++) {
            float v = c[-2] * os8_tap(16 + k);
            v = st_fma(c[0], os8_tap(k), v);
            v = st_fma(c[-1], os8_tap(8 + k), v);
            v = st_fma(c[-3], os8_tap(24 + k), v);
            v = st_fma(c[-4], os8_tap(32 + k), v);
            o[k] = clip(v);
        }
    }
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        const float *e = buf + 47 + 8 * i;      /* sample i's newest point: the k-th newest is e[-k] */
        float a[4];
#pragma clang loop unroll(full)
        for (int w = 0; w < 4; w++)
            a[w] = (e[8 * w - 39] + e[8 * w]) * OS8_FIR[0];
#pragma clang loop unroll(full)
        for (int k = 1; k < 20; k++) {
#pragma clang loop unroll(full)
            for (int w = 0; w < 4; w++)
                a[w] = st_fma(e[8 * w + k - 39] + e[8 * w - k], OS8_FIR[k], a[w]);
        }
#pragma clang loop unroll(full)
        for (int w = 0; w < 4; w++)
            y[i + w] = a[w];
    }
    for (; i < n; i++) {
        const float *e = buf + 47 + 8 * i;
        float acc = (e[-39] + e[0]) * OS8_FIR[0];
#pragma clang loop unroll(full)
        for (int k = 1; k < 20; k++)
            acc = st_fma(e[k - 39] + e[-k], OS8_FIR[k], acc);
        y[i] = acc;
    }
    for (int j = 0; j < 5; j++)
        s->in[j] = in[3 + n - j];
    const float *last = buf + 8 * n + 39;
    for (int j = 0; j < 40; j++)
        s->pts[j] = s->pts[j + 40] = last[-j];
    s->at = 0;
}

/* the Brit 50s' stages: +-50 (k 0.3) and +-2 (k 1 / 40.96) */
static inline void stage50_block(struct stage *s, float *y, int n) { stage_exp_block(s, y, n, 50.0f, 0.3f); }
static inline void stage2_block(struct stage *s, float *y, int n) { stage_pow_block(s, y, n, 2.0f, 0.024414062f); }

/* ---------------------------------------------------------------- tone stack */

/* 3rd order: y = (b1 x1 + b2 x2 + b0 x + b3 x3 - (a1 y1 + a2 y2 + a3 y3)) / a0, in the original's order */
struct tone3 {
    float b[4], a[4];
    float x[3], y[3];
};

static inline float tone3_out(struct tone3 *t, float x, float n)
{
    float d = t->a[1] * t->y[0];
    d = st_fma(t->a[2], t->y[1], d);
    d = st_fma(t->a[3], t->y[2], d);
    float y = (n - d) / t->a[0];
    t->x[2] = t->x[1]; t->x[1] = t->x[0]; t->x[0] = x;
    t->y[2] = t->y[1]; t->y[1] = t->y[0]; t->y[0] = y;
    return y;
}

static inline float tone3_run(struct tone3 *t, float x)
{
    float n = t->b[1] * t->x[0];
    n = st_fma(t->b[2], t->x[1], n);
    n = st_fma(t->b[0], x, n);
    n = st_fma(t->b[3], t->x[2], n);
    return tone3_out(t, x, n);
}

/* the same with the numerator summed b0, b1, b2, b3 (a 3rd-order gain stage) */
static inline float tone3_run_std(struct tone3 *t, float x)
{
    float n = t->b[0] * x;
    n = st_fma(t->b[1], t->x[0], n);
    n = st_fma(t->b[2], t->x[1], n);
    n = st_fma(t->b[3], t->x[2], n);
    return tone3_out(t, x, n);
}

/* the same with the numerator summed b1, b0, b2, b3 */
static inline float tone3_run_b1b0(struct tone3 *t, float x)
{
    float n = t->b[1] * t->x[0];
    n = st_fma(t->b[0], x, n);
    n = st_fma(t->b[2], t->x[1], n);
    n = st_fma(t->b[3], t->x[2], n);
    return tone3_out(t, x, n);
}

/* ---------------------------------------------------------------- the classic tone stack */

/*
 * The tone stack's coefficients as the original computes them: polynomials in Bass (b, after its taper), Middle
 * (m) and Treble (t), the same shape in the Brit and Calif amps, its constants per model (the circuit's values
 * folded in):
 *   n0 = b nb0 + nk0 + m nm0 + m b mb0 + t nt0 + t b + m^2 m20 - 0.025 t m
 *   n1 = m nm1 + nk1 - (b nb1 + 3 t b) + m b mb1 + t nt1 + 0.075 t m + m^2 m21
 *   n2 = -nk0 - b nb0 + m nm2 + m b mb2 + 3 t b + t nt2 + m^2 m22 - 0.075 t m
 *   n3 = b nb1 - nk1 + m nm3 + m b mb3 + t nt3 - t b + 0.025 t m + m^2 m23
 *   di = b dbi + dki + m dmi + m b mbi + m^2 m2i
 * summed left to right, the products by m and t (and by b in the d's) fused.
 */
struct brit_ts {
    float nb0, nk0, nb1, nk1;
    float nm[4], nt[4], mb[4], m2[4];
    float db[4], dk[4], dm[4];
};

/* in single precision */
static inline void tone_brit_f(struct tone3 *o, const struct brit_ts *c, float b, float m, float t)
{
    float mb = m * b, tb = t * b, tm = t * m, m2 = m * m, tb3 = tb * 3.0f;
    float bn0 = b * c->nb0, bn1 = b * c->nb1;

    float n0 = st_fma(m, c->nm[0], bn0 + c->nk0);
    n0 = st_fma(t, c->nt[0], n0 + mb * c->mb[0]);
    n0 = ((n0 + tb) + m2 * c->m2[0]) - tm * 0.025f;

    float n1 = st_fma(m, c->nm[1], c->nk1) - (bn1 + tb3);
    n1 = st_fma(t, c->nt[1], n1 + mb * c->mb[1]);
    n1 = (n1 + tm * 0.075f) + m2 * c->m2[1];

    float n2 = st_fma(m, c->nm[2], -c->nk0 - bn0);
    n2 = st_fma(t, c->nt[2], (n2 + mb * c->mb[2]) + tb3);
    n2 = (n2 + m2 * c->m2[2]) - tm * 0.075f;

    float n3 = st_fma(m, c->nm[3], bn1 - c->nk1);
    n3 = st_fma(t, c->nt[3], n3 + mb * c->mb[3]);
    n3 = ((n3 - tb) + tm * 0.025f) + m2 * c->m2[3];

    float d0 = st_fma(m, c->dm[0], st_fma(b, c->db[0], c->dk[0]) + mb * c->mb[0]) + m2 * c->m2[0];
    float d1 = (st_fma(m, c->dm[1], st_fma(b, c->db[1], c->dk[1])) + mb * c->mb[1]) + m2 * c->m2[1];
    float d2 = st_fma(m, c->dm[2], st_fma(b, c->db[2], c->dk[2]) + mb * c->mb[2]) + m2 * c->m2[2];
    float d3 = (st_fma(m, c->dm[3], st_fma(b, c->db[3], c->dk[3])) + mb * c->mb[3]) + m2 * c->m2[3];

    o->b[0] = n0; o->b[1] = n1; o->b[2] = n2; o->b[3] = n3;
    o->a[0] = d0; o->a[1] = d1; o->a[2] = d2; o->a[3] = d3;
}

/* the same in double from single-precision knobs; m^2 and the t b of n0 are taken in single precision, n3's t
   terms fused as (nt3 - b) t */
struct brit_ts_d {
    double nb0, nk0, nb1, nk1;
    double nm[4], nt[4], mb[4], m2[4];
    double db[4], dk[4], dm[4];
};

static inline void tone_brit_d(struct tone3 *o, const struct brit_ts_d *c, float bass, float middle, float treble)
{
    double b = bass, m = middle, t = treble;
    double mb = m * b, tb = t * b, tm = t * m;
    double m2 = (float)(middle * middle), tb1 = (float)(treble * bass);
    double tb3 = tb * 3.0, bn0 = b * c->nb0, bn1 = b * c->nb1;

    double n0 = st_fmad(m, c->nm[0], bn0 + c->nk0);
    n0 = st_fmad(t, c->nt[0], n0 + mb * c->mb[0]);
    n0 = ((n0 + tb1) + m2 * c->m2[0]) - tm * 0.025;

    double n1 = st_fmad(m, c->nm[1], c->nk1) - (bn1 + tb3);
    n1 = st_fmad(t, c->nt[1], n1 + mb * c->mb[1]);
    n1 = (n1 + tm * 0.075) + m2 * c->m2[1];

    double n2 = st_fmad(m, c->nm[2], -c->nk0 - bn0);
    n2 = st_fmad(t, c->nt[2], (n2 + mb * c->mb[2]) + tb3);
    n2 = (n2 + m2 * c->m2[2]) - tm * 0.075;

    double n3 = st_fmad(m, c->nm[3], bn1 - c->nk1);
    n3 = ((n3 + mb * c->mb[3]) + tm * 0.025) + m2 * c->m2[3];
    n3 = st_fmad(c->nt[3] - b, t, n3);

    double d0 = st_fmad(m, c->dm[0], st_fmad(b, c->db[0], c->dk[0]) + mb * c->mb[0]) + m2 * c->m2[0];
    double d1 = (st_fmad(m, c->dm[1], st_fmad(b, c->db[1], c->dk[1])) + mb * c->mb[1]) + m2 * c->m2[1];
    double d2 = st_fmad(m, c->dm[2], st_fmad(b, c->db[2], c->dk[2]) + mb * c->mb[2]) + m2 * c->m2[2];
    double d3 = (st_fmad(m, c->dm[3], st_fmad(b, c->db[3], c->dk[3])) + mb * c->mb[3]) + m2 * c->m2[3];

    o->b[0] = (float)n0; o->b[1] = (float)n1; o->b[2] = (float)n2; o->b[3] = (float)n3;
    o->a[0] = (float)d0; o->a[1] = (float)d1; o->a[2] = (float)d2; o->a[3] = (float)d3;
}

/* the constants, each written once for both precisions (ST_F appends f, ST_D leaves the double literal) */
#define ST_F(x) x##f
#define ST_D(x) x

/* ---------------------------------------------------------------- the full tone stack (Dragon, Sol) */

/*
 * The tone stack with Treble^2 terms (the Dragon and Sol amps), b = Bass, m = Middle, t = Treble after their tapers;
 * the constants per model in struct tmb_ts, in double, taken to single precision by the single-precision version:
 *   n0 = b nb0 + nk0 + t nt0 + t b + m^2 nm2_0 - tm t m + (b nmb0 + nmk0) m
 *   n1 = -nk1 - (b nb1 + 3 t b) + t nt1 + tm3 t m + m^2 nm2_1 + (b nmb1 + nmk1) m
 *   n2 = -nk0 - b nb0 + 3 t b + t nt2 + m^2 nm2_2 - tm3 t m + (b nmb2 + nmk2) m
 *   n3 = b nb1 + nk1 + t nt3 - t b + tm t m + m^2 nm2_3 + (b nmb3 + nmk3) m
 *   d0 = m m + k + m^2 m2 + e t^2 m^2 - f m b t^2 + (m mt2 + kt2 + b bt2) t^2 + (t tb + kb + (t tmb + mb) m) b
 *        + (m mt + kt + m^2 m2t) t                                   (d2 the same with e3, f3)
 *   d1 = b b + k - e3 t^2 m^2 + f3 m b t^2 + (b bt2 + kt2) t^2 + (b bt + kt) t + (t tm2 + km2) m^2
 *        + (b bm + km + t^2 t2m + (b btm + tm) t) m                  (d3 the same with e, f)
 * summed left to right: the b nb products and the t b, t m, t^2 m^2 and m b t^2 terms rounded, every other product
 * fused.
 */
struct tmb_d02 { double k, m, m2, kt2, mt2, bt2, kb, tb, mb, tmb, kt, mt, m2t; };
struct tmb_d13 { double k, b, kt2, bt2, kt, bt, km2, tm2, km, bm, t2m, tm, btm; };
struct tmb_ts {
    double nb0, nk0, nb1, nk1, tm, tm3;
    double nt[4], nm2[4], nmb[4], nmk[4];
    double e, e3, f, f3;
    struct tmb_d02 d0, d2;
    struct tmb_d13 d1, d3;
};

static inline float tmb_d02_f(const struct tmb_d02 *d, float b, float m, float t, float m2, float t2, float n)
{
    n = st_fma(st_fma(b, (float)d->bt2, st_fma(m, (float)d->mt2, (float)d->kt2)), t2, n);
    n = st_fma(st_fma(st_fma(t, (float)d->tmb, (float)d->mb), m, st_fma(t, (float)d->tb, (float)d->kb)), b, n);
    return st_fma(st_fma(m2, (float)d->m2t, st_fma(m, (float)d->mt, (float)d->kt)), t, n);
}

static inline float tmb_d13_f(const struct tmb_d13 *d, float b, float m, float t, float m2, float t2, float n)
{
    n = st_fma(st_fma(b, (float)d->bt2, (float)d->kt2), t2, n);
    n = st_fma(st_fma(b, (float)d->bt, (float)d->kt), t, n);
    n = st_fma(st_fma(t, (float)d->tm2, (float)d->km2), m2, n);
    return st_fma(st_fma(st_fma(b, (float)d->btm, (float)d->tm), t,
                         st_fma(t2, (float)d->t2m, st_fma(b, (float)d->bm, (float)d->km))), m, n);
}

/* in single precision (the Dragon CL and OD, the Sol 100 OD) */
static inline void tmb_tone_set(struct tone3 *o, const struct tmb_ts *c, float b, float m, float t)
{
    float m2 = m * m, t2 = t * t, t2m2 = t2 * m2, mb = m * b;
    float tb = t * b, tb3 = tb * 3.0f, tm = t * m, tm1 = tm * (float)c->tm, tm3 = tm * (float)c->tm3;
    float nb0 = b * (float)c->nb0, nb1 = b * (float)c->nb1, nk0 = (float)c->nk0, nk1 = (float)c->nk1;
    float e1 = t2m2 * (float)c->e, e3 = t2m2 * (float)c->e3;
    float f1 = mb * (float)c->f * t2, f3 = mb * (float)c->f3 * t2;
    float n;
#define TMB_M(i) st_fma(st_fma(b, (float)c->nmb[i], (float)c->nmk[i]), m, n)

    n = st_fma(t, (float)c->nt[0], nb0 + nk0) + tb;
    n = st_fma(m2, (float)c->nm2[0], n) - tm1;
    o->b[0] = TMB_M(0);
    n = st_fma(t, (float)c->nt[1], -nk1 - (nb1 + tb3)) + tm3;
    n = st_fma(m2, (float)c->nm2[1], n);
    o->b[1] = TMB_M(1);
    n = st_fma(t, (float)c->nt[2], (-nk0 - nb0) + tb3);
    n = st_fma(m2, (float)c->nm2[2], n) - tm3;
    o->b[2] = TMB_M(2);
    n = (st_fma(t, (float)c->nt[3], nb1 + nk1) - tb) + tm1;
    n = st_fma(m2, (float)c->nm2[3], n);
    o->b[3] = TMB_M(3);
#undef TMB_M

    n = st_fma(m2, (float)c->d0.m2, st_fma(m, (float)c->d0.m, (float)c->d0.k)) + e1 - f1;
    o->a[0] = tmb_d02_f(&c->d0, b, m, t, m2, t2, n);
    n = st_fma(b, (float)c->d1.b, (float)c->d1.k) - e3 + f3;
    o->a[1] = tmb_d13_f(&c->d1, b, m, t, m2, t2, n);
    n = st_fma(m2, (float)c->d2.m2, st_fma(m, (float)c->d2.m, (float)c->d2.k)) + e3 - f3;
    o->a[2] = tmb_d02_f(&c->d2, b, m, t, m2, t2, n);
    n = st_fma(b, (float)c->d3.b, (float)c->d3.k) - e1 + f1;
    o->a[3] = tmb_d13_f(&c->d3, b, m, t, m2, t2, n);
}

static inline double tmb_d02_d(const struct tmb_d02 *d, double b, double m, double t, double m2, double t2, double n)
{
    n = st_fmad(st_fmad(b, d->bt2, st_fmad(m, d->mt2, d->kt2)), t2, n);
    n = st_fmad(st_fmad(st_fmad(t, d->tmb, d->mb), m, st_fmad(t, d->tb, d->kb)), b, n);
    return st_fmad(st_fmad(m2, d->m2t, st_fmad(m, d->mt, d->kt)), t, n);
}

static inline double tmb_d13_d(const struct tmb_d13 *d, double b, double m, double t, double m2, double t2, double n)
{
    n = st_fmad(st_fmad(b, d->bt2, d->kt2), t2, n);
    n = st_fmad(st_fmad(b, d->bt, d->kt), t, n);
    n = st_fmad(st_fmad(t, d->tm2, d->km2), m2, n);
    return st_fmad(st_fmad(st_fmad(b, d->btm, d->tm), t, st_fmad(t2, d->t2m, st_fmad(b, d->bm, d->km))), m, n);
}

/* in double (the Dragon CL B, the Sol 100 CL): the knobs, m^2, t^2 and n0's t b in single precision, the rest in
   double; n3 regrouped as b nb1 + nk1 + tm t m + m^2 nm2_3 + (b nmb3 + nmk3) m + (nt3 - b) t */
static inline void tmb_tone_set_d(struct tone3 *o, const struct tmb_ts *c, float bass, float middle, float treble)
{
    double b = bass, m = middle, t = treble;
    double m2 = middle * middle, t2 = treble * treble, t2m2 = t2 * m2, mb = m * b;
    double tbf = treble * bass, tb3 = t * b * 3.0, tm = t * m, tm1 = tm * c->tm, tm3 = tm * c->tm3;
    double nb0 = b * c->nb0, nb1 = b * c->nb1;
    double e1 = t2m2 * c->e, e3 = t2m2 * c->e3;
    double f1 = mb * c->f * t2, f3 = mb * c->f3 * t2;
    double n;
#define TMB_M(i) st_fmad(st_fmad(b, c->nmb[i], c->nmk[i]), m, n)

    n = st_fmad(t, c->nt[0], nb0 + c->nk0) + tbf;
    n = st_fmad(m2, c->nm2[0], n) - tm1;
    o->b[0] = (float)TMB_M(0);
    n = st_fmad(t, c->nt[1], -c->nk1 - (nb1 + tb3)) + tm3;
    n = st_fmad(m2, c->nm2[1], n);
    o->b[1] = (float)TMB_M(1);
    n = st_fmad(t, c->nt[2], (-c->nk0 - nb0) + tb3);
    n = st_fmad(m2, c->nm2[2], n) - tm3;
    o->b[2] = (float)TMB_M(2);
    n = st_fmad(m2, c->nm2[3], (nb1 + c->nk1) + tm1);
    n = TMB_M(3);
    o->b[3] = (float)st_fmad(c->nt[3] - b, t, n);
#undef TMB_M

    n = st_fmad(m2, c->d0.m2, st_fmad(m, c->d0.m, c->d0.k)) + e1 - f1;
    o->a[0] = (float)tmb_d02_d(&c->d0, b, m, t, m2, t2, n);
    n = st_fmad(b, c->d1.b, c->d1.k) - e3 + f3;
    o->a[1] = (float)tmb_d13_d(&c->d1, b, m, t, m2, t2, n);
    n = st_fmad(m2, c->d2.m2, st_fmad(m, c->d2.m, c->d2.k)) + e3 - f3;
    o->a[2] = (float)tmb_d02_d(&c->d2, b, m, t, m2, t2, n);
    n = st_fmad(b, c->d3.b, c->d3.k) - e1 + f1;
    o->a[3] = (float)tmb_d13_d(&c->d3, b, m, t, m2, t2, n);
}
