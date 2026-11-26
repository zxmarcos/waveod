/*
 * Reverb (RVB › Reverb, waveod's): a light 16-line feedback-delay-network reverb, 50 types, docs/reverb.md.
 *
 * Per 16 frames:
 *   input L + R, faded in / out per frame (bypassed with Spillover the input leaves and the tail rings on; without,
 *   the tail is cut) -> + Echo x the predelay's output -> Low Cut / High Cut -> the predelay line; the reverb's
 *   input X = (1 - Echo) x (the predelay's output, or its input by type);
 *   early reflections: X -> an input delay -> a line, 8 + 8 taps L / R with gains (from Normal on through four
 *   Schroeder all-passes per side); the FDN's input X through four all-passes too (Normal on);
 *   16 lines: every Step-th line (Economy 4, Normal / High 2, Ultra 1) read linearly at a position moved by one of
 *   the LFOs L, R, -L, -R, the others at their delay -> per line the damping one-pole -> per pair a first-order low
 *   shelf -> the lines' inputs (X + F) + line, F = -2/16 x the sum of the lines (Householder); from High on also
 *   + the Hadamard transform of the 16 raw early taps x m/8, X x (1 - m/2);
 *   L = the lines with the odd ones negated, R = with lines 2, 3 mod 4 negated, x Late / 16 -> two tone one-poles
 *   -> + early reflections -> two peaking EQs (when they boost or cut) -> + Echo -> Spread (mid / side) -> the mix
 *   law (the wet full from Mix 50 %, the dry falling from it), level, ramped over one block.
 * Design, when a knob moves: per line of D frames the decay gains g = 10^(-3 D / (fs T)) at Time T, at T x the
 * type's high ratio and at T x its low ratio: the damping one-pole (pole (g - g_hf) / (g + g_hf), at most 0.9999,
 * gain g (1 - pole)) and the low shelf of DC gain g_lf / g (0.1..10), corner at the type's crossover; the output
 * tone shelf from their mean, and a pre-emphasis from the high ratio. The geometry (delays, taps, gains) is the
 * type's, at 48 kHz, scaled.
 *
 *   Type (50)  Time 0.1..100 s  Predelay 0..1000 ms  Mix %  Level dB  High Cut  Low Cut  Quality  Spillover
 */
#include "modfx/modfx.h"

#define N  16
#define NL 16

struct rt_type {
    float erg;
    uint8_t flags;                      /* 1: High Cut 12 dB/oct, 2: the reverb's input from the predelay's output */
    uint16_t df, delay[NL], erdl[8], erdr[8], apd[4], erapd[4];
    float ergl[8], ergr[8];
    float r13, early, eq1f, eq1q, eq1g, eq2f, eq2q, eq2g, lamp, lrate, ap, erap, mtx, p37, p38, width, efb, echo;
};
#include "reverb_types.h"
#define NTYPES ((int)(sizeof rt_types / sizeof rt_types[0]))

enum { K_TYPE, K_TIME, K_PREDELAY, K_MIX, K_LEVEL, K_HICUT, K_LOWCUT, K_QUALITY, K_SPILL, NKNOBS };

static const struct wvfx_param params[] = {
    { "Type", 0.0f, 49.0f, 1.0f, 5.0f, K_TYPE, 0, 0, 0, RT_NAMES, 0 },
    { "Time", 0.1f, 100.0f, 0.01f, 3.2f, K_TIME, 0, 0, 0, 0, "%.2f s" },
    { "Predelay", 0.0f, 1000.0f, 1.0f, 0.0f, K_PREDELAY, 0, 0, 0, 0, "%.0f ms" },
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 3950.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Low Cut", 20.0f, 2000.0f, 1.0f, 70.0f, K_LOWCUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Quality", 0.0f, 3.0f, 1.0f, 0.0f, K_QUALITY, 0, 0, 0, "Economy\0Normal\0High\0Ultra\0", 0 },
    { "Spillover", 0.0f, 1.0f, 1.0f, 1.0f, K_SPILL, 0, 0, 0, "Off\0On\0", 0 },
};

/* the lines, at 44.1 kHz (the geometry x 0.91875), sizes multiples of 16 */
#define SCALED(d) ((uint32_t)((float)(d) * (44100.0f / 48000.0f)) + 1u)
#define LEN16(n)  ((((uint32_t)(n)) + 15u) & ~15u)
#define PD_LEN    LEN16(44100u + 64u)
#define ER_LEN    LEN16(SCALED(RT_MAX_EARLY) + 64u)
#define DF_LEN    LEN16(SCALED(RT_MAX_INPUT) + 64u)
#define FDN_LEN   LEN16(SCALED(RT_MAX_LINE) + 1103u + 64u)   /* + the LFO's swing, 0.025 s */
#define AP_LEN(i) LEN16(SCALED(RT_MAX_AP##i) + 48u)
#define ERAP_LEN  LEN16(SCALED(RT_MAX_ERAP) + 48u)
#define BUF_FLOATS (PD_LEN + ER_LEN + DF_LEN + NL * FDN_LEN + AP_LEN(0) + AP_LEN(1) + AP_LEN(2) + AP_LEN(3) + 8u * ERAP_LEN)

struct line { float *buf; uint32_t size, w; };

/* y[i] = the sample `d` frames before frame i of this step (frame i is at w + i; d >= 16 if not written yet) */
static inline void line_read(const struct line *l, uint32_t d, float *y)
{
    int32_t r = (int32_t)l->w - (int32_t)d;
    if (r < 0)
        r += (int32_t)l->size;
    const float *b = l->buf;
    if ((uint32_t)r + N <= l->size) {
        for (int i = 0; i < N; i++)
            y[i] = b[r + i];
    } else {
        for (int i = 0; i < N; i++) {
            y[i] = b[r];
            if ((uint32_t)++r >= l->size)
                r = 0;
        }
    }
}

/* the same frames in place when they do not wrap (else copied into tmp) */
static inline const float *line_span(const struct line *l, uint32_t d, float *tmp)
{
    int32_t r = (int32_t)l->w - (int32_t)d;
    if (r < 0)
        r += (int32_t)l->size;
    if ((uint32_t)r + N <= l->size)
        return l->buf + r;
    line_read(l, d, tmp);
    return tmp;
}

static inline void line_write(struct line *l, const float *x)
{
    uint32_t w = l->w;
    float *b = l->buf;
    if (w + N <= l->size) {
        for (int i = 0; i < N; i++)
            b[w + i] = x[i];
        w += N;
        if (w >= l->size)
            w = 0;
    } else {
        for (int i = 0; i < N; i++) {
            b[w] = x[i];
            if (++w >= l->size)
                w = 0;
        }
    }
    l->w = w;
}

/* writing 16 frames in place: where to put them (the line itself when they do not wrap, else tmp), then commit */
static inline float *line_wspan(struct line *l, float *tmp)
{
    return l->w + N <= l->size ? l->buf + l->w : tmp;
}

static inline void line_commit(struct line *l, const float *p)
{
    if (p != l->buf + l->w) {
        line_write(l, p);
        return;
    }
    l->w += N;
    if (l->w >= l->size)
        l->w = 0;
}

struct design {
    const struct rt_type *t;
    int quality, step, eq1_on, eq2_on, spill;
    uint32_t pd;                        /* predelay, frames (>= 16) */
    uint32_t df, erdl[8], erdr[8], apd[4], erapd[4], delay[NL];
    float damp_g[NL], damp_p[NL];       /* the damping one-pole: gain, pole */
    float sh_b0[NL], sh_b1[NL], sh_a1[NL];
    struct op tone1, tone2;
    struct bq lowcut, hicut, eq1, eq2;
    float lfo_inc, gw, gd;
};

struct state {
    struct line pd, er, df, fdn[NL], ap[4], erap[8];
    float knobs[NKNOBS];
    int designed, idle, quiet, type;
    struct design d;
    float ph, in_g, hold_g;
    float ds[NL], ss[NL];               /* damping and shelf states */
    float y[NL][N];
    float s_lc[2], s_hc[2], s_t1[2], s_t2[2], s_eq1[2][2], s_eq2[2][2];
    float gw, gd, gt;
};

/* ---------------------------------------------------------------- design */

static struct bq peaking(float f, float q, float db, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    const float a = modfx_exp2(db * (0.16609640474f * 0.5f)), al = sn / (2.0f * q), n = 1.0f / (1.0f + al / a);
    return (struct bq){ (1.0f + al * a) * n, -2.0f * cs * n, (1.0f - al * a) * n, 2.0f * cs * n, -(1.0f - al / a) * n };
}

/* zero fz, pole fp, unity at the top */
static void fo_shelf(float fz, float fp, float fs, float *b0, float *b1, float *a1)
{
    const float kz = modfx_prewarp(fz, fs), kp = modfx_prewarp(fp, fs), g = 1.0f / (1.0f + kp);
    *b0 = (1.0f + kz) * g;
    *b1 = (kz - 1.0f) * g;
    *a1 = (1.0f - kp) * g;
}

static uint32_t scaled(uint32_t d) { return (uint32_t)((float)d * (44100.0f / 48000.0f) + 0.5f); }

static void design(struct state *s, const float *k, float fs)
{
    struct design *d = &s->d;
    const struct rt_type *t = &rt_types[modfx_option(k[K_TYPE], NTYPES)];
    d->t = t;
    d->quality = modfx_option(k[K_QUALITY], 4);
    d->step = d->quality == 0 ? 4 : d->quality == 3 ? 1 : 2;
    d->spill = k[K_SPILL] >= 0.5f;
    const float time = wvfx_clamp(k[K_TIME], 0.1f, 100.0f);
    const int pd = (int)(fs * (wvfx_clamp(k[K_PREDELAY], 0.0f, 1000.0f) * 0.001f)) - 16;
    d->pd = (uint32_t)(pd < 0 ? 0 : pd) + 16u;
    d->df = scaled(t->df);
    for (int i = 0; i < 8; i++) {
        d->erdl[i] = scaled(t->erdl[i]);
        d->erdr[i] = scaled(t->erdr[i]);
    }
    for (int i = 0; i < 4; i++) {
        d->apd[i] = scaled(t->apd[i]) + 16u;
        d->erapd[i] = scaled(t->erapd[i]) + 16u;
    }
    float rsum = 0.0f;
    for (int i = 0; i < NL; i++) {
        d->delay[i] = scaled(t->delay[i]);
        const float e = -3.0f * (float)d->delay[i] / (fs * time) * 3.32192809f;      /* log2 of 10^e */
        const float g = modfx_exp2(e), ghf = modfx_exp2(e / t->r13), glf = modfx_exp2(e / t->p37);
        float pole = (g - ghf) / (g + ghf);
        if (pole > 0.9999f)
            pole = 0.9999f;
        d->damp_g[i] = g * (1.0f - pole);
        d->damp_p[i] = pole;
        const float r = wvfx_clamp(glf / g, 0.1f, 10.0f), sq = modfx_pow(r, 0.5f);
        fo_shelf(t->p38 * sq, t->p38 / sq, fs, &d->sh_b0[i], &d->sh_b1[i], &d->sh_a1[i]);
        rsum += r;
    }
    {
        const float x = modfx_pow(rsum * (1.0f / NL), -0.25f);
        fo_shelf(t->p38 * x, t->p38 / x, fs, &d->tone2.a, &d->tone2.b, &d->tone2.c);
        const float g = modfx_pow(t->r13, 0.5f), c1 = 2.0f * g / (1.0f + g);
        d->tone1 = (struct op){ 1.0f / c1, (c1 - 1.0f) / c1, -0.0f };
    }
    const float lc = wvfx_clamp(k[K_LOWCUT], 20.0f, 2000.0f), hc = wvfx_clamp(k[K_HICUT], 200.0f, 20000.0f);
    d->lowcut = bq_of_op(op_hp(lc, fs));
    d->hicut = t->flags & 1 ? bq_rbj(0, hc, RT_CUT_Q, fs) : bq_of_op(op_lp(hc, fs));
    d->eq1_on = t->eq1g != 0.0f;
    d->eq2_on = t->eq2g != 0.0f;
    if (d->eq1_on)
        d->eq1 = peaking(t->eq1f, t->eq1q, t->eq1g, fs);
    if (d->eq2_on)
        d->eq2 = peaking(t->eq2f, t->eq2q, t->eq2g, fs);
    d->lfo_inc = t->lrate * MODFX_TWO_PI / fs;
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = (mix >= 0.5f ? 1.0f : modfx_wet_curve(2.0f * mix)) * level;
    d->gd = (mix <= 0.5f ? 1.0f : 2.0f - 2.0f * mix) * level;
}

/* ---------------------------------------------------------------- the stages */

/* four Schroeder all-passes (gain 0.707, delays given), then x = amt y + (1 - amt) x */
static void diffuse(struct line *ap, const uint32_t *dl, float amt, float *x)
{
    float cur[N], rt[N], wt[N];
    for (int i = 0; i < N; i++)
        cur[i] = x[i];
    for (int st = 0; st < 4; st++) {                    /* the delays are > 16: the read never meets the write */
        const float *dd = line_span(&ap[st], dl[st], rt);
        float *q = line_wspan(&ap[st], wt);
        for (int i = 0; i < N; i++) {
            const float w = dd[i] * -0.707000017f + cur[i];
            cur[i] = w * 0.707000017f + dd[i];
            q[i] = w;
        }
        line_commit(&ap[st], q);
    }
    const float k = 1.0f - amt;
    for (int i = 0; i < N; i++)
        x[i] = amt * cur[i] + k * x[i];
}

/* a modulated line: linear between the frames at 16 + floor(pos) and the next, pos = lfo 0.025 fs + D - 16 */
static void line_lerp(const struct line *l, const float *lfo, float dk, float fs, float *y)
{
    const float *b = l->buf;
    const int32_t size = (int32_t)l->size;
    for (int i = 0; i < N; i++) {
        float pos = (lfo[i] * 0.025f) * fs + dk;
        if (pos < 0.0f)
            pos = 0.0f;
        const int ip = (int)pos;
        const float fr = pos - (float)ip;
        int32_t r = (int32_t)l->w + i - 16 - ip;        /* ip < size: one turn at most */
        if (r < 0)
            r += size;
        const int32_t r1 = r ? r - 1 : size - 1;
        const float q = b[r];
        y[i] = q - (q - b[r1]) * fr;
    }
}

static void step(struct state *s, float *x, int on)
{
    const struct design *d = &s->d;
    const struct rt_type *t = d->t;
    const float fs = MODFX_FS;
    float lfo[4][N], m[N], e[N], xin[N], dd[N], erl[N], err[N], z[16][N];
    /* the LFO: sine, the right a quarter turn ahead; the lines use L, R, -L, -R */
    s->ph = modfx_rotate(s->ph, d->lfo_inc, N, lfo[0], lfo[1]);
    for (int i = 0; i < N; i++) {
        lfo[0][i] *= t->lamp;
        lfo[1][i] *= t->lamp;
        lfo[2][i] = -lfo[0][i];
        lfo[3][i] = -lfo[1][i];
    }
    /* the input */
    {
        const float a = RT_IN_R, kk = on ? 1.0f - a : 0.0f;
        float g = s->in_g;
        for (int i = 0; i < N; i++) {
            g = kk + a * g;
            m[i] = (x[2 * i] + x[2 * i + 1]) * g;
        }
        s->in_g = g;
    }
    s->hold_g = 0.1f + 0.9f * s->hold_g;
    for (int i = 0; i < N; i++)
        m[i] *= s->hold_g;
    /* the predelay: read first (>= 16 frames), the echo, the cuts, then the write */
    line_read(&s->pd, d->pd, e);
    for (int i = 0; i < N; i++)
        m[i] = m[i] + e[i] * t->efb;
    bq_block(&d->lowcut, s->s_lc, m, N);
    bq_block(&d->hicut, s->s_hc, m, N);
    line_write(&s->pd, m);
    {
        const float k1 = 1.0f - t->echo;
        const float *src = t->flags & 2 ? e : m;
        for (int i = 0; i < N; i++) {
            xin[i] = src[i] * k1;
            e[i] = e[i] * t->echo;
            dd[i] = xin[i];
        }
    }
    /* the early reflections, from the undiffused input; the FDN's input diffused (Normal on) */
    if (d->quality != 0 && t->ap > 0.0f)
        diffuse(s->ap, d->apd, t->ap, xin);
    line_write(&s->df, dd);
    line_read(&s->df, d->df + N, dd);                  /* written first: frame i is at w - 16 + i */
    line_write(&s->er, dd);
    {                                                   /* the 16 taps where they lie, summed in registers */
        const float *tp[16];
        for (int j = 0; j < 8; j++) {
            tp[2 * j] = line_span(&s->er, d->erdl[j] + N, z[2 * j]);
            tp[2 * j + 1] = line_span(&s->er, d->erdr[j] + N, z[2 * j + 1]);
        }
        for (int i = 0; i < N; i++) {
            float l = 0.0f, r = 0.0f;
            for (int j = 0; j < 8; j++) {
                l = tp[2 * j][i] * t->ergl[j] + l;
                r = tp[2 * j + 1][i] * t->ergr[j] + r;
            }
            erl[i] = l;
            err[i] = r;
        }
        if (d->quality >= 2)                            /* the matrix wants them in z */
            for (int j = 0; j < 16; j++)
                if (tp[j] != z[j])
                    for (int i = 0; i < N; i++)
                        z[j][i] = tp[j][i];
    }
    if (d->quality != 0 && t->erap > 0.0f) {
        diffuse(s->erap, d->erapd, t->erap, erl);
        diffuse(s->erap + 4, d->erapd, t->erap, err);
    }
    /* the lines: read, damping, shelf, the sums */
    float sum[N], ol[N], orr[N];
    for (int k = 0; k < NL; k++) {
        float *yk = s->y[k], tmp[N];
        const float *src;
        if (k % d->step) {
            src = line_span(&s->fdn[k], d->delay[k], tmp);
        } else {
            line_lerp(&s->fdn[k], lfo[k & 3], (float)d->delay[k] - 16.0f, fs, tmp);
            src = tmp;
        }
        const float cg = d->damp_g[k], cp = d->damp_p[k], b0 = d->sh_b0[k], b1 = d->sh_b1[k], a1 = d->sh_a1[k];
        float sd = s->ds[k], ss = s->ss[k];
        for (int i = 0; i < N; i++) {
            sd = sd * cp + src[i] * cg;
            const float v = b0 * sd + ss;
            ss = a1 * v + b1 * sd;
            yk[i] = v;
        }
        s->ds[k] = sd;
        s->ss[k] = ss;
    }
    for (int i = 0; i < N; i++) {                       /* the sums, pair by pair in line order, in registers */
        float sm = 0.0f, l = 0.0f, r = 0.0f;
        for (int k = 0; k < NL; k += 2) {
            const float va = s->y[k][i], vb = s->y[k + 1][i];
            sm = vb + (va + sm);
            l = (l + va) - vb;
            r = (k & 2) ? (r - va) - vb : (r + va) + vb;
        }
        sum[i] = sm;
        ol[i] = l;
        orr[i] = r;
    }
    /* the feedback */
    {
        const float g = -2.0f / (float)NL * RT_DIFFUSION;
        for (int i = 0; i < N; i++)
            sum[i] = g * sum[i];
        float in[N];
        if (d->quality < 2) {
            for (int i = 0; i < N; i++)
                xin[i] = xin[i] + sum[i];
            for (int k = 0; k < NL; k++) {
                float *q = line_wspan(&s->fdn[k], in);
                for (int i = 0; i < N; i++)
                    q[i] = xin[i] + s->y[k][i];
                line_commit(&s->fdn[k], q);
            }
        } else {
            const float kx = 1.0f - 0.5f * t->mtx, km = 0.125f * t->mtx;
            for (int i = 0; i < N; i++)
                xin[i] = kx * xin[i] + sum[i];
            for (int st = 1; st < 16; st <<= 1)         /* the Hadamard transform of the raw taps, in place */
                for (int j = 0; j < 16; j++)
                    if (!(j & st))
                        for (int i = 0; i < N; i++) {
                            const float a = z[j][i], b = z[j + st][i];
                            z[j][i] = a + b;
                            z[j + st][i] = a - b;
                        }
            for (int k = 0; k < NL; k++) {
                float *q = line_wspan(&s->fdn[k], in);
                for (int i = 0; i < N; i++)
                    q[i] = (xin[i] + s->y[k][i]) + z[k][i] * km;
                line_commit(&s->fdn[k], q);
            }
        }
    }
    /* the outputs */
    {
        const float kn = RT_LATE / (float)NL, ke = t->erg * t->early;
        for (int i = 0; i < N; i++) {
            ol[i] *= kn;
            orr[i] *= kn;
        }
        op_block(&d->tone1, &s->s_t1[0], ol, N);
        op_block(&d->tone1, &s->s_t1[1], orr, N);
        op_block(&d->tone2, &s->s_t2[0], ol, N);
        op_block(&d->tone2, &s->s_t2[1], orr, N);
        for (int i = 0; i < N; i++) {
            ol[i] = ol[i] + erl[i] * ke;
            orr[i] = orr[i] + err[i] * ke;
        }
        if (d->eq1_on) {
            bq_block(&d->eq1, s->s_eq1[0], ol, N);
            bq_block(&d->eq1, s->s_eq1[1], orr, N);
        }
        if (d->eq2_on) {
            bq_block(&d->eq2, s->s_eq2[0], ol, N);
            bq_block(&d->eq2, s->s_eq2[1], orr, N);
        }
        const float w = t->width, a = w < 0.0f ? -w : w, g = 1.0f / (1.0f + a), gs = w * g;
        for (int i = 0; i < N; i++) {
            const float l = ol[i] + e[i], r = orr[i] + e[i];
            const float mm = (l + r) * g, sd = (l - r) * gs;
            x[2 * i] = mm + sd;
            x[2 * i + 1] = mm - sd;
        }
    }
}

/* ---------------------------------------------------------------- the block */

static int changed(struct state *s, const float *k)
{
    int c = !s->designed;
    for (int i = 0; i < NKNOBS; i++)
        if (k[i] != s->knobs[i]) {
            c = 1;
            s->knobs[i] = k[i];
        }
    s->designed = 1;
    return c;
}

static void set_lines(struct state *s, float *b)
{
    struct line *all[] = { &s->pd, &s->er, &s->df };
    const uint32_t len[] = { PD_LEN, ER_LEN, DF_LEN };
    for (int i = 0; i < 3; i++) {
        *all[i] = (struct line){ b, len[i], 0 };
        b += len[i];
    }
    for (int k = 0; k < NL; k++) {
        s->fdn[k] = (struct line){ b, FDN_LEN, 0 };
        b += FDN_LEN;
    }
    const uint32_t apl[4] = { AP_LEN(0), AP_LEN(1), AP_LEN(2), AP_LEN(3) };
    for (int k = 0; k < 4; k++) {
        s->ap[k] = (struct line){ b, apl[k], 0 };
        b += apl[k];
    }
    for (int k = 0; k < 8; k++) {
        s->erap[k] = (struct line){ b, ERAP_LEN, 0 };
        b += ERAP_LEN;
    }
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    float *b = buffer;
    for (uint32_t i = 0; i < BUF_FLOATS; i++)
        b[i] = 0.0f;
    set_lines(s, b);
    changed(s, knobs);
    design(s, knobs, MODFX_FS);
    s->gt = 1.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    if (changed(s, io->knobs))
        design(s, io->knobs, MODFX_FS);
    const struct design *d = &s->d;
    float *x = io->io;
    /* on: the mix; bypassed with Spillover: the tail rings over the input; without: the input alone */
    const float tw = io->enabled || d->spill ? d->gw : 0.0f, td = io->enabled ? d->gd : 0.0f, tt = io->enabled ? 0.0f : 1.0f;
    if (!io->enabled && s->idle && s->gd == 0.0f && s->gt == 1.0f)
        return;
    float wet[2 * WVFX_MAX_FRAMES];
    for (int i = 0; i < 2 * n; i++)
        wet[i] = x[i];
    for (int c0 = 0; c0 + N <= n; c0 += N)
        step(s, &wet[2 * c0], io->enabled);
    float peak = 0.0f;
    for (int i = 0; i < 2 * n; i++)
        peak = __builtin_fmaxf(peak, __builtin_fabsf(wet[i]));
    /* the tail decays on (heard with Spillover, muted without, so nothing stale comes back); once below -140 dB
       for 0.1 s with the input closed, nothing runs */
    if (io->enabled || peak > 1e-7f || s->in_g > 1e-6f) {
        s->quiet = 0;
        s->idle = 0;
    } else if (++s->quiet > 70) {
        s->idle = 1;
    }
    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n), dt = wvfx_ramp_step(s->gt, tt, n);
    float gw = s->gw, gd = s->gd, gt = s->gt;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        gt += dt;
        x[2 * i] = gw * wet[2 * i] + (gd + gt) * x[2 * i];
        x[2 * i + 1] = gw * wet[2 * i + 1] + (gd + gt) * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
    s->gt = tt;
}

const struct wvfx_desc reverb_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 1),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Reverb",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUF_FLOATS * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
