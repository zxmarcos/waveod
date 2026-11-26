/* Transient engine for tools/circuits/circuit.py: modified nodal analysis, trapezoidal capacitors, full Newton on
 * every sample (Ebers-Moll NPNs, exponential diodes, limited transconductances), double precision. Reference only:
 * slow and exact.
 *
 *   refsim netlist.txt in.f64 out.f64     in = the signal sources' voltage per sample; out = the probes per sample
 *
 * netlist lines (written by Circuit.netlist): N nodes / T fs / R a b ohms / C a b farads / S a ohms vdc signal /
 * D a k Is n / Q c b e Is bf br / O p n x gm imax (imax tanh(gm (vp - vn) / imax) into x: an op-amp's input stage) /
 * G row col siemens (raw stamp) / I node amps (constant injection) / V node volts (operating point) / P node (probe)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VT 0.025852
#define MAXN 64
#define MAXE 256

typedef struct { int a, b; double v; } two_t;
typedef struct { int a; double r, vdc; int sig; } src_t;
typedef struct { int a, k; double is, n; } dio_t;
typedef struct { int c, b, e; double is, bf, br; } npn_t;
typedef struct { int a, b; double c, g, vp, ip; } cap_t;
typedef struct { int p, n, x; double gm, imax; } ota_t;

static int N, nR, nC, nS, nD, nQ, nO, nP, nG, nI, probe[MAXN];
static ota_t O[MAXE];
static struct { int r, c; double g; } GS[MAXE * 4];
static struct { int a; double i; } IS[MAXE];
static two_t R[MAXE];
static cap_t C[MAXE];
static src_t S[MAXE];
static dio_t D[MAXE];
static npn_t Q[MAXE];
static double fs, v[MAXN], G0[MAXN][MAXN], G[MAXN][MAXN], rhs[MAXN];

static double xexp(double x) { return x > 40.0 ? exp(40.0) * (1.0 + x - 40.0) : exp(x); }

static void solve(int n, double A[MAXN][MAXN], double *b)   /* nodes 1..n-1, partial pivoting */
{
    for (int k = 1; k < n; k++) {
        int p = k;
        for (int i = k + 1; i < n; i++) if (fabs(A[i][k]) > fabs(A[p][k])) p = i;
        if (p != k) {
            for (int j = 1; j < n; j++) { double t = A[k][j]; A[k][j] = A[p][j]; A[p][j] = t; }
            double t = b[k]; b[k] = b[p]; b[p] = t;
        }
        for (int i = k + 1; i < n; i++) {
            double f = A[i][k] / A[k][k];
            if (f == 0.0) continue;
            for (int j = k; j < n; j++) A[i][j] -= f * A[k][j];
            b[i] -= f * b[k];
        }
    }
    for (int k = n - 1; k >= 1; k--) {
        double s = b[k];
        for (int j = k + 1; j < n; j++) s -= A[k][j] * b[j];
        b[k] = s / A[k][k];
    }
}

static void add(int a, int b, double g)
{
    G[a][a] += g; G[b][b] += g; G[a][b] -= g; G[b][a] -= g;
}

/* one terminal current I (into the device at node t[i]) with dI/dv[t[j]] = J[i][j]: G += J, rhs -= I - J v */
static void dev(const int *t, int m, const double *I, double J[3][3])
{
    for (int i = 0; i < m; i++) {
        double s = I[i];
        for (int j = 0; j < m; j++) { G[t[i]][t[j]] += J[i][j]; s -= J[i][j] * v[t[j]]; }
        rhs[t[i]] -= s;
    }
}

static double last_step;                     /* the last Newton step of the sample (volts) */

static int step(double x)                    /* one sample: Newton from the previous solution */
{
    for (int it = 0; it < 200; it++) {
        memcpy(G, G0, sizeof G);
        memset(rhs, 0, sizeof rhs);
        for (int i = 0; i < nS; i++) rhs[S[i].a] += (S[i].vdc + (S[i].sig ? x : 0.0)) / S[i].r;
        for (int i = 0; i < nI; i++) rhs[IS[i].a] += IS[i].i;
        for (int i = 0; i < nC; i++) { double h = C[i].g * C[i].vp + C[i].ip; rhs[C[i].a] += h; rhs[C[i].b] -= h; }
        for (int i = 0; i < nD; i++) {
            double e = xexp((v[D[i].a] - v[D[i].k]) / (D[i].n * VT));
            double I[2] = { D[i].is * (e - 1.0), 0 }, g = D[i].is * e / (D[i].n * VT);
            I[1] = -I[0];
            double J[3][3] = { { g, -g }, { -g, g } };
            int t[2] = { D[i].a, D[i].k };
            dev(t, 2, I, J);
        }
        for (int i = 0; i < nQ; i++) {
            npn_t *q = &Q[i];
            double vbe = v[q->b] - v[q->e], vbc = v[q->b] - v[q->c];
            double ef = xexp(vbe / VT), er = xexp(vbc / VT);
            double ic = q->is * (ef - er) - q->is / q->br * (er - 1.0);
            double ib = q->is / q->bf * (ef - 1.0) + q->is / q->br * (er - 1.0);
            double dc_be = q->is * ef / VT, dc_bc = -q->is * er / VT * (1.0 + 1.0 / q->br);
            double db_be = q->is / q->bf * ef / VT, db_bc = q->is / q->br * er / VT;
            double J[3][3] = { { -dc_bc, dc_be + dc_bc, -dc_be }, { -db_bc, db_be + db_bc, -db_be }, { 0 } };
            for (int j = 0; j < 3; j++) J[2][j] = -J[0][j] - J[1][j];
            double I[3] = { ic, ib, -ic - ib };
            int t[3] = { q->c, q->b, q->e };
            dev(t, 3, I, J);
        }
        for (int i = 0; i < nO; i++) {      /* the current into x; into the device: minus */
            ota_t *o = &O[i];
            double th = tanh(o->gm * (v[o->p] - v[o->n]) / o->imax), g = o->gm * (1.0 - th * th);
            double I[3] = { 0.0, 0.0, -o->imax * th };
            double J[3][3] = { { 0 }, { 0 }, { -g, g, 0.0 } };
            int t[3] = { o->p, o->n, o->x };
            dev(t, 3, I, J);
        }
        double b[MAXN];
        memcpy(b, rhs, sizeof b);
        solve(N, G, b);
        double mx = 0.0;
        for (int k = 1; k < N; k++) { double d = fabs(b[k] - v[k]); if (d > mx) mx = d; }
        double damp = mx > 0.2 ? 0.2 / mx : 1.0;
        for (int k = 1; k < N; k++) v[k] += damp * (b[k] - v[k]);
        last_step = mx;
        if (mx < 1e-9) return it;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: refsim netlist in.f64 out.f64\n"); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 1; }
    char t[8];
    while (fscanf(f, "%7s", t) == 1) {
        switch (t[0]) {
        case 'N': fscanf(f, "%d", &N); break;
        case 'T': fscanf(f, "%lf", &fs); break;
        case 'R': fscanf(f, "%d %d %lf", &R[nR].a, &R[nR].b, &R[nR].v); nR++; break;
        case 'C': fscanf(f, "%d %d %lf", &C[nC].a, &C[nC].b, &C[nC].c); nC++; break;
        case 'S': fscanf(f, "%d %lf %lf %d", &S[nS].a, &S[nS].r, &S[nS].vdc, &S[nS].sig); nS++; break;
        case 'D': fscanf(f, "%d %d %lf %lf", &D[nD].a, &D[nD].k, &D[nD].is, &D[nD].n); nD++; break;
        case 'Q': fscanf(f, "%d %d %d %lf %lf %lf", &Q[nQ].c, &Q[nQ].b, &Q[nQ].e, &Q[nQ].is, &Q[nQ].bf, &Q[nQ].br); nQ++; break;
        case 'G': fscanf(f, "%d %d %lf", &GS[nG].r, &GS[nG].c, &GS[nG].g); nG++; break;
        case 'O': fscanf(f, "%d %d %d %lf %lf", &O[nO].p, &O[nO].n, &O[nO].x, &O[nO].gm, &O[nO].imax); nO++; break;
        case 'I': fscanf(f, "%d %lf", &IS[nI].a, &IS[nI].i); nI++; break;
        case 'V': { int i; double x; fscanf(f, "%d %lf", &i, &x); v[i] = x; } break;
        case 'P': fscanf(f, "%d", &probe[nP]); nP++; break;
        }
    }
    fclose(f);
    if (N > MAXN) { fprintf(stderr, "too many nodes\n"); return 1; }
    memset(G0, 0, sizeof G0);
    memcpy(G, G0, sizeof G);
    for (int i = 0; i < nR; i++) add(R[i].a, R[i].b, 1.0 / R[i].v);
    for (int i = 0; i < nS; i++) G[S[i].a][S[i].a] += 1.0 / S[i].r;
    for (int i = 0; i < nC; i++) {          /* trapezoidal: i = g (v - vp) - ip, starting at the operating point */
        C[i].g = 2.0 * C[i].c * fs;
        C[i].vp = v[C[i].a] - v[C[i].b];
        C[i].ip = 0.0;
        add(C[i].a, C[i].b, C[i].g);
    }
    for (int i = 0; i < nG; i++) G[GS[i].r][GS[i].c] += GS[i].g;
    for (int k = 1; k < N; k++) G[k][k] += 1e-12;
    memcpy(G0, G, sizeof G);

    FILE *fi = fopen(argv[2], "rb"), *fo = fopen(argv[3], "wb");
    if (!fi || !fo) { perror("open"); return 1; }
    double x, out[MAXN], worst_left = 0.0;
    long n = 0, worst = 0, unconverged = 0;
    while (fread(&x, sizeof x, 1, fi) == 1) {
        int it = step(x);
        if (it < 0) {                       /* 200 iterations: the step left (rounding at the conductances' range) */
            unconverged++;
            if (last_step > worst_left) worst_left = last_step;
        }
        if (it > worst) worst = it;
        for (int i = 0; i < nC; i++) {
            double vc = v[C[i].a] - v[C[i].b];
            C[i].ip = C[i].g * (vc - C[i].vp) - C[i].ip;
            C[i].vp = vc;
        }
        for (int i = 0; i < nP; i++) out[i] = v[probe[i]];
        fwrite(out, sizeof(double), nP, fo);
        n++;
    }
    fclose(fi); fclose(fo);
    fprintf(stderr, "refsim: %ld samples, worst %ld Newton iterations", n, worst);
    if (unconverged)
        fprintf(stderr, "; %ld samples above 1e-9 V after 200, the largest step left %.3g V", unconverged, worst_left);
    fprintf(stderr, "\n");
    return unconverged && worst_left > 1e-6 ? 2 : 0;
}
