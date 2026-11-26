/*
 * The pieces the original's drives share beyond amp.h (docs/stock_port.md): the rational soft clippers, the Warm OD's
 * two-curve clipper and a 3rd-order section.
 */
#pragma once
#include "../amps/amp.h"

/* the rational clipper: (n2 x^2 + n1 |x| + n3 |x|^3) / (d2 x^2 + d0 + |x|^3 + d1 |x|) with the sign of x (the
   Shark, the Blues OD) */
struct clip_rat { float n2, n1, n3, d2, d0, d1; };

static inline float clip_rat(const struct clip_rat *c, float x)
{
    float x2 = x * x, a = st_abs(x), x3 = x2 * a;
    float n = st_fma(a, c->n1, x2 * c->n2);
    float d = st_fma(a, c->d1, st_fma(x2, c->d2, c->d0) + x3);
    /* VFNMA below zero: -(x3 n3 + n) exactly; picked by x's bits, not a float compare (amp.h, clip_sign: at -0 the
       original's +0 stays) */
    n = clip_sign(st_fma(x3, c->n3, n), x);
    return n / d;
}

/* a second rational form: (n3 |x|^3 - n2 x^2 + n1 |x|) / (|x|^3 + d2 x^2 + d1 |x| + d0) with the sign of x (the Dark
   Mouse, the Bass Dist) */
struct clip_cub { float n3, n2, n1, d0, d2, d1; };

static inline float clip_cub(const struct clip_cub *c, float x)
{
    float x2 = x * x, a = st_abs(x), x3 = x2 * a;
    float n = st_fma(x3, c->n3, -(x2 * c->n2));
    n = clip_sign(st_fma(a, c->n1, n), x);      /* VFNMA below zero, by x's bits (amp.h, clip_sign) */
    float d = st_fma(a, c->d1, st_fma(x2, c->d2, c->d0) + x3);
    return n / d;
}

/* a FIR pair order several drives use: 0, 3, 2, 1, then 19 down to 4 */
static const uint8_t DRV_FIR_ORDER_0321[20] = { 0, 3, 2, 1, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4 };

/* the Butter OD's and Super OD's gain section and clipper: a 1st-order section from the Gain taper d, then on 4
   points per sample 0.005 x + 0.995 A (1 - e^-x) above zero, 0.005 x + 0.995 B (1 - e^(2 x)) below, A = 1.4 - 0.3 d,
   B = 0.15 d - 0.65 */
static inline void bod_gain_set(struct fos *c, float d)
{
    float t = d * 202.37869262695312f;
    c->b0 = t + 7.678496837615967f;
    c->b1 = -7.580856800079346f - t;
    c->a1 = -0.9023599028587341f;
}

/* the clipper on the three earlier points, and on the current one (v = 3 g, its terms grouped otherwise): e^-x above
   zero, e^(2 x) below, picked by x's sign bit, not a compare (at +0 both exponents are 0: e = 1, every term 0), A or B
   the same way; amp.h exp_np */
static inline float bod_clip_fast(float x, float A, float B)
{
    float e = exp_np(pick_sign(x, -x, x + x));
    return st_fma(pick_sign(x, A, B) * 0.995f, 1.0f - e, x * 0.004999995231628418f);
}

static inline float bod_clip_cur_fast(float g, float A, float B)
{
    float v = g * 3.0f;
    float e = exp_np(pick_sign(v, -v, g * 6.0f));
    return st_fma(st_fma(-e, 0.995f, 0.995f), pick_sign(v, A, B), g * 0.014999985694885254f);
}

/* the stage over a block in place (g the gain section's outputs, x the clean signal the FIR is summed onto): the
   points of 3 g, the current one from g itself (no point is the last one's again), then the FIR four samples at a
   time */
static inline void bod_stage_block(struct stage *s, float *g, int n, float d, const float *x)
{
    float A = st_fma(d, -0.30000001192092896f, 1.399999976158142f);
    float B = st_fma(d, 0.15000000596046448f, -0.6499999761581421f);
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float v = g[i] * 3.0f, dd = v - prev, *o = buf + 40 + 4 * i;
        o[0] = bod_clip_fast(prev, A, B);
        o[1] = bod_clip_fast(st_fma(dd, 0.33333334f, prev), A, B);
        o[2] = bod_clip_fast(st_fma(dd, 0.6666667f, prev), A, B);
        o[3] = bod_clip_cur_fast(g[i], A, B);
        prev = v;
    }
    s->prev = prev;
    stage_block_fir_acc(s, buf, g, n, AMP_FIR_ORDER2, x);
}

/* a 3rd-order section, each sum from its first term, divided by a0 */
struct sos3 { float b0, b1, b2, b3, a0, a1, a2, a3; };
struct sos3_state { float x1, x2, x3, y1, y2, y3; };

static inline float sos3_run_div(const struct sos3 *c, struct sos3_state *s, float x)
{
    float n = c->b0 * x, d = c->a1 * s->y1;
    n = st_fma(c->b1, s->x1, n);
    d = st_fma(c->a2, s->y2, d);
    n = st_fma(c->b2, s->x2, n);
    d = st_fma(c->a3, s->y3, d);
    n = st_fma(c->b3, s->x3, n);
    float y = (n - d) / c->a0;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

/* the same with the numerator summed from b1 x1 */
static inline float sos3_run_div_b1(const struct sos3 *c, struct sos3_state *s, float x)
{
    float n = c->b1 * s->x1, d = c->a1 * s->y1;
    n = st_fma(x, c->b0, n);
    d = st_fma(c->a2, s->y2, d);
    n = st_fma(c->b2, s->x2, n);
    d = st_fma(c->a3, s->y3, d);
    n = st_fma(c->b3, s->x3, n);
    float y = (n - d) / c->a0;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

/* the two-curve clipper over six ranges of x (a = |x|), its curves A and B blended by m: B + (A - B) m (the
   Warm OD, the Fuzz Cream) */
static inline float clip_warm(float x, float m)
{
    float a = st_abs(x), A, B;
    if (x < -4.0f) {
        float u = a / (a + 20.0f), u2;
        A = st_fma(st_fma(a, 6.695199317618972e-06f, -0.002491199877113104f), a,
                   st_fma(u, -0.5088000297546387f, -0.928659975528717f));
        B = st_fma(st_fma(a, 6.221499461389612e-06f, -0.001942700007930398f), a,
                   st_fma(u, -0.47870001196861267f, -0.6172000169754028f));
        u2 = u * u;
        A = st_fma(u2, 0.35034000873565674f, A);
        B = st_fma(u2, 0.31758999824523926f, B);
    } else if (x < -0.6000000238418579f) {
        float u = (a + 1.0f) / (a * 10.0f), u2;
        A = st_fma(st_fma(a, 0.00421757809817791f, -0.05276937410235405f), a,
                   st_fma(u, -2.5778000354766846f, -0.7287700176239014f));
        B = st_fma(st_fma(a, 0.0019821873866021633f, -0.029077500104904175f), a,
                   st_fma(u, 1.0627000331878662f, -0.7398999929428101f));
        u2 = u * u;
        A = st_fma(u2, 12.013699531555176f, A);
        B = st_fma(u2, 0.07924000173807144f, B);
    } else if (x < 0.0f) {
        float u = a / st_fma(a, 2.0f, 1.0f), x2 = x * x, u2 = u * u;
        A = st_fma(u2, 1.4809999465942383f, st_fma(u, 0.7321199774742126f,
                   st_fma(a, -1.7582999467849731f, x2 * 0.42555001378059387f)));
        B = st_fma(u2, 0.9658300280570984f, st_fma(u, 1.6452000141143799f,
                   st_fma(a, -2.5806000232696533f, x2 * 1.5563000440597534f)));
    } else if (x < 0.44999998807907104f) {
        float u = x / st_fma(x, 2.0f, 1.0f), x2 = x * x, u2 = u * u;
        A = st_fma(u2, -1.6991000175476074f, st_fma(u, -0.9537100195884705f,
                   st_fma(x2, -0.5924199819564819f, x * 1.9723999500274658f)));
        B = st_fma(u2, -8.832599639892578f, st_fma(u, -6.196000099182129f,
                   st_fma(x2, -4.497399806976318f, x * 7.22130012512207f)));
    } else if (x < 5.0f) {
        float u = (x + 1.0f) / (x * 10.0f), u2 = u * u;
        A = st_fma(u2, -5.532800197601318f, st_fma(u, 1.3762999773025513f, 0.5617799758911133f));
        B = st_fma(u2, -0.2247599959373474f, st_fma(u, -0.5240899920463562f, 0.5603500008583069f));
        A = st_fma(st_fma(a, -0.002705351449549198f, 0.035791873931884766f), a, A);
        B = st_fma(st_fma(a, -0.0010849999962374568f, 0.017771249637007713f), a, B);
    } else {
        float u = x / (x + 20.0f), u2;
        A = st_fma(st_fma(x, -3.2962000204861397e-06f, 0.001489299931563437f), x,
                   st_fma(u, 0.2861500084400177f, 0.7033200263977051f));
        B = st_fma(st_fma(x, -3.152599902023212e-06f, 0.0010362999746575952f), x,
                   st_fma(u, 0.27630001306533813f, 0.5042600035667419f));
        u2 = u * u;
        A = st_fma(u2, -0.17428000271320343f, A);
        B = st_fma(u2, -0.16398000717163086f, B);
    }
    return st_fma(A - B, m, B);
}
