/*
 * Rotary (MOD › Rotary, waveod's): a light rotating-speaker simulation, docs/modfx.md.
 *
 * Mono in, 0.7 (L + R) -> drive: a 2.5 kHz +16 dB shelf, x Drive, a soft clip, / Drive, the -16 dB shelf back ->
 * crossover: the drum low-passed at 800 Hz and high-passed at 50 Hz, the horn high-passed at 800 Hz with a +5 dB
 * peak at 1.5 kHz -> each into a short line.
 * The rotors: their speeds follow Rate with inertia (the drum x Low Rate, 2 s; the horn 0.8 s), updated every 16
 * frames; each turns a phase ramp. The distance from a mic to the rotor's mouth, |mic - rotor| / 350 m/s over 256
 * angles (a table made at init: rotor radius 0.14 m, the mic 0.1 m from its circle), gives the Doppler delay
 * (a 4-point cubic read) and the level, 1 - Depth (distance - nearest) / (farthest - nearest). The drum feeds
 * both sides through one mic, the horn has two, the right Spacing ahead.
 * Then drum + horn x Hi Level per side, Spread (mid / side), and a linear mix with the level, ramped over one
 * block. Bypassed, only the rotors keep turning; the lines are cleared when it comes back.
 *
 *   Rate 0..10 Hz (the horn)  Low Rate x0.1..10 (the drum)  Low Depth %  Hi Depth %  Hi Level -6..6 dB
 *   Spacing 0..180°  Drive 0.05..50  Spread -200..200 %  Mix %  Level dB
 */
#include "modfx/modfx.h"

#define LINE   128u                     /* the Doppler sweep is < 0.8 ms */
#define MASK   (LINE - 1u)
#define STRIDE (LINE + 4u)
#define CTRL   16
#define LAT    7.0f                     /* frames added to every read */
#define TAB    256

enum { K_RATE, K_LOWRATE, K_LODEPTH, K_HIDEPTH, K_HILEVEL, K_SPACING, K_DRIVE, K_SPREAD, K_MIX, K_LEVEL, NKNOBS };

static const struct wvfx_param params[] = {
    { "Rate", 0.0f, 10.0f, 0.01f, 6.5f, K_RATE, 0, 0, 0, 0, "%.2f Hz" },
    { "Low Rate", 0.1f, 10.0f, 0.01f, 0.9f, K_LOWRATE, 0, 0, 0, 0, "x%.2f" },
    { "Low Depth", 0.0f, 100.0f, 1.0f, 25.0f, K_LODEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Hi Depth", 0.0f, 100.0f, 1.0f, 25.0f, K_HIDEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Hi Level", -6.0f, 6.0f, 0.1f, 0.0f, K_HILEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Spacing", 0.0f, 180.0f, 1.0f, 90.0f, K_SPACING, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Drive", 0.05f, 50.0f, 0.05f, 0.05f, K_DRIVE, 0, 0, 0, 0, "%.2f" },
    { "Spread", -200.0f, 200.0f, 1.0f, 50.0f, K_SPREAD, 0, 0, 0, 0, "%.0f%%" },
    { "Mix", 0.0f, 100.0f, 1.0f, 100.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
};

/* the audio cookbook's high shelf and peak */
static struct bq bq_hshelf(float f, float q, float db, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    const float a = modfx_exp2(db * (0.16609640474f * 0.5f)), al = sn / (2.0f * q);
    float sa = 2.0f * al;                                   /* 2 sqrt(A) alpha */
    {
        float r = a > 1.0f ? a : 1.0f;                      /* sqrt by Newton */
        for (int i = 0; i < 12; i++)
            r = 0.5f * (r + a / r);
        sa *= r;
    }
    const float b0 = a * ((a + 1.0f) + (a - 1.0f) * cs + sa), b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cs);
    const float b2 = a * ((a + 1.0f) + (a - 1.0f) * cs - sa);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cs + sa, a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cs);
    const float a2 = (a + 1.0f) - (a - 1.0f) * cs - sa, n = 1.0f / a0;
    return (struct bq){ b0 * n, b1 * n, b2 * n, -a1 * n, -a2 * n };
}

static struct bq bq_peak(float f, float q, float db, float fs)
{
    float sn, cs;
    modfx_sincos(f / fs, &sn, &cs);
    const float a = modfx_exp2(db * (0.16609640474f * 0.5f)), al = sn / (2.0f * q), n = 1.0f / (1.0f + al / a);
    return (struct bq){ (1.0f + al * a) * n, -2.0f * cs * n, (1.0f - al * a) * n, 2.0f * cs * n, -(1.0f - al / a) * n };
}

struct design {
    float rate, lowmul, ofs;            /* ofs: the horn's right mic, rad */
    float kdrum, khorn;                 /* depth / (farthest - nearest) */
    float hilevel, drive, rdrive, spread;
    float gw, gd;
};

struct state {
    float *line[2];                     /* drum, horn */
    uint32_t w;
    float knobs[NKNOBS];
    int designed, idle;
    struct design d;
    struct bq pre, de, lp800, hp50, hp800, pk1500;
    float s_pre[2], s_de[2], s_lp[2], s_hp50[2], s_hp800[2], s_pk[2];
    float s1, s2;                       /* rotor speeds */
    float ph1, ph2;                     /* rotor phases, rad */
    float q[3], g[3];                   /* the mics' delay and level where the last step ended */
    float gw, gd, gt;
    float tab[TAB + 1];                 /* mic distance by angle, s, minus the nearest */
};

/* rotor inertia per 16 frames at 44.1 kHz, r = exp(-16 / (T fs)), k = 1 - r: the drum 2 s, the horn 0.8 s */
static const float r1 = 0x1.ffe83ap-1f, k1 = 0x1.7c6p-13f, r2 = 0x1.ffc492p-1f, k2 = 0x1.db7p-12f;

static void design(struct state *s, const float *k)
{
    struct design *d = &s->d;
    d->rate = wvfx_clamp(k[K_RATE], 0.0f, 10.0f);
    d->lowmul = wvfx_clamp(k[K_LOWRATE], 0.1f, 10.0f);
    d->ofs = wvfx_clamp(k[K_SPACING], 0.0f, 180.0f) * (MODFX_PI / 180.0f);
    const float span = s->tab[TAB / 2];
    d->kdrum = wvfx_clamp(k[K_LODEPTH], 0.0f, 100.0f) * 0.01f / span;
    d->khorn = wvfx_clamp(k[K_HIDEPTH], 0.0f, 100.0f) * 0.01f / span;
    d->hilevel = wvfx_db(wvfx_clamp(k[K_HILEVEL], -6.0f, 6.0f));
    d->drive = wvfx_clamp(k[K_DRIVE], 0.05f, 50.0f);
    d->rdrive = 1.0f / d->drive;
    d->spread = wvfx_clamp(k[K_SPREAD], -200.0f, 200.0f) * 0.01f;
    const float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = mix * level;                                    /* linear mix */
    d->gd = (1.0f - mix) * level;
}

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

static void mics(const struct state *s, float q[3], float g[3]);

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    float *b = buffer;
    for (unsigned i = 0; i < 2 * STRIDE; i++)
        b[i] = 0.0f;
    s->line[0] = b;
    s->line[1] = b + STRIDE;
    const float fs = MODFX_FS;
    s->pre = bq_hshelf(2500.0f, 0.367f, 16.0f, fs);
    s->de = bq_hshelf(2500.0f, 0.367f, -16.0f, fs);
    s->lp800 = bq_rbj(0, 800.0f, 0.707f, fs);
    s->hp50 = bq_rbj(1, 50.0f, 0.707f, fs);
    s->hp800 = bq_rbj(1, 800.0f, 0.707f, fs);
    s->pk1500 = bq_peak(1500.0f, 0.707f, 5.0f, fs);
    const double rr = 0.4 * 0.35, dm = 0.1;                 /* the distance table */
    float t0 = 0.0f;
    for (int k = 0; k <= TAB; k++) {
        float sn, cs;
        modfx_sincos((float)(k & (TAB - 1)) / (float)TAB, &sn, &cs);
        const double x = dm + rr - rr * cs, y = rr * sn, q = x * x + y * y;
        double z = q > 1.0 ? q : 1.0;
        for (int i = 0; i < 40; i++)
            z = 0.5 * (z + q / z);
        float v = (float)(z / 350.0);
        if (k == 0)
            t0 = v;
        s->tab[k] = v - t0;
    }
    changed(s, knobs);
    design(s, knobs);
    mics(s, s->q, s->g);
    s->gt = 1.0f;
    return 0;
}

/* the delay (frames, past LAT) and the level of a mic at rotor angle a (turns) */
static inline float doppler(const struct state *s, float a, float *dist)
{
    const float x = 255.0f * a;
    int k = (int)x;
    if (k > 254)
        k = 254;
    float d = (s->tab[k + 1] - s->tab[k]) * (x - (float)k) + s->tab[k];
    d = d > 0.0f ? d : 0.0f;
    *dist = d;
    return MODFX_FS * d;
}


/* the delays (frames, with LAT) and levels of the drum's mic and the horn's two at the rotors' phases */
static void mics(const struct state *s, float q[3], float g[3])
{
    const struct design *d = &s->d;
    const float u = 1.0f / MODFX_TWO_PI;
    float p2r = d->ofs + s->ph2;
    if (p2r >= MODFX_TWO_PI)
        p2r = (p2r - MODFX_TWO_PI) + 1.7484555e-7f;
    float v[3];
    q[0] = LAT + doppler(s, s->ph1 * u, &v[0]);
    q[1] = LAT + doppler(s, s->ph2 * u, &v[1]);
    q[2] = LAT + doppler(s, p2r * u, &v[2]);
    g[0] = 1.0f - v[0] * d->kdrum;
    g[1] = d->hilevel * (1.0f - v[1] * d->khorn);
    g[2] = d->hilevel * (1.0f - v[2] * d->khorn);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    if (changed(s, io->knobs))
        design(s, io->knobs);
    const struct design *d = &s->d;
    float *x = io->io;
    const float tw = io->enabled ? d->gw : 0.0f, td = io->enabled ? d->gd : 0.0f, tt = io->enabled ? 0.0f : 1.0f;
    const int idle = !io->enabled && s->gw == 0.0f && s->gd == 0.0f && s->gt == 1.0f;
    if (idle) {                                             /* the rotors keep turning */
        for (int c0 = 0; c0 < n; c0 += CTRL) {
            s->s1 = d->rate * k1 + r1 * s->s1;
            s->s2 = d->rate * k2 + s->s2 * r2;
            const int m = n - c0 < CTRL ? n - c0 : CTRL;
            const float i1 = d->lowmul * s->s1 * MODFX_TWO_PI / MODFX_FS, i2 = s->s2 * MODFX_TWO_PI / MODFX_FS;
            for (int i = 0; i < m; i++) {
                s->ph1 = modfx_advance(s->ph1, i1);
                s->ph2 = modfx_advance(s->ph2, i2);
            }
        }
        s->idle = 1;
        return;
    }
    if (s->idle) {                                          /* back: no stale audio in the lines */
        for (unsigned i = 0; i < 2 * STRIDE; i++)
            s->line[0][i] = 0.0f;
        mics(s, s->q, s->g);
        s->idle = 0;
    }
    float drum[WVFX_MAX_FRAMES], horn[WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++)
        drum[i] = 0.7f * (x[2 * i] + x[2 * i + 1]);
    bq_block(&s->pre, s->s_pre, drum, n);
    {
        const float g = d->drive, rg = d->rdrive;
        for (int i = 0; i < n; i++) {                       /* soft clip: two cubic knees around +-0.7 */
            const float v = 1.4f * (g * drum[i]);
            const float a = wvfx_clamp(v - 0.7f, -1.0f, 1.0f), b = wvfx_clamp(-v - 0.7f, -1.0f, 1.0f);
            drum[i] = rg * ((0.75f * a - a * (a * (a * 0.25f))) - (0.75f * b - b * (b * (0.25f * b))));
        }
    }
    bq_block(&s->de, s->s_de, drum, n);
    for (int i = 0; i < n; i++)
        horn[i] = drum[i];
    bq_block(&s->lp800, s->s_lp, drum, n);
    bq_block(&s->hp50, s->s_hp50, drum, n);
    bq_block(&s->hp800, s->s_hp800, horn, n);
    bq_block(&s->pk1500, s->s_pk, horn, n);
    const uint32_t w = s->w;
    for (int i = 0; i < n; i++) {
        s->line[0][(w + (uint32_t)i) & MASK] = drum[i];
        s->line[1][(w + (uint32_t)i) & MASK] = horn[i];
    }
    modfx_mirror(s->line[0], LINE);
    modfx_mirror(s->line[1], LINE);

    float wl[WVFX_MAX_FRAMES], wr[WVFX_MAX_FRAMES];
    for (int c0 = 0; c0 < n; c0 += CTRL) {
        s->s1 = d->rate * k1 + r1 * s->s1;
        s->s2 = d->rate * k2 + s->s2 * r2;
        const int m = n - c0 < CTRL ? n - c0 : CTRL;
        const float i1 = d->lowmul * s->s1 * MODFX_TWO_PI / MODFX_FS, i2 = s->s2 * MODFX_TWO_PI / MODFX_FS;
        for (int i = 0; i < m; i++) {                       /* the phases: a float sum frame by frame */
            s->ph1 = modfx_advance(s->ph1, i1);
            s->ph2 = modfx_advance(s->ph2, i2);
        }
        /* the three mics' delay and level at the step's end, reached linearly over it */
        float q[3], g[3];
        mics(s, q, g);
        const float r = 1.0f / (float)m;
        float dq[3], dg[3];
        for (int j = 0; j < 3; j++) {
            dq[j] = (q[j] - s->q[j]) * r;
            dg[j] = (g[j] - s->g[j]) * r;
        }
        float q0 = s->q[0], q1 = s->q[1], q2 = s->q[2], g0 = s->g[0], g1 = s->g[1], g2 = s->g[2];
        const float *drum_l = s->line[0], *horn_l = s->line[1];
        for (int i = c0; i < c0 + m; i++) {
            q0 += dq[0];
            q1 += dq[1];
            q2 += dq[2];
            g0 += dg[0];
            g1 += dg[1];
            g2 += dg[2];
            const uint32_t wi = w + (uint32_t)i;
            const float dr = modfx_tap(drum_l, MASK, wi, q0) * g0;
            wl[i] = dr + modfx_tap(horn_l, MASK, wi, q1) * g1;
            wr[i] = dr + modfx_tap(horn_l, MASK, wi, q2) * g2;
        }
        for (int j = 0; j < 3; j++) {
            s->q[j] = q[j];
            s->g[j] = g[j];
        }
    }
    s->w = (w + (uint32_t)n) & MASK;
    {
        const float a = d->spread < 0.0f ? -d->spread : d->spread, g = 1.0f / (1.0f + a), gs = d->spread * g;
        for (int i = 0; i < n; i++) {
            const float mm = (wl[i] + wr[i]) * g, sd = (wl[i] - wr[i]) * gs;
            wl[i] = mm + sd;
            wr[i] = mm - sd;
        }
    }
    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n), dt = wvfx_ramp_step(s->gt, tt, n);
    float gw = s->gw, gd = s->gd, gt = s->gt;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        gt += dt;
        x[2 * i] = gw * wl[i] + (gd + gt) * x[2 * i];
        x[2 * i + 1] = gw * wr[i] + (gd + gt) * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
    s->gt = tt;
}

const struct wvfx_desc rotary_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 4),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Rotary",
    .category = "Rotary",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 2 * STRIDE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
