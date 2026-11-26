/*
 * Vibe (MOD › Vibe, waveod's): a light lamp-and-photocell vibe, docs/modfx.md.
 *
 * Control rate, every 16 frames:
 *   LFO (sine or triangle, amplitude 1/2, the right channel Phase ahead) x Scale (on the Lamp type x clamp(Rate / 4,
 *   0.33, 1): the lamp's swing grows with the rate) -> a one-pole smoother (not with the photocell) ->
 *   x = (Bias + 1/2) + (Manual - 1/2)(1 - Depth) + Depth lfo, clamped to 0..1 -> the law:
 *     photocell: x^2 -> one-pole -> an attack / release envelope e -> the LDR's conductance g = Gd + e (Gl - Gd)
 *     -> f = 1 / (2 pi C (1/g + Rs));
 *     exponential: f = Min/10 + (Max - Min)/10 (M^x - 1) / (M - 1), M = Max / Min x Shape;
 *   -> four stages at f x (1, r1, r2, r3), at most 21.1 kHz, each with its all-pass coefficient (1 - w) / (1 + w),
 *   w = tan(pi f / fs), reached linearly over the 16 frames.
 * Audio, per channel: the four first-order all-pass stages, the chain's last output x Feedback added at the
 * first; the chain output is the wet (no normalisation). Then the type's low cut / high cut (one-poles) and the
 * equal-power mix with the level, ramped over one block (Mix 100 % = vibrato). Bypassed, only the control rate
 * runs; the output is the input bit for bit.
 *
 *   Rate 0.1..10 Hz (Sync)  Depth %  Feedback -90..90 %  Mix %  Phase 0..180°  Wave  Level dB  Sync  Type (3)
 */
#include "modfx/modfx.h"

#define CTRL 16
#define NST  4

enum { K_RATE, K_DEPTH, K_FB, K_MIX, K_PHASE, K_WAVE, K_LEVEL, K_SYNC, K_TYPE, NKNOBS };

static const struct wvfx_param params[] = {
    { "Rate", 0.1f, 10.0f, 0.01f, 1.0f, K_RATE, 0, 0x10 | K_SYNC, 0, 0, "%.2f Hz" },
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, K_DEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Feedback", -90.0f, 90.0f, 1.0f, 0.0f, K_FB, 0, 0, 0, 0, "%.0f%%" },
    { "Mix", 0.0f, 100.0f, 1.0f, 53.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Phase", 0.0f, 180.0f, 1.0f, 0.0f, K_PHASE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Wave", 0.0f, 1.0f, 1.0f, 0.0f, K_WAVE, 0, 0, 0, "Sine\0Triangle\0", 0 },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
    { "Type", 0.0f, 2.0f, 1.0f, 0.0f, K_TYPE, 0, 0, 0, "Lamp\0Exp\0Bright Lamp\0", 0 },
};

/*
 * The types. photo: the photocell law (else exponential); swing: the lamp's swing follows the rate; scale, manual,
 * bias, shape: the sweep; minf, maxf: Hz (x 1/10 in the sweep); gl, gd: the LDR's conductance lit / dark; rs, cap:
 * its series resistor and capacitor; up, down: the photocell's attack / release, s; ratio: the stages' frequencies;
 * smooth: the sweep smoother (Hz at the control rate x 16); lowcut, hicut: Hz, 0 = off.
 */
struct type {
    uint8_t photo, swing;
    float scale, manual, bias, shape, minf, maxf, gl, gd, rs, cap, up, down, ratio[NST], smooth, lowcut, hicut;
};

static const struct type types[] = {
    { 1, 1, 1.5f, 0.5f, -0.324015021f, 1.0f, 11.3796291f, 890.574768f, 0.000292124489f, 1.58472312e-06f, 4700.0f,
      2.2e-07f, 0.0450007419f, 0.0249994424f, { 1.0f, 14.6f, 46.8f, 468.0f }, 266.72f, 47.0009f, 10500.0f },   /* Lamp */
    { 0, 0, 1.0f, 0.5f, 0.0f, 1.0f, 6.80015802f, 3615.08032f, 0.000999999931f, 9.44157648e-07f, 4700.0f,
      2.2e-07f, 0.0900025124f, 0.0180006419f, { 1.0f, 8.0f, 64.0f, 512.0f }, 145.603f, 0.0f, 0.0f },          /* Exp */
    { 1, 0, 1.5f, 0.5f, -0.324015021f, 1.0f, 11.3796291f, 890.574768f, 0.000292124489f, 1.58472312e-06f, 4700.0f,
      2.2e-07f, 0.0450007419f, 0.0249994424f, { 1.0f, 14.6f, 46.8f, 468.0f }, 266.72f, 30.0002f, 14999.9f },   /* Bright Lamp */
};
#define NTYPES ((int)(sizeof types / sizeof types[0]))

struct design {
    const struct type *t;
    int tri, lowcut_on, hicut_on;
    float inc, ofs, depth, scale, fb, up, down, m, rm1;
    struct op sm, lowcut, hicut;
    float gw, gd;
};

struct state {
    float knobs[NKNOBS];
    uint32_t beat;
    int designed;
    struct design d;
    float ph;
    float a[2][NST];                    /* the stages' coefficients where the last control step ended */
    float st[2][NST];
    float prev[2];
    float sm[2], pc[2], env[2], s_lc[2], s_hc[2];
    float gw, gd, gt;
};

static void design(struct state *s, const float *k, uint32_t beat_ms, float fs)
{
    struct design *d = &s->d;
    const struct type *t = &types[modfx_option(k[K_TYPE], NTYPES)];
    float rate = modfx_rate(k[K_RATE], k[K_SYNC] >= 0.5f, beat_ms, 0.1f, 10.0f);
    d->t = t;
    d->tri = k[K_WAVE] >= 0.5f;
    d->inc = rate * MODFX_TWO_PI / fs * (float)CTRL;
    d->ofs = wvfx_clamp(k[K_PHASE], 0.0f, 180.0f) * (MODFX_PI / 180.0f);
    d->depth = wvfx_clamp(k[K_DEPTH], 0.0f, 100.0f) * 0.01f;
    d->scale = t->scale;
    if (t->swing)
        d->scale *= wvfx_clamp(0.25f * rate, 0.33f, 1.0f);
    d->fb = wvfx_clamp(k[K_FB], -90.0f, 90.0f) * 0.01f;
    d->up = modfx_exp2(-1.44269504f * (float)CTRL / (t->up * fs));
    d->down = modfx_exp2(-1.44269504f * (float)CTRL / (t->down * fs));
    d->m = t->maxf / t->minf * t->shape;
    d->rm1 = 1.0f / (d->m - 1.0f);
    d->sm = op_lp(t->smooth, fs);
    d->lowcut_on = t->lowcut > 20.01f;
    if (d->lowcut_on)
        d->lowcut = op_hp(t->lowcut, fs);
    d->hicut_on = t->hicut > 0.0f && t->hicut < 19990.0f;
    if (d->hicut_on)
        d->hicut = op_lp(t->hicut, fs);
    float sw, cw;
    modfx_mix(wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, &sw, &cw);
    float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = sw * level;
    d->gd = cw * level;
}

static inline float lfo_shape(int tri, float p)
{
    if (tri)
        return modfx_tri(p);
    float sn, cs;
    modfx_sincos(p, &sn, &cs);
    return -cs;
}

/* one control step: the target coefficients of the stages, per channel */
static void control(struct state *s, float fs, float a[2][NST])
{
    const struct design *d = &s->d;
    const struct type *t = d->t;
    s->ph = modfx_advance(s->ph, d->inc);
    float q = d->ofs + s->ph;
    if (q >= MODFX_TWO_PI)
        q -= MODFX_TWO_PI;
    float v[2];
    v[0] = 0.5f * lfo_shape(d->tri, s->ph * (1.0f / MODFX_TWO_PI));
    v[1] = d->ofs > 0.0f ? 0.5f * lfo_shape(d->tri, q * (1.0f / MODFX_TWO_PI)) : v[0];
    const float man = (t->manual - 0.5f) * (1.0f - d->depth), b5 = t->bias + 0.5f;
    for (int ch = 0; ch < 2; ch++) {
        float x = d->scale * v[ch];
        if (!t->photo)
            x = op_run(&d->sm, &s->sm[ch], x);
        x = wvfx_clamp(b5 + (man + x * d->depth), 0.0f, 1.0f);
        float f;
        if (t->photo) {                                 /* the lamp lights the LDR: an RC low-pass's corner */
            float y = op_run(&d->sm, &s->pc[ch], x * x), *e = &s->env[ch];
            *e = y > *e ? d->up * *e + (1.0f - d->up) * y : d->down * *e + (1.0f - d->down) * y;
            const float g = t->gd + *e * (t->gl - t->gd);
            f = 1.0f / (t->cap * ((1.0f / g + t->rs) * MODFX_TWO_PI));
        } else {
            const float lo = 0.1f * t->minf, hi = 0.1f * t->maxf;
            if (d->m < 0.99f || d->m > 1.01f)
                x = (modfx_pow(d->m, x) - 1.0f) * d->rm1;
            f = (hi - lo) * x + lo;
        }
        for (int k = 0; k < NST; k++) {
            const float fk = __builtin_fminf(f * t->ratio[k], 21131.25f), w = modfx_prewarp(fk, fs), n = 1.0f - w;
            a[ch][k] = n == 0.0f ? 0.0f : n / (1.0f + w);
        }
    }
}

/* the chain of one channel over m frames, each stage's coefficient ramping by its dc */
static void chain(struct state *s, int ch, float *x, int m, const float *dc)
{
    float z0 = s->st[ch][0], z1 = s->st[ch][1], z2 = s->st[ch][2], z3 = s->st[ch][3];
    float c0 = s->a[ch][0], c1 = s->a[ch][1], c2 = s->a[ch][2], c3 = s->a[ch][3];
    const float d0 = dc[0], d1 = dc[1], d2 = dc[2], d3 = dc[3], fb = s->d.fb;
    float yp = s->prev[ch];
    for (int i = 0; i < m; i++) {
        c0 = d0 + c0;
        c1 = d1 + c1;
        c2 = d2 + c2;
        c3 = d3 + c3;
        float v = fb * yp + x[i], y;
        y = z0 - v * c0;
        z0 = y * c0 + v;
        v = y;
        y = z1 - v * c1;
        z1 = y * c1 + v;
        v = y;
        y = z2 - v * c2;
        z2 = y * c2 + v;
        v = y;
        y = z3 - v * c3;
        z3 = y * c3 + v;
        x[i] = y;
        yp = y;
    }
    s->st[ch][0] = z0;
    s->st[ch][1] = z1;
    s->st[ch][2] = z2;
    s->st[ch][3] = z3;
    s->a[ch][0] = c0;
    s->a[ch][1] = c1;
    s->a[ch][2] = c2;
    s->a[ch][3] = c3;
    s->prev[ch] = yp;
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
    for (int ch = 0; ch < 2; ch++)
        for (int k = 0; k < NST; k++)
            s->a[ch][k] = 0.95f;
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
        float a[2][NST];
        control(s, fs, a);
        const int m = n - c0 < CTRL ? n - c0 : CTRL;
        for (int ch = 0; ch < 2; ch++) {
            float dc[NST];
            for (int k = 0; k < NST; k++)
                dc[k] = 0.0625f * (a[ch][k] - s->a[ch][k]);
            if (!idle)
                chain(s, ch, &wet[ch][c0], m, dc);
            else
                for (int k = 0; k < NST; k++)
                    for (int i = 0; i < m; i++)
                        s->a[ch][k] = dc[k] + s->a[ch][k];
        }
    }
    if (idle)
        return;
    for (int ch = 0; ch < 2; ch++) {
        if (d->lowcut_on)
            op_block(&d->lowcut, &s->s_lc[ch], wet[ch], n);
        if (d->hicut_on)
            op_block(&d->hicut, &s->s_hc[ch], wet[ch], n);
    }
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

const struct wvfx_desc vibe_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 5),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Vibe",
    .category = "Vibe",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
