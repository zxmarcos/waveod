/*
 * Circuits in real time for waveod's blocks (docs/drives.md): the discrete-time "DK method" (David Yeh's K-method in
 * nodal form, Holters & Zoelzer). A circuit's linear part (resistors, capacitors as trapezoidal companions,
 * linearised transistors, op-amps, the pots) is folded into state-space matrices, built again when a pot moves;
 * on every sample only the nonlinear ports (transistor junctions, diodes, an op-amp's slew limit) are solved, by
 * Newton.
 *
 * A circuit stage is a dk_desc_t generated from its netlist by tools/circuits (operating point solved there,
 * everything here is the deviation from it). Node 0 is ground (the supply rails too, for the signal). Up to
 * DK_MAXIO inputs (voltage sources behind a conductance) and outputs (node voltages), so a pedal runs as a chain
 * of stages that exchange their interface voltages.
 *
 * For a block (sdk/wvfx.h: no mutable statics, the audio interrupt must not wait):
 *   - a stage's state (struct dk_stage) and its two sets of matrices live in the block's state;
 *   - the build's workspace (struct dk_work, doubles) in its buffer: the build runs in init and in poll (main
 *     loop), into the set the audio is not using (dk_build(stage, &stage->m[back], ...)); the block then flips
 *     one index for all its stages (dk_publish), and process reads the index once per block (dk_current).
 *
 * Same arithmetic as research/pulse/common/dk.h (the Dark Muff there): the blocks ported from it give the same
 * samples.
 */
#pragma once

#include <stdint.h>

#ifndef DK_MAXN
#define DK_MAXN 24                      /* nodes */
#endif
#define DK_MAXC 8                       /* capacitors = states */
#define DK_MAXP 4                       /* nonlinear ports */
#define DK_MAXIO 2                      /* inputs, outputs */
#define DK_W (DK_MAXIO + DK_MAXC + DK_MAXP)

typedef struct { uint8_t a, b; float g; } dk_g_t;          /* conductance a-b */
typedef struct { uint8_t r, c; float g; } dk_raw_t;        /* raw stamp G[r][c] (linearised transistors, op-amps) */
typedef struct { uint8_t a, b; float c; } dk_c_t;          /* capacitor a-b */
typedef struct { uint8_t a, w, b, knob; float r; } dk_pot_t;   /* pot a-w-b on knob `knob`, rotation 0 = wiper at a */
/* kind 0: a junction, i = is (exp(v / nvt) - exp(v0 / nvt)) at v = v0 + dv (Ebers-Moll's diodes, single diodes);
 * kind 1: two antiparallel diodes, i = is (exp(dv / nvt) - exp(-dv / nvt));
 * kind 3: a transconductance with a current limit (an op-amp's input stage: its slew rate), i = is tanh(dv / nvt).
 * dv = V(sp) - V(sn); the current leaves node n[k] with weight w[k] (into the device) */
typedef struct { uint8_t kind, sp, sn, n[3]; float w[3], is, nvt, v0; } dk_port_t;
typedef struct {
    const dk_g_t *g; uint8_t ng;
    const dk_raw_t *raw; uint8_t nraw;
    const dk_c_t *c; uint8_t nc;
    const dk_pot_t *pot; uint8_t npot;
    const dk_port_t *port; uint8_t np;
    const uint8_t *group; uint8_t ngroup;   /* group k = ports group[k] .. group[k + 1] - 1 (one group today) */
    uint8_t nn;                             /* nodes without ground */
    uint8_t nin, in[DK_MAXIO];              /* inputs: node, its source conductance */
    float gin[DK_MAXIO];
    uint8_t nout, out[DK_MAXIO];            /* outputs: node */
} dk_desc_t;

/* one build: the matrices for one setting of the pots */
typedef struct {
    float A[DK_MAXC][DK_MAXC], B[DK_MAXC][DK_MAXIO], C[DK_MAXC][DK_MAXP];
    float D[DK_MAXP][DK_MAXC], E[DK_MAXP][DK_MAXIO], K[DK_MAXP][DK_MAXP];
    float L[DK_MAXIO][DK_MAXC], M[DK_MAXIO][DK_MAXIO], N[DK_MAXIO][DK_MAXP];
    float *tab, tab_s;                      /* one diode-pair port: its current against q, tabulated (dk_tab_build) */
    int ntab;
} dk_mat_t;

typedef struct {
    const dk_desc_t *d;
    dk_mat_t m[2];
    float h[DK_MAXC], i[DK_MAXP], v[DK_MAXP], v1[DK_MAXP];
    float is[DK_MAXP], a[DK_MAXP], z0[DK_MAXP], ie0[DK_MAXP];   /* per port: is, 1 / nvt, v0 / nvt, is e^z0 */
    float xcrit[DK_MAXP], v0[DK_MAXP];  /* a junction's critical deviation and its voltage at rest (DK_PNJLIM) */
    float vt[DK_MAXP];                  /* 1 / a: nvt, for the step limits (once here, not per sample) */
} dk_stage;

/* the build's workspace (doubles: the nodal matrix spans 1e-8 .. 1e4 S) */
typedef struct { double G[DK_MAXN][DK_MAXN], Y[DK_MAXN][DK_W]; } dk_work;

static inline float dk_exp(float x)     /* e^x to 3e-6 for x in [-87, 59], branchless */
{
    float t = __builtin_fminf(__builtin_fmaxf(x * 1.44269504f, -125.0f), 86.0f);
    int i = (int)(t + 128.0f) - 128;    /* floor: the sum is positive */
    float f = t - (float)i;
    float p = 1.0f + f * (0.69304483f + f * (0.2412803f + f * (0.05224228f + f * 0.01342681f)));
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)(i + 127) << 23;
    return p * v.f;
}

static inline float dk_ln(float x)      /* ln x for x > 0 (error ~1e-6) */
{
    union { float f; uint32_t u; } v = { x };
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;
    float m = v.f, t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    return (float)e * 0.69314718f + 2.0f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * (0.14285714f + t2 * 0.11111111f))));
}

static inline uint32_t dk_bits(float x)
{
    union { float f; uint32_t u; } v = { x };
    return v.u;
}

static inline void dk_init(dk_stage *m, const dk_desc_t *d)
{
    m->d = d;
    m->m[0].tab = m->m[1].tab = 0;
    for (int k = 0; k < DK_MAXC; k++) m->h[k] = 0.0f;
    for (int j = 0; j < d->np; j++) {
        const dk_port_t *p = &d->port[j];
        m->i[j] = m->v[j] = m->v1[j] = 0.0f;
        m->is[j] = p->is;
        m->a[j] = 1.0f / p->nvt;
        m->vt[j] = 1.0f / m->a[j];
        m->z0[j] = p->v0 * m->a[j];
        m->ie0[j] = p->kind == 0 ? p->is * dk_exp(m->z0[j]) : 0.0f;
        /* SPICE's vcrit = nvt ln(nvt / (sqrt 2 is)), as a deviation from the operating point */
        m->xcrit[j] = p->nvt * dk_ln(p->nvt / (1.41421356f * p->is)) - p->v0;
        m->v0[j] = p->v0;
    }
}

static inline void dk_stamp(dk_work *w, int a, int b, double g)
{
    if (a) w->G[a - 1][a - 1] += g;
    if (b) w->G[b - 1][b - 1] += g;
    if (a && b) { w->G[a - 1][b - 1] -= g; w->G[b - 1][a - 1] -= g; }
}

/* the state-space matrices into `m` for the pots' rotations rot[] (0..1) at sample rate fs; the state (capacitor
 * histories) is kept, so a pot moves as on the real circuit */
static inline void dk_build(const dk_stage *st, dk_mat_t *m, dk_work *wk, const float *rot, float fs)
{
    const dk_desc_t *d = st->d;
    int n = d->nn, nc = d->nc, np = d->np, ni = d->nin, w = ni + nc + np;
    double (*G)[DK_MAXN] = wk->G, (*Y)[DK_W] = wk->Y;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) G[r][c] = 0.0;
        for (int c = 0; c < w; c++) Y[r][c] = 0.0;
    }
    for (int k = 0; k < d->ng; k++) dk_stamp(wk, d->g[k].a, d->g[k].b, d->g[k].g);
    for (int k = 0; k < d->nraw; k++) G[d->raw[k].r - 1][d->raw[k].c - 1] += d->raw[k].g;
    for (int k = 0; k < d->npot; k++) {
        float x0 = rot[d->pot[k].knob];
        double r = d->pot[k].r, x = x0 < 0.0f ? 0.0 : (x0 > 1.0f ? 1.0 : x0);
        double ra = r * x, rb = r * (1.0 - x);
        dk_stamp(wk, d->pot[k].a, d->pot[k].w, 1.0 / (ra > 1.0 ? ra : 1.0));
        dk_stamp(wk, d->pot[k].w, d->pot[k].b, 1.0 / (rb > 1.0 ? rb : 1.0));
    }
    for (int k = 0; k < nc; k++) dk_stamp(wk, d->c[k].a, d->c[k].b, 2.0 * d->c[k].c * fs);
    /* right-hand sides: each input, each capacitor's history (+ at a, - at b), each port's current (- n w) */
    for (int k = 0; k < ni; k++) {
        G[d->in[k] - 1][d->in[k] - 1] += d->gin[k];
        Y[d->in[k] - 1][k] = d->gin[k];
    }
    for (int k = 0; k < nc; k++) {
        if (d->c[k].a) Y[d->c[k].a - 1][ni + k] += 1.0;
        if (d->c[k].b) Y[d->c[k].b - 1][ni + k] -= 1.0;
    }
    for (int j = 0; j < np; j++)
        for (int t = 0; t < 3; t++)
            if (d->port[j].n[t]) Y[d->port[j].n[t] - 1][ni + nc + j] -= d->port[j].w[t];
    /* Gaussian elimination with partial pivoting on all right-hand sides at once */
    for (int k = 0; k < n; k++) {
        int p = k;
        for (int r = k + 1; r < n; r++) if ((G[r][k] < 0 ? -G[r][k] : G[r][k]) > (G[p][k] < 0 ? -G[p][k] : G[p][k])) p = r;
        if (p != k) {
            for (int c = 0; c < n; c++) { double t = G[k][c]; G[k][c] = G[p][c]; G[p][c] = t; }
            for (int c = 0; c < w; c++) { double t = Y[k][c]; Y[k][c] = Y[p][c]; Y[p][c] = t; }
        }
        double inv = 1.0 / G[k][k];
        for (int r = k + 1; r < n; r++) {
            double f = G[r][k] * inv;
            if (f == 0.0) continue;
            for (int c = k; c < n; c++) G[r][c] -= f * G[k][c];
            for (int c = 0; c < w; c++) Y[r][c] -= f * Y[k][c];
        }
    }
    for (int k = n - 1; k >= 0; k--) {
        double inv = 1.0 / G[k][k];
        for (int c = 0; c < w; c++) {
            double s = Y[k][c];
            for (int j = k + 1; j < n; j++) s -= G[k][j] * Y[j][c];
            Y[k][c] = s * inv;
        }
    }
    /* node voltage = Y [u, h, i]; project onto the capacitors, the ports and the outputs */
#define DK_V(node, col) ((node) ? Y[(node) - 1][col] : 0.0)
    for (int k = 0; k < nc; k++) {
        double g2 = 4.0 * d->c[k].c * fs;      /* h' = 2 Gc vc - h */
        int a = d->c[k].a, b = d->c[k].b;
        for (int c = 0; c < ni; c++) m->B[k][c] = (float)(g2 * (DK_V(a, c) - DK_V(b, c)));
        for (int c = 0; c < nc; c++) m->A[k][c] = (float)(g2 * (DK_V(a, ni + c) - DK_V(b, ni + c)) - (c == k));
        for (int j = 0; j < np; j++) m->C[k][j] = (float)(g2 * (DK_V(a, ni + nc + j) - DK_V(b, ni + nc + j)));
    }
    for (int q = 0; q < np; q++) {
        int a = d->port[q].sp, b = d->port[q].sn;
        for (int c = 0; c < ni; c++) m->E[q][c] = (float)(DK_V(a, c) - DK_V(b, c));
        for (int c = 0; c < nc; c++) m->D[q][c] = (float)(DK_V(a, ni + c) - DK_V(b, ni + c));
        for (int j = 0; j < np; j++) m->K[q][j] = (float)(DK_V(a, ni + nc + j) - DK_V(b, ni + nc + j));
    }
    for (int o = 0; o < d->nout; o++) {
        int a = d->out[o];
        for (int c = 0; c < ni; c++) m->M[o][c] = (float)DK_V(a, c);
        for (int c = 0; c < nc; c++) m->L[o][c] = (float)DK_V(a, ni + c);
        for (int j = 0; j < np; j++) m->N[o][j] = (float)DK_V(a, ni + nc + j);
    }
#undef DK_V
}

/* the set the audio reads, and the switch to a new one (after every stage of the block is built into it) */
static inline unsigned dk_current(const volatile uint32_t *cur)
{
    return __atomic_load_n(cur, __ATOMIC_ACQUIRE) & 1u;
}

static inline void dk_publish(volatile uint32_t *cur, unsigned set)
{
    __atomic_store_n(cur, set, __ATOMIC_RELEASE);
}

/* port current and its slope at deviation x; kind (a constant): 0 forward junction, 2 junction reverse-biased at
 * rest, 1 diode pair, 3 limited transconductance */
static inline __attribute__((always_inline)) float dk_port_i(const dk_stage *m, int j, float x, float *g, const unsigned kind)
{
    float a = m->a[j];
    if (kind == 1) {
        float e = dk_exp(x * a), f = 1.0f / e;
        *g = m->is[j] * a * (e + f);
        return m->is[j] * (e - f);
    }
    if (kind == 3) {                        /* is tanh(x a) = is (1 - 2 / (e^2xa + 1)) */
        float e = dk_exp(2.0f * x * a), r = 1.0f / (e + 1.0f), t = 1.0f - 2.0f * r;
        *g = m->is[j] * a * (1.0f - t * t);
        return m->is[j] * t;
    }
    float z = m->z0[j] + x * a;             /* the total junction voltage: a reverse-biased e^z0 underflows */
    if (kind == 2 && z < -20.0f) { *g = 0.0f; return -m->ie0[j]; }   /* off */
    float e = m->is[j] * dk_exp(z);
    *g = a * e;
    return e - m->ie0[j];
}

/* A stage whose only nonlinear port is a diode pair: x = q + K i(x) has one solution i(q), odd in q; tabulate it on
 * |q| = 0 .. qmax (n points, buf provided) into `m` after its dk_build, and dk_run reads it (linear interpolation;
 * beyond qmax, the last segment's slope) instead of solving by Newton on every sample. */
static inline void dk_tab_build(const dk_stage *st, dk_mat_t *m, float *buf, int n, float qmax)
{
    float K = m->K[0][0], x = 0.0f, g;
    m->tab = buf; m->ntab = n; m->tab_s = (float)(n - 1) / qmax;
    for (int k = 0; k < n; k++) {
        float q = qmax * (float)k / (float)(n - 1);
        for (int it = 0; it < 40; it++) {   /* Newton from the last point's solution */
            float i = dk_port_i(st, 0, x, &g, 1u);
            float dx = (x - q - K * i) / (1.0f - K * g);
            dx = __builtin_fminf(__builtin_fmaxf(dx, -0.1f), 0.1f);
            x -= dx;
            if (__builtin_fabsf(dx) < 1e-7f) break;
        }
        buf[k] = dk_port_i(st, 0, x, &g, 1u);
    }
}

static inline __attribute__((always_inline)) float dk_tab_read(const dk_mat_t *m, float q)
{
    float a = __builtin_fabsf(q) * m->tab_s;
    int k = (int)a;
    if (k > m->ntab - 2) k = m->ntab - 2;
    float f = a - (float)k;
    float i = m->tab[k] + f * (m->tab[k + 1] - m->tab[k]);
    return q < 0.0f ? -i : i;
}

#ifndef DK_TOL
#define DK_TOL 3e-4f                    /* volts: no change in the output against 1e-5 */
#endif
#ifndef DK_ITERS
#define DK_ITERS 12
#endif
#ifndef DK_UNROLL_FULL
#define DK_UNROLL_FULL 1               /* the per-sample loops unrolled in full: clang leaves the 3 and 4 port
                                           Newton loops rolled (-35 % instructions on the fuzz, the same samples) */
#endif
#if DK_UNROLL_FULL
#define DK_UNROLL _Pragma("clang loop unroll(full)")
#else
#define DK_UNROLL
#endif
#ifndef DK_PNJLIM
#define DK_PNJLIM 0                     /* 1: Newton starts from the last sample's solution and junction steps are
                                           limited as SPICE does (germanium, transistors that saturate and cut
                                           off: tools/check_drives.py); 0: an extrapolated start, every step
                                           clamped to 0.1 V (the research's Muff, its samples) */
#endif
#ifndef DK_EXTRAP
#define DK_EXTRAP 0                     /* with DK_PNJLIM: Newton starts extrapolated from the last two samples,
                                           except a junction that would start beyond vcrit */
#endif
#ifndef DK_PREDICT
#define DK_PREDICT 0                    /* 1: Newton also stops when the step it would take next, estimated from this
                                           one's to second order, is under DK_TOL (see dk_run_n) */
#endif

/* one Newton iteration of a stage's ports from x (q: the linear part of their voltages): x and the currents ic
 * updated. Returns 1 when another is needed: the step taken was not under DK_TOL; with DK_PREDICT, the next step
 * estimated was not, or a step was limited. */
static inline __attribute__((always_inline)) unsigned dk_iter(const dk_stage *restrict st, const dk_mat_t *restrict m,
                                                              float *restrict x, const float *restrict q,
                                                              float *restrict ic, const int np, const unsigned kinds)
{
    float r[DK_MAXP], J[DK_MAXP][DK_MAXP], dx[DK_MAXP], gg[DK_MAXP];
#if DK_PREDICT
    float iv[DK_MAXP], dd[DK_MAXP];     /* the elimination's pivots' inverses, the steps taken */
    int pv[DK_MAXP];                    /* its row swaps */
    unsigned lim = 0;                   /* a step limited: the quadratic estimate does not hold */
#endif
    DK_UNROLL for (int a = 0; a < np; a++) ic[a] = dk_port_i(st, a, x[a], &gg[a], (kinds >> (2 * a)) & 3u);
    DK_UNROLL for (int a = 0; a < np; a++) {
        float s = x[a] - q[a];
        DK_UNROLL for (int b = 0; b < np; b++) {
            s -= m->K[a][b] * ic[b];
            J[a][b] = (a == b) - m->K[a][b] * gg[b];
        }
        r[a] = s;
    }
    if (np == 1) {
        dx[0] = r[0] / J[0][0];
    } else if (np == 2) {               /* Cramer: one division */
        float inv = 1.0f / (J[0][0] * J[1][1] - J[0][1] * J[1][0]);
        dx[0] = (r[0] * J[1][1] - r[1] * J[0][1]) * inv;
        dx[1] = (r[1] * J[0][0] - r[0] * J[1][0]) * inv;
    } else {                            /* elimination with partial pivoting: J = I - K G is far from */
        DK_UNROLL for (int c = 0; c < np; c++) {  /* diagonally dominant when junctions conduct into high impedances */
            int p = c;
            DK_UNROLL for (int rr = c + 1; rr < np; rr++)
                if (__builtin_fabsf(J[rr][c]) > __builtin_fabsf(J[p][c])) p = rr;
            if (p != c) {
                DK_UNROLL for (int t = c; t < np; t++) { float tmp = J[c][t]; J[c][t] = J[p][t]; J[p][t] = tmp; }
                float tmp = r[c]; r[c] = r[p]; r[p] = tmp;
            }
            float inv = 1.0f / J[c][c];
#if DK_PREDICT
            pv[c] = p;
            iv[c] = inv;
#endif
            DK_UNROLL for (int rr = c + 1; rr < np; rr++) {
                float f = J[rr][c] * inv;
                DK_UNROLL for (int t = c + 1; t < np; t++) J[rr][t] -= f * J[c][t];
                r[rr] -= f * r[c];
#if DK_PREDICT
                J[rr][c] = f;           /* kept for the estimate's solve (column c is done with) */
#endif
            }
            r[c] *= inv;
            DK_UNROLL for (int t = c + 1; t < np; t++) J[c][t] *= inv;
        }
        DK_UNROLL for (int c = np - 1; c >= 0; c--) {
            DK_UNROLL for (int t = c + 1; t < np; t++) r[c] -= J[c][t] * dx[t];
            dx[c] = r[c];
        }
    }
#if !DK_PREDICT
    float step = 0.0f;
#endif
    DK_UNROLL for (int a = 0; a < np; a++) {
        float d = __builtin_fminf(__builtin_fmaxf(dx[a], -0.1f), 0.1f);   /* junctions: small steps */
#if DK_PREDICT
        unsigned la = dk_bits(d) != dk_bits(dx[a]);   /* clamped (integer compares: no FPU flags) */
#endif
#if DK_PNJLIM
        unsigned kd = (kinds >> (2 * a)) & 3u;
        if (kd == 0 || kd == 2) {       /* SPICE's pnjlim: a step into the forward region beyond vcrit */
            float vt = st->vt[a], xn = x[a] - dx[a];   /* grows as a log; other steps are free */
#if DK_PREDICT
            la = 0;
#endif
            if (xn > st->xcrit[a] && __builtin_fabsf(dx[a]) > 2.0f * vt) {
#if DK_PREDICT
                la = 1;
#endif
                float vold = st->v0[a] + x[a], vnew = st->v0[a] + xn;
                if (vold > 0.0f) {
                    float arg = 1.0f + (vnew - vold) / vt;
                    vnew = arg > 0.0f ? vold + vt * dk_ln(arg) : st->v0[a] + st->xcrit[a];
                } else {
                    vnew = vt * dk_ln(vnew / vt);
                }
                xn = vnew - st->v0[a];
            }
            d = x[a] - xn;
        }
#endif
        x[a] -= d;
        ic[a] -= gg[a] * d;             /* the current follows the last step to first order */
#if DK_PREDICT
        dd[a] = d;
        lim |= la;
#else
        step = __builtin_fmaxf(step, __builtin_fabsf(d));
#endif
    }
#if !DK_PREDICT
    return !(step < DK_TOL);
#else
    /* The next step, estimated without evaluating the ports again: to second order the current after the
     * step misses the linearised one by e = i'' d^2 / 2 (a g d^2 / 2 for a junction or a diode pair, at most
     * a g d^2 for the tanh; 0 for a junction that is off), so the residual left is K e and the next step
     * J^-1 K e, with this iteration's factors of J. A linear port's step, however big, leaves nothing.
     * Done when it is under DK_TOL and no step was limited (this replaces the test of the step taken: a
     * small step leaves a smaller estimate). The tests are integer compares of the floats' bits: on the M7
     * a compare into the flags waits for the FPU to drain. */
    {
        float e[DK_MAXP], b[DK_MAXP];
        uint32_t big;
        DK_UNROLL for (int a = 0; a < np; a++)
            e[a] = (((kinds >> (2 * a)) & 3u) == 3u ? 1.0f : 0.5f) * st->a[a] * gg[a] * (dd[a] * dd[a]);
        DK_UNROLL for (int a = 0; a < np; a++) {
            float s = 0.0f;
            DK_UNROLL for (int c = 0; c < np; c++) s += m->K[a][c] * e[c];
            b[a] = s;
        }
        if (np == 1) {                  /* |b| >= TOL |J|: the sign of the difference */
            big = ~dk_bits(__builtin_fabsf(b[0]) - DK_TOL * __builtin_fabsf(J[0][0])) >> 31;
        } else if (np == 2) {
            float inv = 1.0f / (J[0][0] * J[1][1] - J[0][1] * J[1][0]);
            float m2 = __builtin_fmaxf(__builtin_fabsf((b[0] * J[1][1] - b[1] * J[0][1]) * inv),
                                       __builtin_fabsf((b[1] * J[0][0] - b[0] * J[1][0]) * inv));
            big = dk_bits(m2) >= dk_bits(DK_TOL);   /* non-negative floats order as their bits; NaN big */
        } else {                        /* the elimination replayed on b */
            DK_UNROLL for (int c = 0; c < np; c++) {
                DK_UNROLL for (int t = 0; t < np; t++)   /* b[c] <-> b[pv[c]], unrolled */
                    if (t > c && t == pv[c]) { float tmp = b[c]; b[c] = b[t]; b[t] = tmp; }
                DK_UNROLL for (int rr = c + 1; rr < np; rr++) b[rr] -= J[rr][c] * b[c];
                b[c] *= iv[c];
            }
            float m2 = 0.0f;
            DK_UNROLL for (int c = np - 1; c >= 0; c--) {
                DK_UNROLL for (int t = c + 1; t < np; t++) b[c] -= J[c][t] * b[t];
                m2 = __builtin_fmaxf(m2, __builtin_fabsf(b[c]));
            }
            big = dk_bits(m2) >= dk_bits(DK_TOL);
        }
        return big | lim;
    }
#endif
}

/* a stage's outputs y and next state hn from its inputs u, state h and port currents ic */
static inline __attribute__((always_inline)) void dk_out(const dk_mat_t *restrict m, const float *u, const float *h,
                                                         const float *ic, float *y, float *hn, const int ni,
                                                         const int nc, const int np, const int no)
{
    DK_UNROLL for (int o = 0; o < no; o++) {
        float s = 0.0f;
        DK_UNROLL for (int c = 0; c < ni; c++) s += m->M[o][c] * u[c];
        DK_UNROLL for (int c = 0; c < nc; c++) s += m->L[o][c] * h[c];
        DK_UNROLL for (int j = 0; j < np; j++) s += m->N[o][j] * ic[j];
        y[o] = s;
    }
    DK_UNROLL for (int r = 0; r < nc; r++) {
        float s = 0.0f;
        DK_UNROLL for (int c = 0; c < ni; c++) s += m->B[r][c] * u[c];
        DK_UNROLL for (int c = 0; c < nc; c++) s += m->A[r][c] * h[c];
        DK_UNROLL for (int j = 0; j < np; j++) s += m->C[r][j] * ic[j];
        hn[r] = s;
    }
}

/* one sample through stage st with the matrices m: inputs u[] (volts) -> outputs y[] (volts, deviations from the
 * operating point). The sizes are constants at each call site (DK_RUN below), so the loops unroll. */
static inline __attribute__((always_inline)) void dk_run_n(dk_stage *restrict st, const dk_mat_t *restrict m,
                                                           const float *restrict u0, float *restrict y,
                                                           const int ni, const int nc, const int np, const int no,
                                                           const unsigned kinds)
{
    float u[DK_MAXIO], h[DK_MAXC], ic[DK_MAXP + 1];
    DK_UNROLL for (int c = 0; c < ni; c++) u[c] = u0[c];
    DK_UNROLL for (int c = 0; c < nc; c++) h[c] = st->h[c];
    if (np == 1 && m->tab) {                /* a tabulated diode pair */
        float q = 0.0f;
        DK_UNROLL for (int c = 0; c < ni; c++) q += m->E[0][c] * u[c];
        DK_UNROLL for (int c = 0; c < nc; c++) q += m->D[0][c] * h[c];
        ic[0] = st->i[0] = dk_tab_read(m, q);
    } else if (np) {                        /* one Newton group: all the ports */
        float x[DK_MAXP], q[DK_MAXP];
        DK_UNROLL for (int a = 0; a < np; a++) {
            float s = 0.0f;
            DK_UNROLL for (int c = 0; c < ni; c++) s += m->E[a][c] * u[c];
            DK_UNROLL for (int c = 0; c < nc; c++) s += m->D[a][c] * h[c];
            q[a] = s;
#if DK_PNJLIM && DK_EXTRAP
            {                                   /* the start extrapolated, but a junction it would take beyond */
                float xe = 2.0f * st->v[a] - st->v1[a];   /* vcrit starts from the last solution */
                const unsigned kd = (kinds >> (2 * a)) & 3u;
                if ((kd == 0 || kd == 2) && xe > st->xcrit[a] && xe > st->v[a])
                    xe = __builtin_fmaxf(st->v[a], st->xcrit[a]);
                x[a] = xe;
            }
#elif DK_PNJLIM
            x[a] = st->v[a];                    /* the start: the last sample's solution (an extrapolated start */
#else                                           /* can land a junction far forward, where Newton gets lost) */
            x[a] = 2.0f * st->v[a] - st->v1[a];   /* the start: extrapolated from the last two samples */
#endif
        }
#if DK_PREDICT
        /* the first iteration, then the outputs and the next state from it in the same stretch of code as the
         * test for another: the core overlaps the two (the test is a chain of dependent operations); when another
         * is needed (rarely), they are computed again */
        float yn[DK_MAXIO], hn[DK_MAXC];
        unsigned more = dk_iter(st, m, x, q, ic, np, kinds);
        dk_out(m, u, h, ic, yn, hn, ni, nc, np, no);
        if (__builtin_expect(more, 0)) {
            int it = 1;
            for (; it < DK_ITERS && more; it++)
                more = dk_iter(st, m, x, q, ic, np, kinds);
#ifdef DK_STAT
            if (!more) DK_STAT(it);
#endif
            dk_out(m, u, h, ic, yn, hn, ni, nc, np, no);
        } else {
#ifdef DK_STAT
            DK_STAT(1);
#endif
        }
        DK_UNROLL for (int a = 0; a < np; a++) {
            st->v1[a] = st->v[a];
            st->v[a] = x[a];
            st->i[a] = ic[a];
        }
        DK_UNROLL for (int o = 0; o < no; o++) y[o] = yn[o];
        DK_UNROLL for (int r = 0; r < nc; r++) st->h[r] = hn[r];
        return;
#else
        for (int it = 0; it < DK_ITERS; it++) {
            if (!dk_iter(st, m, x, q, ic, np, kinds)) {
#ifdef DK_STAT
                DK_STAT(it + 1);                /* a diagnostic harness's count of the iterations */
#endif
                break;
            }
        }
#endif
        DK_UNROLL for (int a = 0; a < np; a++) {
            st->v1[a] = st->v[a];
            st->v[a] = x[a];
            st->i[a] = ic[a];
        }
    }
    dk_out(m, u, h, ic, y, st->h, ni, nc, np, no);
}

/* DK_RUN(name, P): a run function for the generated circuit P, its sizes P_NI, P_NC, P_NP, P_NO constant */
#define DK_RUN(name, P) DK_RUN_(name, P)
#define DK_RUN_(name, P) \
    static inline void name(dk_stage *st, const dk_mat_t *m, const float *u, float *y) \
    { dk_run_n(st, m, u, y, P##_NI, P##_NC, P##_NP, P##_NO, P##_KINDS); }
