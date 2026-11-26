/*
 * Phaser (MOD › Phaser, waveod's): a light multi-type phaser, docs/modfx.md.
 *
 * Control rate, every 16 frames:
 *   LFO (sine, triangle, a rising saw, or an RC "astable" oscillator; amplitude 1/2, the right channel Phase ahead)
 *   -> sweep x = Scale ((1 - Depth) Manual + Depth (lfo + 1/2)) -> a one-pole smoother -> max(Bias + x, 0) -> the
 *   sweep law (linear; exponential (M^x - 1) / (M - 1), M = Max / Min x Shape; JFET, square then linear; or a
 *   photocell, x^2 low-passed into an attack / release envelope) -> f = Min + (Max - Min) x -> the all-pass
 *   coefficient a = (1 - w) / (1 + w), w = pi f / fs, reached linearly over the 16 frames.
 * Audio, per channel: 2..12 first-order all-pass stages y = s - a x, s = x + a y, the chain's last output x
 *   Feedback added before the type's stage, out = (y - Feedback y[n-1]) / (1 - Feedback). The stage count and the
 *   feedback stage are constants of the inner loop (one copy per count), so the states stay in registers.
 * Then the equal-power mix with the level, ramped over one block. Bypassed, only the control rate runs (the sweep
 * keeps its place); the output is the input bit for bit.
 *
 *   Rate 0.1..10 Hz (Sync)  Depth %  Feedback -90..90 %  Stages 2..12  Min Freq 5..500 Hz  Max Freq 200..20000 Hz
 *   Mix %  Phase 0..180°  Wave  Level dB  Sync  Type (13)
 */
#include "modfx/modfx.h"

#define CTRL 16
#define NST  12

enum { K_RATE, K_DEPTH, K_FB, K_STAGES, K_MINF, K_MAXF, K_MIX, K_PHASE, K_WAVE, K_LEVEL, K_SYNC, K_TYPE, NKNOBS };

#define TYPES \
    "Digital Mono\0" "Digital Stereo\0" "Small 45\0" "Small 90\0" "Fed 90\0" "Stereo 8\0" "Bright 90\0" \
    "Rock\0" "Lamp\0" "Saw Sweep\0" "Deep 90\0" "Big Sweep\0" "Twin Lamp\0"

static const struct wvfx_param params[] = {
    { "Rate", 0.1f, 10.0f, 0.01f, 1.0f, K_RATE, 0, 0x10 | K_SYNC, 0, 0, "%.2f Hz" },
    { "Depth", 0.0f, 100.0f, 1.0f, 80.0f, K_DEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Feedback", -90.0f, 90.0f, 1.0f, 40.0f, K_FB, 0, 0, 0, 0, "%.0f%%" },
    { "Stages", 2.0f, 12.0f, 2.0f, 4.0f, K_STAGES, 0, 0, 0, 0, 0 },
    { "Min Freq", 5.0f, 500.0f, 1.0f, 200.0f, K_MINF, 0, 0, 0, 0, "%.0f Hz" },
    { "Max Freq", 200.0f, 20000.0f, 10.0f, 2000.0f, K_MAXF, 0, 0, 0, 0, "%.0f Hz" },
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Phase", 0.0f, 180.0f, 1.0f, 90.0f, K_PHASE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Wave", 0.0f, 1.0f, 1.0f, 1.0f, K_WAVE, 0, 0, 0, "Sine\0Triangle\0", 0 },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
    { "Type", 0.0f, 12.0f, 1.0f, 1.0f, K_TYPE, 0, 0, 0, TYPES, 0 },
};

/*
 * The types. law: 0 linear, 1 exponential, 2 JFET, 3 photocell; tap: the stage the feedback goes in before (at or
 * past the last stage: no feedback); form: 0 the Wave knob, 3 rising saw, 9 astable; scale: the sweep's span;
 * bias: added to the sweep; duty: the astable's curvature; smooth: the sweep smoother (Hz at the control rate x 16);
 * up, down: the photocell's attack and release, s.
 */
struct type {
    uint8_t law, tap, form;
    float scale, manual, bias, shape, duty, smooth, up, down;
};

static const struct type types[] = {
    /* law tap form scale   manual bias        shape duty        smooth       up     down */
    { 1, 0, 0, 1.0f,  0.5f, 0.0f,         1.0f, 0.5f,       800.00006f,  0.01f, 0.0349988f },   /* Digital Mono */
    { 1, 0, 0, 1.0f,  0.5f, 0.0f,         1.0f, 0.5f,       800.00006f,  0.01f, 0.0349988f },   /* Digital Stereo */
    { 2, 0, 9, 0.32f, 0.5f, 0.0599993f,   1.0f, 0.3333369f, 64.00203f,   0.01f, 0.0049999f },   /* Small 45 */
    { 2, 0, 9, 0.32f, 0.5f, 0.0639973f,   1.0f, 0.2419975f, 64.00203f,   0.01f, 0.0349988f },   /* Small 90 */
    { 2, 2, 9, 0.32f, 0.5f, 0.0639973f,   1.0f, 0.2419975f, 64.00203f,   0.01f, 0.0349988f },   /* Fed 90 */
    { 1, 0, 0, 1.0f,  0.5f, 0.0f,         1.0f, 0.5f,       800.00006f,  0.01f, 0.0349988f },   /* Stereo 8 */
    { 2, 2, 9, 0.32f, 0.5f, 0.0639973f,   1.0f, 0.2419975f, 64.00203f,   0.01f, 0.0349988f },   /* Bright 90 */
    { 1, 0, 0, 1.0f,  0.5f, 0.0f,         1.0f, 0.5f,       175.998f,    0.01f, 0.0349988f },   /* Rock */
    { 3, 0, 0, 1.0f,  0.5f, -0.0260018f,  1.0f, 0.5f,       320.00671f,  0.01f, 0.0349988f },   /* Lamp */
    { 2, 0, 3, 1.0f,  0.5f, -0.0299997f,  1.0f, 0.5f,       22.399958f,  0.01f, 0.0349988f },   /* Saw Sweep */
    { 2, 2, 9, 0.3f,  0.5f, 0.0100101f,   1.0f, 0.2419975f, 38.399376f,  0.01f, 0.0349988f },   /* Deep 90 */
    { 1, 0, 0, 1.0f,  0.5f, 0.0f,         1.0f, 0.5f,       24.000284f,  0.01f, 0.0349988f },   /* Big Sweep */
    { 3, 0, 0, 1.0f,  0.5f, -0.0260018f,  1.0f, 0.5f,       320.00671f,  0.01f, 0.0349988f },   /* Twin Lamp */
};
#define NTYPES ((int)(sizeof types / sizeof types[0]))

struct design {
    int form, law, nst, tap;
    float inc, ofs, depth, manual, scale, bias, shape, minf, maxf, fb, g;
    float q, l2, kq;                    /* astable: curvature, 2 ln q, 2 / (1 - q) */
    float up, down;                     /* photocell envelope coefficients per control step */
    struct op sm;
    float gw, gd;
};

struct state {
    float knobs[NKNOBS];
    uint32_t beat;
    int designed;
    struct design d;
    float ph;
    float a[2];                         /* the all-pass coefficient where the last control step ended */
    float st[2][NST];
    float prev[2];                      /* the chain's last output */
    float sm[2], pc[2], env[2];
    float gw, gd, gt;
};

static void design(struct state *s, const float *k, uint32_t beat_ms, float fs)
{
    struct design *d = &s->d;
    const struct type *t = &types[modfx_option(k[K_TYPE], NTYPES)];
    float rate = modfx_rate(k[K_RATE], k[K_SYNC] >= 0.5f, beat_ms, 0.1f, 10.0f);
    d->form = t->form ? t->form : k[K_WAVE] >= 0.5f;
    d->law = t->law;
    d->nst = (int)(wvfx_clamp(k[K_STAGES], 2.0f, 12.0f) * 0.5f + 0.25f) * 2;
    d->tap = t->tap;
    d->inc = rate * MODFX_TWO_PI / fs * (float)CTRL;
    d->ofs = wvfx_clamp(k[K_PHASE], 0.0f, 180.0f) * (MODFX_PI / 180.0f);
    d->depth = wvfx_clamp(k[K_DEPTH], 0.0f, 100.0f) * 0.01f;
    d->manual = t->manual;
    d->scale = t->scale;
    d->bias = t->bias;
    d->shape = t->shape;
    d->minf = wvfx_clamp(k[K_MINF], 5.0f, 500.0f);
    d->maxf = wvfx_clamp(k[K_MAXF], 200.0f, 20000.0f);
    if (d->maxf > 0.45f * fs)
        d->maxf = 0.45f * fs;
    d->fb = wvfx_clamp(k[K_FB], -90.0f, 90.0f) * 0.01f;
    d->g = 1.0f / (1.0f - d->fb);
    float du = t->duty < 0.01f ? 0.462f : t->duty > 0.999f ? 0.999f : t->duty;
    d->q = (1.0f - du) / (1.0f + du);
    d->l2 = 2.0f * 0.69314718f * wvfx_log2(d->q);
    d->kq = 2.0f / (1.0f - d->q);
    d->up = modfx_exp2(-1.44269504f * (float)CTRL / (t->up * fs));
    d->down = modfx_exp2(-1.44269504f * (float)CTRL / (t->down * fs));
    d->sm = op_lp(t->smooth, fs);
    float sw, cw;
    modfx_mix(wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, &sw, &cw);
    float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = sw * level;
    d->gd = cw * level;
}

static inline float lfo_shape(const struct design *d, float p)
{
    switch (d->form) {
    case 0: {
        float sn, cs;
        modfx_sincos(p, &sn, &cs);
        return -cs;
    }
    case 3: {                                           /* a triangle skewed to 0.99: a rising saw */
        float v = 0.99f > p ? p * (2.0f / 0.99f) - 1.0f : -200.0f * (p - 0.99f) + 1.0f;
        return wvfx_clamp(v, -1.0f, 1.0f);
    }
    case 9:                                             /* RC charge over the first half, discharge over the second */
        return 0.5f > p ? d->kq * (1.0f - modfx_exp2(1.44269504f * d->l2 * p)) - 1.0f
                        : d->kq * (modfx_exp2(1.44269504f * d->l2 * (p - 0.5f)) - d->q) - 1.0f;
    default:
        return modfx_tri(p);
    }
}

/* the law of the sweep at x >= 0, per channel (the photocell keeps state) */
static inline float law(struct state *s, int ch, float x)
{
    const struct design *d = &s->d;
    switch (d->law) {
    case 1: {
        float m = d->maxf / d->minf * d->shape;
        if (m < 0.99f || m > 1.01f)
            return (modfx_pow(m, x) - 1.0f) / (m - 1.0f);
        return x;
    }
    case 2:
        return x < 0.125f ? 4.2666664f * (x * x) : 4.2666664f * (0.25f * x - 0.015625f);
    case 3: {
        float y = op_run(&d->sm, &s->pc[ch], x * x);
        float *e = &s->env[ch];
        *e = y > *e ? d->up * *e + (1.0f - d->up) * y : d->down * *e + (1.0f - d->down) * y;
        return *e;
    }
    default:
        return x;
    }
}

/* one control step: the target coefficients */
static void control(struct state *s, float fs, float a[2])
{
    const struct design *d = &s->d;
    s->ph = modfx_advance(s->ph, d->inc);
    float q = d->ofs + s->ph;
    if (q >= MODFX_TWO_PI)
        q -= MODFX_TWO_PI;
    float v[2];
    v[0] = 0.5f * lfo_shape(d, s->ph * (1.0f / MODFX_TWO_PI));
    v[1] = d->ofs > 0.0f ? 0.5f * lfo_shape(d, q * (1.0f / MODFX_TWO_PI)) : v[0];
    const float man = d->manual * (1.0f - d->depth);
    for (int ch = 0; ch < 2; ch++) {
        float x = d->scale * (man + d->depth * (v[ch] + 0.5f));
        if (d->law != 3)
            x = op_run(&d->sm, &s->sm[ch], x);
        x = __builtin_fmaxf(d->bias + x, 0.0f);
        x = law(s, ch, x);
        float f = (d->maxf - d->minf) * x + d->minf, w = f * MODFX_PI / fs, n = 1.0f - w;
        a[ch] = n == 0.0f ? 0.0f : n / (1.0f + w);
    }
}

/* the all-pass chain of one channel over m frames, the coefficient ramping by dc: NS stages, the feedback before
   stage TAP (TAP >= NS: none) */
static inline __attribute__((always_inline)) float chain(float *st, float *prev, float *x, int stride, int m, float c,
                                                        float dc, float fb, float g, const int ns, const int tap)
{
    float z[NST];
    for (int k = 0; k < ns; k++)
        z[k] = st[k];
    float yp = *prev;
    for (int i = 0; i < m; i++) {
        c = dc + c;
        float v = x[i * stride];
        for (int k = 0; k < ns; k++) {
            if (k == tap)
                v = yp * fb + v;
            const float y = z[k] - v * c;
            z[k] = c * y + v;
            v = y;
        }
        x[i * stride] = (v - yp * fb) * g;
        yp = v;
    }
    for (int k = 0; k < ns; k++)
        st[k] = z[k];
    *prev = yp;
    return c;
}

#define CHAIN(ns) \
    (tap == 0 ? chain(st, prev, x, stride, m, c, dc, fb, g, ns, 0) : tap == 2 ? chain(st, prev, x, stride, m, c, dc, fb, g, ns, 2) \
              : chain(st, prev, x, stride, m, c, dc, fb, g, ns, NST))

static float run_chain(float *st, float *prev, float *x, int stride, int m, float c, float dc, float fb, float g, int ns,
                      int tap)
{
    switch (ns) {
    case 2: return CHAIN(2);
    case 4: return CHAIN(4);
    case 6: return CHAIN(6);
    case 8: return CHAIN(8);
    case 10: return CHAIN(10);
    default: return CHAIN(12);
    }
}

static int changed(struct state *s, const float *k, uint32_t beat)
{
    int c = !s->designed || (k[K_SYNC] >= 0.5f && beat != s->beat);
    for (int i = 0; i < NKNOBS; i++)
        if (k[i] != s->knobs[i]) {
            c = 1;
            s->knobs[i] = k[i];
        }
    s->beat = beat;
    s->designed = 1;
    return c;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    s->a[0] = s->a[1] = 0.95f;
    changed(s, knobs, 500);
    design(s, knobs, 500, MODFX_FS);
    s->gt = 1.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    const float fs = MODFX_FS;
    if (changed(s, io->knobs, io->beat_ms))
        design(s, io->knobs, io->beat_ms, fs);
    const struct design *d = &s->d;
    float *x = io->io;
    const float tw = io->enabled ? d->gw : 0.0f, td = io->enabled ? d->gd : 0.0f, tt = io->enabled ? 0.0f : 1.0f;
    const int idle = !io->enabled && s->gw == 0.0f && s->gd == 0.0f && s->gt == 1.0f;
    float wet[2][WVFX_MAX_FRAMES];
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < n; i++)
            wet[ch][i] = x[2 * i + ch];
    for (int c0 = 0; c0 < n; c0 += CTRL) {
        float a[2];
        control(s, fs, a);
        const int m = n - c0 < CTRL ? n - c0 : CTRL;
        for (int ch = 0; ch < 2; ch++) {
            const float dc = 0.0625f * (a[ch] - s->a[ch]);
            if (!idle) {
                s->a[ch] = run_chain(s->st[ch], &s->prev[ch], &wet[ch][c0], 1, m, s->a[ch], dc, d->fb, d->g, d->nst, d->tap);
            } else {
                for (int i = 0; i < m; i++)
                    s->a[ch] = dc + s->a[ch];
            }
        }
    }
    if (idle)
        return;
    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n), dt = wvfx_ramp_step(s->gt, tt, n);
    float gw = s->gw, gd = s->gd, gt = s->gt;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        gt += dt;
        x[2 * i] = gw * wet[0][i] + (gd + gt) * x[2 * i];
        x[2 * i + 1] = gw * wet[1][i] + (gd + gt) * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
    s->gt = tt;
}

const struct wvfx_desc phaser_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 3),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Phaser",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
