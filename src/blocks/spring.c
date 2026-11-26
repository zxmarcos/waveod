/*
 * Spring (RVB › Spring, waveod's): a light spring reverb, 10 types, docs/plate.md.
 *
 * Per 16 frames, mono:
 *   the front (src/blocks/modfx/rvb.h): input fade, hold, predelay with echo, Low Cut -> the reverb's input X ->
 *   two peaking EQs (when they boost or cut) -> the drive u = clamp(10 d X + 1, 0, 4), (u^2 - u^3 / 6 - 5/6) / 15 d
 *   -> (pair tanks: the tone filter) -> a 4x decimator; at a quarter of the rate, 4 frames:
 *   per line: read at its delay -> a chain of first-order all-passes of coefficient -0.5 (the spring's
 *   dispersion) -> the damping one-pole and the low shelf;
 *   four-line springs (lines a b c d, outputs A B C D): out += C, c <- (1 - k) A + k D, b <- (1 - k) D + k A, d <- C,
 *   a <- allpass(B) + X, k = Diffusion / 20; pair tanks (a b): out += A, b <- A, a <- X + allpass(B); the all-pass
 *   is a Schroeder one (0.707) on a line of its own, mixed by the type's amount;
 *   out x 0.5 / sqrt(springs) -> a 4x interpolator -> (four-line springs: the tone filter) -> High Cut -> L = R ->
 *   the mix law, the level.
 *
 *   Type (10)  Time 0.1..100 s  Predelay 0..1000 ms  Mix %  Level dB  High Cut  Low Cut  Spillover
 */
#include "modfx/rvb.h"

#define N  RVB_N
#define NL 12                            /* lines: 3 springs of 4, or 6 pairs */
#define Q  (N / 4)                       /* frames per step at a quarter of the rate */

struct st_type { RVB_TYPE_FIELDS };
#include "spring_types.h"
#define NTYPES ((int)(sizeof st_types / sizeof st_types[0]))

enum { K_TYPE, K_TIME, K_PREDELAY, K_MIX, K_LEVEL, K_HICUT, K_LOWCUT, K_SPILL, NKNOBS };

static const struct wvfx_param params[] = {
    { "Type", 0.0f, 9.0f, 1.0f, 0.0f, K_TYPE, 0, 0, 0, ST_NAMES, 0 },
    { "Time", 0.1f, 100.0f, 0.01f, 2.0f, K_TIME, 0, 0, 0, 0, "%.2f s" },
    { "Predelay", 0.0f, 1000.0f, 1.0f, 0.0f, K_PREDELAY, 0, 0, 0, 0, "%.0f ms" },
    { "Mix", 0.0f, 100.0f, 1.0f, 30.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 3000.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Low Cut", 20.0f, 2000.0f, 1.0f, 400.0f, K_LOWCUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Spillover", 0.0f, 1.0f, 1.0f, 1.0f, K_SPILL, 0, 0, 0, "Off\0On\0", 0 },
};

#define LEN16(n)   ((((uint32_t)(n)) + 15u) & ~15u)
#define SCALED(d)  ((uint32_t)((float)(d) * (44100.0f / 48000.0f)) + 1u)
#define PD_LEN     LEN16(44100u + 64u)
#define LINE_LEN   LEN16(SCALED(ST_MAX_LINE) + 16u)
#define SAP_LEN    208u                  /* the all-pass lines (delays < 204) */
#define AP_LEN     ((uint32_t)ST_MAX_NST)
#define BUF_FLOATS (PD_LEN + NL * LINE_LEN + 6u * SAP_LEN + NL * AP_LEN)

struct design {
    const struct st_type *t;
    int nst, tanks, pairs, eq1_on, eq2_on, drive_on, spill;
    uint32_t delay[NL], apd;
    struct rvb_fdes f;
    struct rvb_damp damp[NL];
    struct bq eq1, eq2, tone, hicut;
    float kd, kc, k10, kdrv, gout, gw, gd;
};

struct state {
    struct rline pd, line[NL], sap[6];
    float *aps;
    float knobs[NKNOBS];
    int designed, idle, quiet;
    struct design d;
    struct rvb_front fr;
    float sd[NL][2];
    float dn[128 - 4 + N], up[31 + Q];
    float s_eq1[2], s_eq2[2], s_tone[2], s_hc[2];
    float gw, gd, gt;
};

/* ---------------------------------------------------------------- design */

static void design(struct state *s, const float *k, float fs)
{
    struct design *d = &s->d;
    const struct st_type *t = &st_types[modfx_option(k[K_TYPE], NTYPES)];
    const float fe = 0.25f * fs;
    d->t = t;
    d->nst = t->nst;
    d->pairs = t->pairs;
    d->tanks = t->pairs ? t->n : t->n / 2;
    d->spill = k[K_SPILL] >= 0.5f;
    rvb_front_design(&d->f, k, K_PREDELAY, K_LOWCUT, K_HICUT, t->flags, t->efb, t->echo, t->cutq, t->lcq, ST_IN_R, 0, fs);
    d->hicut = d->f.hicut;
    const float time = wvfx_clamp(k[K_TIME], 0.1f, 100.0f);
    for (int i = 0; i < NL; i++) {
        d->delay[i] = rvb_scaled(t->delay[i]);
        rvb_damp_design(&d->damp[i], d->delay[i], time, t->r13, t->p37, t->p38, fe, fs);
    }
    d->apd = (uint32_t)((204.0f * t->apd) * (fs / 48000.0f));
    d->eq1_on = t->eq1g != 0.0f;
    d->eq2_on = t->eq2g != 0.0f;
    if (d->eq1_on)
        d->eq1 = rvb_peaking(t->eq1f, t->eq1q, t->eq1g, fs);
    if (d->eq2_on)
        d->eq2 = rvb_peaking(t->eq2f, t->eq2q, t->eq2g, fs);
    d->drive_on = t->drive > 0.00999999978f;
    d->k10 = 10.0f * t->drive;
    d->kdrv = 1.0f / (1.5f * d->k10);
    d->tone = rvb_from_analog(t->post, fs);
    d->kc = 0.05f * t->diff;
    d->kd = 1.0f - d->kc;
    {
        static const float sq[7] = { 1.0f, 1.0f, 1.41421354f, 1.73205078f, 2.0f, 2.23606801f, 2.44948959f };
        d->gout = 0.5f / sq[d->tanks > 0 && d->tanks < 7 ? d->tanks : 1];
    }
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = (mix >= 0.5f ? 1.0f : modfx_wet_curve(2.0f * mix)) * level;
    d->gd = (mix <= 0.5f ? 1.0f : 2.0f - 2.0f * mix) * level;
}

/* ---------------------------------------------------------------- one step */

/* the Schroeder all-pass of a tank on its own line (read d behind, then the write), mixed by amt */
static void allpass(struct rline *l, uint32_t dl, float amt, float *x)
{
    float t[Q], w[Q];
    rl_read(l, dl, t, Q);
    for (int i = 0; i < Q; i++)
        w[i] = t[i] * -0.707000017f + x[i];
    rl_write(l, w, Q);
    const float k = 1.0f - amt;
    for (int i = 0; i < Q; i++)
        x[i] = (w[i] * 0.707000017f + t[i]) * amt + x[i] * k;
}

static void step(struct state *s, float *x, int on)
{
    const struct design *d = &s->d;
    const struct st_type *t = d->t;
    float xin[N], e[N], xd[Q], o[Q] = { 0.0f }, ou[N], tmp[Q];

    rvb_front(&s->fr, &s->pd, &d->f, x, on, xin, e);
    if (d->eq1_on)
        bq_block(&d->eq1, s->s_eq1, xin, N);
    if (d->eq2_on)
        bq_block(&d->eq2, s->s_eq2, xin, N);
    if (d->drive_on) {
        for (int i = 0; i < N; i++) {
            float u = d->k10 * xin[i] + 1.0f;
            u = __builtin_fminf(__builtin_fmaxf(u, 0.0f), 4.0f);
            const float u2 = u * u;
            xin[i] = ((u2 - (u * u2) * 0.166666672f) - 0.833333313f) * d->kdrv;
        }
    }
    if (d->pairs)
        bq_block(&d->tone, s->s_tone, xin, N);
    rvb_down(s->dn, 128, 4, rvb_dn4, xin, xd);

    /* every line in (read, dispersion four lines at a time, damping): a tank writes only its own lines, after it
     * read them, so all the reads can come first */
    float y[NL][Q];
    const int nl = d->pairs ? 2 * d->tanks : 4 * d->tanks;
    for (int k = 0; k < nl; k++)
        rl_read(&s->line[k], d->delay[k], y[k], Q);
    int k4 = 0;
    for (; k4 + 4 <= nl; k4 += 4) {
        float *a = s->aps + k4 * AP_LEN;
        rvb_disperse4x4(a, y[k4], a + AP_LEN, y[k4 + 1], a + 2 * AP_LEN, y[k4 + 2], a + 3 * AP_LEN, y[k4 + 3], d->nst,
                        -0.5f);
    }
    for (; k4 < nl; k4++)
        rvb_disperse4(s->aps + k4 * AP_LEN, y[k4], d->nst, -0.5f);
    for (int k = 0; k < nl; k++)
        rvb_damp_run(&d->damp[k], s->sd[k], y[k], Q);
    if (d->pairs) {
        for (int j = 0; j < d->tanks; j++) {
            float *ya = y[2 * j], *yb = y[2 * j + 1];
            rl_write(&s->line[2 * j + 1], ya, Q);
            allpass(&s->sap[j], d->apd, t->ap, yb);
            for (int i = 0; i < Q; i++) {
                tmp[i] = xd[i] + yb[i];
                o[i] = o[i] + ya[i];
            }
            rl_write(&s->line[2 * j], tmp, Q);
        }
    } else {
        const float kc = d->kc, kd = d->kd;
        for (int sp = 0; sp < d->tanks; sp++) {
            const int b = 4 * sp;
            float *ya = y[b], *yb = y[b + 1], *yc = y[b + 2], *yy = y[b + 3];
            for (int i = 0; i < Q; i++) {
                o[i] = o[i] + yc[i];
                tmp[i] = ya[i] * kd + yy[i] * kc;
            }
            rl_write(&s->line[b + 2], tmp, Q);
            for (int i = 0; i < Q; i++)
                tmp[i] = yy[i] * kd + ya[i] * kc;
            rl_write(&s->line[b + 1], tmp, Q);
            rl_write(&s->line[b + 3], yc, Q);
            allpass(&s->sap[sp], d->apd, t->ap, yb);
            for (int i = 0; i < Q; i++)
                tmp[i] = yb[i] + xd[i];
            rl_write(&s->line[b], tmp, Q);
        }
    }
    for (int i = 0; i < Q; i++)
        o[i] *= d->gout;
    rvb_up(s->up, 4, rvb_up4, o, ou);
    if (!d->pairs)
        bq_block(&d->tone, s->s_tone, ou, N);
    bq_block(&d->hicut, s->s_hc, ou, N);
    for (int i = 0; i < N; i++)
        x[2 * i] = x[2 * i + 1] = ou[i];
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
        s->line[k] = (struct rline){ b, LINE_LEN, 0 };
        b += LINE_LEN;
    }
    for (int k = 0; k < 6; k++) {
        s->sap[k] = (struct rline){ b, SAP_LEN, 0 };
        b += SAP_LEN;
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

const struct wvfx_desc spring_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_RVB, 3),
    .menu = WVFX_RVB,
    .nparams = sizeof params / sizeof params[0],
    .name = "Spring",
    .category = "Reverb",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUF_FLOATS * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
