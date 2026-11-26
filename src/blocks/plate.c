/*
 * Plate (RVB › Plate, waveod's): a light plate reverb, 8 types, docs/plate.md.
 *
 * Per 16 frames:
 *   the front (src/blocks/modfx/rvb.h): input fade, hold, predelay with echo, Low / High Cut -> the reverb's input
 *   X -> a 2x decimator; at half the rate, 8 frames:
 *   16 lines, every Step-th (Economy 4, Normal / High 2, Ultra 1) read linearly at a position moved by one of the
 *   LFOs L, R, -L, -R (0.025 s x half the rate x the type's depth), the others at their delay -> per line a chain of
 *   first-order all-passes of coefficient 0.5 (the dispersion of a plate) -> the damping one-pole and the low shelf;
 *   the lines also go into 16 short output lines; the lines' inputs (X + F) + line, F = -2/16 x Diffusion x the
 *   sum of the lines (Householder);
 *   L = the lines with the odd ones negated, R = the output lines at D x Tap / 4 + 8 with the odd ones negated,
 *   both / 16 -> two tone one-poles -> a 2x interpolator -> two peaking EQs (when they boost or cut) -> + Echo ->
 *   Spread -> the mix law, the level.
 * Design: as the Reverb's (docs/reverb.md), the decay gains at half the rate, the shelves at the full rate.
 *
 *   Type (8)  Time 0.1..100 s  Predelay 0..1000 ms  Mix %  Level dB  High Cut  Low Cut  Quality  Spillover
 */
#include "modfx/rvb.h"

#define N  RVB_N
#define NL 16
#define H  (N / 2)                       /* frames per step at half the rate */

struct pt_type { RVB_TYPE_FIELDS };
#include "plate_types.h"
#define NTYPES ((int)(sizeof pt_types / sizeof pt_types[0]))

enum { K_TYPE, K_TIME, K_PREDELAY, K_MIX, K_LEVEL, K_HICUT, K_LOWCUT, K_QUALITY, K_SPILL, NKNOBS };

static const struct wvfx_param params[] = {
    { "Type", 0.0f, 7.0f, 1.0f, 0.0f, K_TYPE, 0, 0, 0, PT_NAMES, 0 },
    { "Time", 0.1f, 100.0f, 0.01f, 1.0f, K_TIME, 0, 0, 0, 0, "%.2f s" },
    { "Predelay", 0.0f, 1000.0f, 1.0f, 0.0f, K_PREDELAY, 0, 0, 0, 0, "%.0f ms" },
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 5200.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Low Cut", 20.0f, 2000.0f, 1.0f, 20.0f, K_LOWCUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Quality", 0.0f, 3.0f, 1.0f, 0.0f, K_QUALITY, 0, 0, 0, "Economy\0Normal\0High\0Ultra\0", 0 },
    { "Spillover", 0.0f, 1.0f, 1.0f, 1.0f, K_SPILL, 0, 0, 0, "Off\0On\0", 0 },
};

/* the lines at 22.05 kHz (the geometry x 0.91875) */
#define LEN16(n)   ((((uint32_t)(n)) + 15u) & ~15u)
#define SCALED(d)  ((uint32_t)((float)(d) * (44100.0f / 48000.0f)) + 1u)
#define PD_LEN     LEN16(44100u + 64u)
#define FDN_LEN    LEN16(SCALED(PT_MAX_LINE) + 16u + 16u)        /* + the LFO's swing (< 6) */
#define OUT_LEN    LEN16(SCALED(PT_MAX_LINE) * 0.1375f + 8u + 16u)
#define AP_LEN     ((uint32_t)PT_MAX_NST)
#define BUF_FLOATS (PD_LEN + NL * FDN_LEN + NL * OUT_LEN + NL * AP_LEN)

struct design {
    const struct pt_type *t;
    int step, nst, eq1_on, eq2_on, spill;
    uint32_t delay[NL], tap[NL];
    struct rvb_fdes f;
    struct rvb_damp damp[NL];
    struct op tone1, tone2;
    struct bq eq1, eq2;
    float lfo_inc, depth, kf, gw, gd;
};

struct state {
    struct rline pd, fdn[NL], out[NL];
    float *aps;
    float knobs[NKNOBS];
    int designed, idle, quiet;
    struct design d;
    struct rvb_front fr;
    float ph;
    float sd[NL][2];                     /* damping and shelf states */
    float dn[64 - 2 + N], up[2][31 + H]; /* the resamplers' histories */
    float s_t1[2], s_t2[2], s_eq1[2][2], s_eq2[2][2];
    float gw, gd, gt;
};

/* ---------------------------------------------------------------- design */

static void design(struct state *s, const float *k, float fs)
{
    struct design *d = &s->d;
    const struct pt_type *t = &pt_types[modfx_option(k[K_TYPE], NTYPES)];
    const float fe = 0.5f * fs;
    d->t = t;
    const int q = modfx_option(k[K_QUALITY], 4);
    d->step = q == 0 ? 4 : q == 3 ? 1 : 2;
    d->nst = t->nst;
    d->spill = k[K_SPILL] >= 0.5f;
    rvb_front_design(&d->f, k, K_PREDELAY, K_LOWCUT, K_HICUT, t->flags, t->efb, t->echo, t->cutq, t->lcq, PT_IN_R, 1, fs);
    const float time = wvfx_clamp(k[K_TIME], 0.1f, 100.0f), tq = 0.25f * t->tap;
    float rsum = 0.0f;
    for (int i = 0; i < NL; i++) {
        d->delay[i] = rvb_scaled(t->delay[i]);
        d->tap[i] = (uint32_t)((float)d->delay[i] * tq) + 8u;
        rsum += rvb_damp_design(&d->damp[i], d->delay[i], time, t->r13, t->p37, t->p38, fe, fs);
    }
    {
        const float x = modfx_pow(rsum * (1.0f / NL), -0.25f);
        rvb_fo_shelf(t->p38 * x, t->p38 / x, fs, &d->tone2.a, &d->tone2.b, &d->tone2.c);
        const float g = modfx_pow(t->r13, 0.5f), c1 = 2.0f * g / (1.0f + g);
        d->tone1 = (struct op){ 1.0f / c1, (c1 - 1.0f) / c1, -0.0f };
    }
    d->eq1_on = t->eq1g != 0.0f;
    d->eq2_on = t->eq2g != 0.0f;
    if (d->eq1_on)
        d->eq1 = rvb_peaking(t->eq1f, t->eq1q, t->eq1g, fs);
    if (d->eq2_on)
        d->eq2 = rvb_peaking(t->eq2f, t->eq2q, t->eq2g, fs);
    d->lfo_inc = t->lrate * MODFX_TWO_PI / fs;
    d->depth = (0.025f * fs) * 0.5f;
    d->kf = -2.0f / (float)NL * t->diff;
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = (mix >= 0.5f ? 1.0f : modfx_wet_curve(2.0f * mix)) * level;
    d->gd = (mix <= 0.5f ? 1.0f : 2.0f - 2.0f * mix) * level;
}

/* ---------------------------------------------------------------- one step */

/* a modulated line: (1 - fr) x[w - ip + i] + fr x[w - ip - 1 + i], pos = ip + fr = lfo x depth + D (>= 8) */
static void line_mod(const struct rline *l, const float *lfo, float depth, float dk, float *y)
{
    const float *b = l->buf;
    const int32_t size = (int32_t)l->size;
    for (int i = 0; i < H; i++) {
        float pos = (lfo[i] * depth) + dk;
        if (!(pos >= 8.0f))
            pos = 8.0f;
        const int ip = (int)pos;
        const float fr = pos - (float)ip;
        int32_t r = (int32_t)l->w - ip - 1 + i;
        if (r < 0)
            r += size;
        const int32_t r1 = r + 1 < size ? r + 1 : 0;
        y[i] = (1.0f - fr) * b[r1] + fr * b[r];
    }
}

static void step(struct state *s, float *x, int on)
{
    const struct design *d = &s->d;
    const struct pt_type *t = d->t;
    float lfo[4][N], xin[N], e[N], xd[H], y[NL][H], l[N], r[N];

    s->ph = modfx_rotate(s->ph, d->lfo_inc, N, lfo[0], lfo[1]);
    for (int i = 0; i < H; i++) {
        lfo[0][i] *= t->lamp;
        lfo[1][i] *= t->lamp;
        lfo[2][i] = -lfo[0][i];
        lfo[3][i] = -lfo[1][i];
    }
    rvb_front(&s->fr, &s->pd, &d->f, x, on, xin, e);
    rvb_down(s->dn, 64, 2, rvb_dn2, xin, xd);

    /* the lines: read, dispersion, damping and shelf; into the output lines */
    for (int k = 0; k < NL; k++) {
        if (k % d->step)
            rl_read(&s->fdn[k], d->delay[k], y[k], H);
        else
            line_mod(&s->fdn[k], lfo[k & 3], d->depth, (float)d->delay[k], y[k]);
    }
    for (int k = 0; k < NL; k += 4) {          /* four lines at a time (the lines are independent), 4 frames a run */
        float *a = s->aps + k * AP_LEN;
        for (int i = 0; i < H; i += 4)
            rvb_disperse4x4(a, y[k] + i, a + AP_LEN, y[k + 1] + i, a + 2 * AP_LEN, y[k + 2] + i, a + 3 * AP_LEN,
                            y[k + 3] + i, d->nst, 0.5f);
    }
    for (int k = 0; k < NL; k++) {
        rvb_damp_run(&d->damp[k], s->sd[k], y[k], H);
        rl_write(&s->out[k], y[k], H);
    }
    /* the feedback, and L */
    float hl[H], hr[H];
    for (int i = 0; i < H; i++) {
        float sm = 0.0f, lv = 0.0f;
        for (int k = 0; k < NL; k += 2) {
            sm = (sm + y[k][i]) + y[k + 1][i];
            lv = (lv + y[k][i]) - y[k + 1][i];
        }
        xd[i] = xd[i] + d->kf * sm;
        hl[i] = lv * (1.0f / NL);
    }
    for (int k = 0; k < NL; k++) {
        float q[H];
        for (int i = 0; i < H; i++)
            q[i] = xd[i] + y[k][i];
        rl_write(&s->fdn[k], q, H);
    }
    /* R, from the output lines */
    for (int i = 0; i < H; i++)
        hr[i] = 0.0f;
    for (int k = 0; k < NL; k += 2) {
        float za[H], zb[H];
        const float *pa = rl_span(&s->out[k], d->tap[k], za, H), *pb = rl_span(&s->out[k + 1], d->tap[k + 1], zb, H);
        for (int i = 0; i < H; i++)
            hr[i] = (hr[i] + pa[i]) - pb[i];
    }
    for (int i = 0; i < H; i++)
        hr[i] *= 1.0f / NL;
    op_block(&d->tone1, &s->s_t1[0], hl, H);
    op_block(&d->tone1, &s->s_t1[1], hr, H);
    op_block(&d->tone2, &s->s_t2[0], hl, H);
    op_block(&d->tone2, &s->s_t2[1], hr, H);
    rvb_up(s->up[0], 2, rvb_up2, hl, l);
    rvb_up(s->up[1], 2, rvb_up2, hr, r);
    if (d->eq1_on) {
        bq_block(&d->eq1, s->s_eq1[0], l, N);
        bq_block(&d->eq1, s->s_eq1[1], r, N);
    }
    if (d->eq2_on) {
        bq_block(&d->eq2, s->s_eq2[0], l, N);
        bq_block(&d->eq2, s->s_eq2[1], r, N);
    }
    const float w = t->width, a = w < 0.0f ? -w : w, g = 1.0f / (1.0f + a), gs = w * g;
    for (int i = 0; i < N; i++) {
        const float lv = l[i] + e[i], rv = r[i] + e[i];
        const float mm = (lv + rv) * g, sd = (lv - rv) * gs;
        x[2 * i] = mm + sd;
        x[2 * i + 1] = mm - sd;
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

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    float *b = buffer;
    for (uint32_t i = 0; i < BUF_FLOATS; i++)
        b[i] = 0.0f;
    s->pd = (struct rline){ b, PD_LEN, 0 };
    b += PD_LEN;
    for (int k = 0; k < NL; k++) {
        s->fdn[k] = (struct rline){ b, FDN_LEN, 0 };
        b += FDN_LEN;
        s->out[k] = (struct rline){ b, OUT_LEN, 0 };
        b += OUT_LEN;
    }
    s->aps = b;
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
    if (io->enabled || peak > 1e-7f || s->fr.in_g > 1e-6f) {
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

const struct wvfx_desc plate_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 2),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Plate",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUF_FLOATS * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
