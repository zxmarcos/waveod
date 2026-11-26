/*
 * Tremolo (MOD › Tremolo, waveod's): a light VCA tremolo and panner, docs/modfx.md.
 *
 * The LFO (sine, triangle or square; the right channel Phase ahead) through a 20 Hz one-pole, then
 *   Tremolo: gain = (1 - d/2) + (d/2) lfo on each side;
 *   Panner:  the same on the left and (1 - d/2) - (d/2) lfo on the right, so the sound moves between them;
 * d = Depth, faded in when the block starts (~30 ms). Then level and balance, ramped over one block.
 * Bypassed, the output is the input bit for bit and the LFO waits at its start (a quarter turn), so it always
 * comes back at the same point.
 *
 *   Rate 0.2..20 Hz (Sync)  Depth %  Wave  Phase 0..180°  Level dB  Balance  Sync  Mode
 */
#include "modfx/modfx.h"

#define CTRL 16

enum { K_RATE, K_DEPTH, K_WAVE, K_PHASE, K_LEVEL, K_BALANCE, K_SYNC, K_MODE, NKNOBS };

static const struct wvfx_param params[] = {
    { "Rate", 0.2f, 20.0f, 0.01f, 5.0f, K_RATE, 0, 0x10 | K_SYNC, 0, 0, "%.2f Hz" },
    { "Depth", 0.0f, 100.0f, 1.0f, 80.0f, K_DEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Wave", 0.0f, 2.0f, 1.0f, 1.0f, K_WAVE, 0, 0, 0, "Sine\0Triangle\0Square\0", 0 },
    { "Phase", 0.0f, 180.0f, 1.0f, 0.0f, K_PHASE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Balance", -100.0f, 100.0f, 1.0f, 0.0f, K_BALANCE, 0x75, 0, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
    { "Mode", 0.0f, 1.0f, 1.0f, 0.0f, K_MODE, 0, 0, 0, "Tremolo\0Panner\0", 0 },
};

static const struct op lfo_lp = { 0x1.74f5f6p-10f, 0x1.74f5f6p-10f, 0x1.fe8b0ap-1f };   /* 20 Hz at 44.1 kHz */
static const float fade_r = 0x1.fa6ep-1f, fade_k = 0x1.648p-7f;                        /* per 16 frames, ~30 ms */
#define START (0.5f * MODFX_PI)

struct state {
    float knobs[NKNOBS];
    uint32_t beat;
    int designed;
    int wave, pan;
    float inc, ofs, depth, gl, gr;      /* gl, gr: level with the balance */
    float ph, lp[2], fade;
    float ol, or_, ot;                  /* where the last block's gains ended: left, right, through */
};

static void design(struct state *s, const float *k, uint32_t beat_ms)
{
    const float rate = modfx_rate(k[K_RATE], k[K_SYNC] >= 0.5f, beat_ms, 0.2f, 20.0f);
    s->wave = modfx_option(k[K_WAVE], 3);
    s->pan = k[K_MODE] >= 0.5f;
    s->inc = rate * MODFX_TWO_PI / MODFX_FS;
    s->ofs = wvfx_clamp(k[K_PHASE], 0.0f, 180.0f) * (MODFX_PI / 180.0f);
    s->depth = wvfx_clamp(k[K_DEPTH], 0.0f, 100.0f) * 0.01f;
    float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    float bal = wvfx_clamp(k[K_BALANCE], -100.0f, 100.0f) * 0.01f;
    if (bal > 0.9999f)
        bal = 0.9999f;
    s->gl = level * (1.0f - (bal > 0.0f ? bal : 0.0f));
    s->gr = level * (1.0f + (bal < 0.0f ? bal : 0.0f));
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
    changed(s, knobs, 500);
    design(s, knobs, 500);
    s->ph = 0.0f;
    s->ot = 1.0f;
    return 0;
}

/* the LFO over n frames into a (left) and b (right) */
static void lfo(struct state *s, int n, float *a, float *b)
{
    float ph = s->ph;
    if (s->wave == 0) {
        float c[WVFX_MAX_FRAMES];
        float sn, cs, rs, rc;
        ph = modfx_rotate(ph, s->inc, n, a, c);         /* -cos of the phase */
        if (s->ofs > 0.0f) {                            /* the right channel: the same rotation, ofs ahead */
            modfx_sincos(s->ofs * (1.0f / MODFX_TWO_PI), &rs, &rc);
            for (int i = 0; i < n; i++) {
                sn = c[i];                              /* sin, -cos -> -cos (phase + ofs) */
                cs = -a[i];
                b[i] = -(cs * rc - sn * rs);
            }
        } else {
            for (int i = 0; i < n; i++)
                b[i] = a[i];
        }
    } else {
        for (int i = 0; i < n; i++) {
            ph = modfx_advance(ph, s->inc);
            float l = modfx_tri(ph * (1.0f / MODFX_TWO_PI)), r = l;
            if (s->ofs > 0.0f) {
                float q = s->ofs + ph;
                if (q >= MODFX_TWO_PI)
                    q = (q - MODFX_TWO_PI) + 1.7484555e-7f;
                r = modfx_tri(q * (1.0f / MODFX_TWO_PI));
            }
            if (s->wave == 2) {
                l = l > 0.0f ? 1.0f : -1.0f;
                r = r > 0.0f ? 1.0f : -1.0f;
            }
            a[i] = l;
            b[i] = r;
        }
    }
    s->ph = ph;
    op_block(&lfo_lp, &s->lp[0], a, n);
    op_block(&lfo_lp, &s->lp[1], b, n);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    if (changed(s, io->knobs, io->beat_ms))
        design(s, io->knobs, io->beat_ms);
    float *x = io->io;
    for (int c0 = 0; c0 < n; c0 += CTRL)                /* the depth fades in per 16 frames */
        s->fade = fade_r * s->fade + fade_k;
    const float tl = io->enabled ? s->gl : 0.0f, tr = io->enabled ? s->gr : 0.0f, tt = io->enabled ? 0.0f : 1.0f;
    if (!io->enabled && s->ol == 0.0f && s->or_ == 0.0f && s->ot == 1.0f) {
        s->ph = START;                                  /* waits at its start */
        return;
    }
    float a[WVFX_MAX_FRAMES], b[WVFX_MAX_FRAMES];
    lfo(s, n, a, b);
    const float d = s->fade * s->depth, k = 0.5f * d, k0 = 1.0f - k, kr = s->pan ? -k : k;
    const float dl = wvfx_ramp_step(s->ol, tl, n), dr = wvfx_ramp_step(s->or_, tr, n), dt = wvfx_ramp_step(s->ot, tt, n);
    float gl = s->ol, gr = s->or_, gt = s->ot;
    for (int i = 0; i < n; i++) {
        gl += dl;
        gr += dr;
        gt += dt;
        const float l = x[2 * i], r = x[2 * i + 1];
        x[2 * i] = (l * (k0 + k * a[i])) * gl + l * gt;
        x[2 * i + 1] = (r * (k0 + kr * b[i])) * gr + r * gt;
    }
    s->ol = tl;
    s->or_ = tr;
    s->ot = tt;
}

const struct wvfx_desc tremolo_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 6),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Tremolo",
    .category = "Tremolo",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
