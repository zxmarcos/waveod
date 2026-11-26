/*
 * Chorus (MOD › Chorus, waveod's): a light multi-type chorus, docs/chorus.md.
 *
 * Per channel, the wet path is
 *   low cut (one-pole) -> emphasis shelf -> soft clip around a bias (a bucket-brigade's input stage) -> High Cut
 *   (4th-order Butterworth, off at 20 kHz) -> high-pass -> de-emphasis shelf -> one delay line
 * and 1..4 voice pairs read the line, each at the fixed Delay (the right channel at Delay (1 - Offset R)) plus a
 * modulation of up to Width, through a 4-point cubic (Lagrange) interpolator. The fixed and the modulated delay
 * share the line: everything before it is linear or memoryless, so the order does not matter.
 *
 * Modulation: one LFO, sine or triangle, the right channel Phase ahead. With a sine, the voice pairs spread the
 * phase evenly: voice v at v Phase / n on the left and Phase + v Phase / n on the right, and the odd pairs are
 * moved by a slow second LFO. Digital types sweep the delay linearly, smoothed per sample. Analog types follow a
 * bucket-brigade clock: once per 16 frames the delay goes to Delay ((Delay + Width) / Delay)^x, x the LFO,
 * smoothed in between.
 *
 * After the voices: 1 / sqrt(pairs), the type's polarity, a 20 Hz high-pass and a 20 kHz low-pass, the
 * cross-mix of the Space types, then equal-power wet / dry, level and the bypass fade over one block. Bypassed,
 * the line keeps being written (no stale audio when it comes back) and the voices do not run.
 *
 *   Rate 0.1..10 Hz (Sync: a note division)  Depth %  Delay 0.1..50 ms  Mix %  Phase 0..180°  Wave  High Cut
 *   Level dB  Sync  Type (the 23 types below: voices, sweep law, filters, clip, depth and width)
 */
#include "modfx/modfx.h"

#define LINE  4096u                     /* per channel: 50 ms + 40 ms of sweep + the read's margin at 44.1 kHz */
#define MASK  (LINE - 1u)
#define STRIDE (LINE + 4u)
#define LAT   7.0f                      /* frames added to every voice's delay (the read is centred on it) */
#define SUB   16                        /* analog types: frames per delay target */

enum { K_RATE, K_DEPTH, K_DELAY, K_MIX, K_PHASE, K_WAVE, K_HICUT, K_LEVEL, K_SYNC, K_TYPE, NKNOBS };

#define TYPES \
    "Digital Mono\0" "Digital Stereo\0" "Analog Mono\0" "Analog Stereo\0" "Compact\0" "Warm Wide\0" \
    "Eighties\0" "Triangle\0" "Octet\0" "Space 1\0" "Space 2\0" "Space 3\0" "Analog Quad\0" \
    "Analog Octet\0" "Vibrato\0" "Vintage Vibrato\0" "Vintage Chorus\0" "Compact Super\0" "Compact Analog\0" \
    "Small Analog\0" "Compact Bass\0" "Studio Rack\0" "Stereo Rack\0"

static const struct wvfx_param params[] = {
    { "Rate", 0.1f, 10.0f, 0.01f, 1.0f, K_RATE, 0, 0x10 | K_SYNC, 0, 0, "%.2f Hz" },
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, K_DEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Delay", 0.1f, 50.0f, 0.1f, 10.0f, K_DELAY, 0, 0, 0, 0, "%.1f ms" },
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Phase", 0.0f, 180.0f, 1.0f, 90.0f, K_PHASE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Wave", 0.0f, 1.0f, 1.0f, 0.0f, K_WAVE, 0, 0, 0, "Sine\0Triangle\0", 0 },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 20000.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
    { "Type", 0.0f, 22.0f, 1.0f, 1.0f, K_TYPE, 0, 0, 0, TYPES, 0 },
};

/*
 * The types. Frequencies in Hz (0 = that filter off), times in s.
 *   analog: the bucket-brigade sweep; centre: the sweep centred on 1/2 instead of on Depth / 2; pairs: voice
 *   pairs; autodepth: Depth scaled by 0.15 n / Rate (slow rates sweep wider); space: the cross-mix (1..3); flip:
 *   polarity (1 = right inverted); mono: no stereo phase; lowcut, hp: one-poles before / after the High Cut;
 *   smooth: a low-pass on the LFO; shelf: emphasis zero / pole (the de-emphasis is its inverse); lfo2: depth of
 *   the second LFO on the odd pairs; clip: gain into the soft clip; offset: right delay = Delay (1 - offset);
 *   width: the sweep.
 */
struct type {
    uint8_t analog, centre, pairs, autodepth, space, flip, mono;
    float lowcut, hp, smooth, shelf_z, shelf_p, lfo2, clip, offset, width;
};

static const struct type types[] = {
    /*  an ce pr ad sp fl mo  lowcut   hp     smooth  shelf z  shelf p  lfo2  clip   offset width */
    { 0, 0, 1, 1, 0, 0, 1,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.05f, 0.0f, 0.04f },    /* Digital Mono */
    { 0, 0, 1, 1, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.05f, 0.0f, 0.04f },    /* Digital Stereo */
    { 1, 0, 1, 1, 0, 0, 1,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.5f,  0.0f, 0.04f },    /* Analog Mono */
    { 1, 0, 1, 1, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.5f,  0.5f, 0.04f },    /* Analog Stereo */
    { 0, 1, 1, 0, 0, 0, 0,   51.0f, 103.0f, 63.7f,   411.0f,  2337.0f, 0.0f, 0.25f, 0.0f, 0.0024f },  /* Compact */
    { 1, 0, 1, 1, 0, 1, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.25f, 0.5f, 0.04f },    /* Warm Wide */
    { 1, 0, 1, 1, 0, 1, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.05f, 0.0f, 0.039f },   /* Eighties */
    { 0, 0, 1, 1, 0, 1, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.05f, 0.0f, 0.04f },    /* Triangle */
    { 0, 0, 4, 1, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.5f, 0.04f },    /* Octet */
    { 0, 0, 1, 1, 1, 0, 0,   51.0f, 103.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.0f, 0.04f },    /* Space 1 */
    { 0, 0, 1, 1, 2, 0, 0,   51.0f, 103.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.0f, 0.04f },    /* Space 2 */
    { 0, 0, 1, 1, 3, 0, 0,   51.0f, 103.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.0f, 0.04f },    /* Space 3 */
    { 1, 0, 2, 1, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.5f, 0.04f },    /* Analog Quad */
    { 1, 0, 4, 1, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.1f, 0.05f, 0.5f, 0.04f },    /* Analog Octet */
    { 0, 0, 1, 2, 0, 0, 0,    0.0f,   0.0f,  0.0f,     0.0f,     0.0f, 0.0f, 0.05f, 0.0f, 0.04f },    /* Vibrato */
    { 0, 1, 1, 0, 0, 0, 0,    0.0f,  56.0f,  0.0f,  1068.0f,  3379.0f, 0.0f, 0.25f, 0.0f, 0.00385f }, /* Vintage Vibrato */
    { 0, 1, 1, 0, 0, 0, 0,    0.0f,  56.0f,  0.0f,  1068.0f,  3379.0f, 0.0f, 0.25f, 0.0f, 0.00385f }, /* Vintage Chorus */
    { 0, 1, 1, 0, 0, 0, 0,   35.0f,   0.0f,  0.0f,   411.0f,  2337.0f, 0.0f, 0.25f, 0.0f, 0.0028f },  /* Compact Super */
    { 0, 1, 1, 0, 0, 0, 0,   58.1f,   0.0f, 63.7f,   411.0f,  2337.0f, 0.0f, 0.25f, 0.0f, 0.00248f }, /* Compact Analog */
    { 1, 0, 1, 0, 0, 0, 0,    0.0f,   0.0f,  3.39f,  398.0f,  2316.0f, 0.0f, 0.25f, 0.0f, 0.00902f }, /* Small Analog */
    { 0, 1, 1, 0, 0, 0, 0,   51.0f, 188.0f, 63.7f,   411.0f,  2337.0f, 0.0f, 0.25f, 0.0f, 0.0024f },  /* Compact Bass */
    { 1, 1, 1, 0, 0, 0, 0,    0.0f,   0.0f,  0.0f,  1518.0f,  8537.0f, 0.0f, 0.25f, 0.0f, 0.0072f },  /* Studio Rack */
    { 0, 1, 1, 0, 0, 1, 0,    0.0f,   0.0f,  0.9f,  1389.0f, 15200.0f, 0.0f, 0.25f, 0.0f, 0.0105f },  /* Stereo Rack */
};
#define NTYPES ((int)(sizeof types / sizeof types[0]))

/* what the knobs make; rebuilt when they change */
struct design {
    int pairs, analog, centre, tri, space, lowcut_on, hp_on, smooth_on, shelf_on, hicut_on;
    struct op lowcut, shelf, hp, deshelf, smooth;
    struct bq lp1, lp2;
    float inc, inc2;                    /* LFO increments, rad per frame (LFO 1, the second LFO) */
    float ofs;                          /* triangle: the right channel's phase, rad */
    float h, o;                         /* sweep x = lfo h + o */
    float span;                         /* digital: the sweep in frames; analog: Delay in frames */
    float l2r;                          /* analog: log2 ((Delay + Width) / Delay) */
    float fix[2];                       /* the fixed delay per channel + LAT, frames */
    float rot[4][2][2];                 /* sine: voice v, channel: lfo_l rot[0] - lfo_r rot[1] */
    float lfo2_d;
    float clip, rclip;
    float wet[2];                       /* 1 / sqrt(pairs) with the polarity */
    float g1, g2;                       /* Space: voices into the other side x g1 (through 800 Hz high-pass), dry x g2 */
    float gw, gd;                       /* the mix with the level */
};

struct state {
    float *line[2];
    uint32_t w;
    float knobs[NKNOBS];
    uint32_t beat;
    int designed;
    struct design d;
    float ph, ph2;                      /* LFO phases, rad */
    float sm[2];                        /* LFO smoothing */
    float vs[4][2], vt[4][2];           /* per voice: the smoothed delay offset, the analog target */
    float s_low[2], s_shelf[2], s_lp1[2][2], s_lp2[2][2], s_hp[2], s_desh[2];
    float s_hp20[2], s_lp20[2], s_space[2][2];
    float gw, gd;                       /* where the last block's output gains ended: wet, dry */
};

/* ---------------------------------------------------------------- design */

static void design(struct state *s, const float *k, uint32_t beat_ms, float fs)
{
    struct design *d = &s->d;
    const struct type *t = &types[modfx_option(k[K_TYPE], NTYPES)];
    float rate = modfx_rate(k[K_RATE], k[K_SYNC] >= 0.5f, beat_ms, 0.1f, 10.0f);
    float depth = wvfx_clamp(k[K_DEPTH], 0.0f, 100.0f) * 0.01f;
    float delay = wvfx_clamp(k[K_DELAY], 0.1f, 50.0f) * 0.001f;
    float mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f;
    float phase = t->mono ? 0.0f : wvfx_clamp(k[K_PHASE], 0.0f, 180.0f) * (1.0f / 360.0f);   /* turns */
    float hicut = wvfx_clamp(k[K_HICUT], 200.0f, 20000.0f);

    d->tri = k[K_WAVE] >= 0.5f;
    d->analog = t->analog;
    d->centre = t->centre;
    d->pairs = d->tri ? 1 : t->pairs;
    d->space = t->space;
    d->inc = rate * MODFX_TWO_PI / fs;
    d->inc2 = 1.0f * MODFX_TWO_PI / fs;                  /* the second LFO: 1 Hz */
    d->ofs = MODFX_TWO_PI * phase;

    if (t->autodepth) {
        depth = depth / rate * (0.15f * (float)t->autodepth);
        if (t->analog)
            depth += depth;
        if (depth > 1.0f)
            depth = 1.0f;
    }
    d->h = 0.5f * depth;
    d->o = t->centre ? 0.5f : d->h;
    if (t->analog) {
        d->span = delay * fs;
        d->l2r = wvfx_log2((delay + t->width) / delay);
    } else {
        d->span = t->width * fs;
        d->l2r = 0.0f;
    }
    float dfs = delay * fs;
    d->fix[0] = (float)(int)dfs + LAT;
    d->fix[1] = (float)(int)(dfs * (1.0f - t->offset)) + LAT;

    d->lowcut_on = t->lowcut > 0.0f;
    if (d->lowcut_on)
        d->lowcut = op_hp(t->lowcut, fs);
    d->hp_on = t->hp > 0.0f;
    if (d->hp_on)
        d->hp = op_hp(t->hp, fs);
    d->shelf_on = t->shelf_z > 0.0f;
    if (d->shelf_on) {
        d->shelf = op_shelf(t->shelf_z, t->shelf_p, fs);
        d->deshelf = op_shelf(t->shelf_p, t->shelf_z, fs);
    }
    d->smooth_on = t->smooth > 0.0f;
    if (d->smooth_on)
        d->smooth = op_lp(t->smooth, fs);
    d->hicut_on = hicut < 20000.0f;
    if (d->hicut_on) {
        d->lp1 = bq_lp(hicut, 0.54118411f, fs);
        d->lp2 = bq_lp(hicut, 1.30656296f, fs);
    }

    static const float level_of[4] = { 1.0f, 0.70710678f, 0.57735027f, 0.5f };
    float step = phase / (float)d->pairs;
    for (int v = 0; v < d->pairs; v++) {
        float a = step * (float)v;
        modfx_sincos(a, &d->rot[v][0][1], &d->rot[v][0][0]);
        modfx_sincos(a + phase, &d->rot[v][1][1], &d->rot[v][1][0]);
    }
    d->lfo2_d = t->lfo2;
    d->clip = t->clip;
    d->rclip = 1.0f / t->clip;
    d->wet[0] = level_of[d->pairs - 1];
    d->wet[1] = t->flip ? -d->wet[0] : d->wet[0];
    static const float space_g1[4] = { 0.0f, -0.3f, -0.3f, -1.0f }, space_g2[4] = { 0.0f, 0.0f, 0.2f, 0.2f };
    d->g1 = space_g1[t->space];
    d->g2 = space_g2[t->space];

    float sw, cw;
    modfx_mix(mix, &sw, &cw);
    float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = sw * level;
    d->gd = cw * level;
}

/* the 800 Hz high-pass of the Space types (RBJ with alpha = w / 2Q, Q 0.5, at 44.1 kHz) */
static const struct bq space_hp = { 0.89476919f, -1.78953838f, 0.89476919f, 1.78371358f, -0.79536325f };
/* 20 Hz high-pass and 20 kHz low-pass, one-poles at 44.1 kHz */
static const struct op hp20 = { 0.9985773f, -0.9985773f, 0.99715453f };
static const struct op lp20k = { 0.87177509f, 0.87177509f, -0.74355012f };

/* ---------------------------------------------------------------- block */

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
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    s->line[0] = buffer;
    s->line[1] = (float *)buffer + STRIDE;
    for (unsigned i = 0; i < 2 * STRIDE; i++)
        s->line[0][i] = 0.0f;
    s->w = 0;
    s->designed = 0;
    changed(s, knobs, 500);
    design(s, knobs, 500, MODFX_FS);
    s->gw = 0.0f;                                    /* from the dry signal: on, the first block fades in */
    s->gd = 1.0f;
    return 0;
}

/* the wet path before the line, and the write */
static void pre(struct state *s, const float *io, int n)
{
    const struct design *d = &s->d;
    const float g = d->clip, rg = d->rclip;
    for (int ch = 0; ch < 2; ch++) {
        float x[WVFX_MAX_FRAMES];
        for (int i = 0; i < n; i++)
            x[i] = io[2 * i + ch];
        if (d->lowcut_on)
            op_block(&d->lowcut, &s->s_low[ch], x, n);
        if (d->shelf_on)
            op_block(&d->shelf, &s->s_shelf[ch], x, n);
        for (int i = 0; i < n; i++)
            x[i] = modfx_bbd_clip(x[i], g, rg);
        if (d->hicut_on) {
            bq_block(&d->lp1, s->s_lp1[ch], x, n);
            bq_block(&d->lp2, s->s_lp2[ch], x, n);
        }
        if (d->hp_on)
            op_block(&d->hp, &s->s_hp[ch], x, n);
        if (d->shelf_on)
            op_block(&d->deshelf, &s->s_desh[ch], x, n);
        float *line = s->line[ch];
        const uint32_t w = s->w;
        for (int i = 0; i < n; i++)
            line[(w + (uint32_t)i) & MASK] = x[i];
        modfx_mirror(line, LINE);
    }
}

/* the LFO over the block: a and b (the right channel's for a triangle, the quadrature for a sine), lfo2 */
static inline void lfo(struct state *s, int n, float *a, float *b, float *l2)
{
    const struct design *d = &s->d;
    float ph = s->ph;
    if (d->tri) {
        for (int i = 0; i < n; i++) {
            ph = modfx_advance(ph, d->inc);
            float p = ph * (1.0f / MODFX_TWO_PI);
            a[i] = modfx_tri(p);
            if (d->ofs > 0.0f) {
                float q = d->ofs + ph;
                if (q >= MODFX_TWO_PI)
                    q = (q - MODFX_TWO_PI) + 1.7484555e-7f;
                q *= 1.0f / MODFX_TWO_PI;
                b[i] = modfx_tri(q);
            } else {
                b[i] = a[i];
            }
        }
    } else {
        ph = modfx_rotate(ph, d->inc, n, a, b);
    }
    s->ph = ph;
    if (d->smooth_on)
        for (int i = 0; i < n; i++) {
            a[i] = op_run(&d->smooth, &s->sm[0], a[i]);
            b[i] = op_run(&d->smooth, &s->sm[1], b[i]);
        }
    if (d->pairs > 1) {
        float c2[WVFX_MAX_FRAMES];
        s->ph2 = modfx_rotate(s->ph2, d->inc2, n, l2, c2);
        const float dd = d->lfo2_d, o2 = 1.0f - dd;
        for (int i = 0; i < n; i++)
            l2[i] = o2 + l2[i] * dd;
    }
}

/* one voice of one channel over the block, added into acc; specialised by the constant flags */
struct voice {
    const float *line;
    uint32_t w;
    float fix;
    const float *a, *b, *l2;            /* triangle: a = the channel's own LFO */
    float r0, r1, h, o, span, l2r, k, r;
    float sm, tg;
};

static inline __attribute__((always_inline)) void voice_run(struct voice *vc, float *acc, int n, const int analog,
                                                            const int tri, const int odd)
{
    const float *line = vc->line, *a = vc->a, *b = vc->b, *l2 = vc->l2;
    const uint32_t w = vc->w;
    const float fix = vc->fix, r0 = vc->r0, r1 = vc->r1, h = vc->h, o = vc->o, span = vc->span, k = vc->k, r = vc->r;
    float sm = vc->sm, tg = vc->tg;
    for (int i = 0; i < n; i++) {
        float lv = tri ? a[i] : a[i] * r0 - b[i] * r1;
        if (odd)
            lv = lv * l2[i];
        if (analog) {
            if ((i & (SUB - 1)) == 0)
                tg = (span * modfx_exp2((lv * h + o) * vc->l2r) - span) * k;
            sm = tg + sm * r;
        } else {
            sm = ((lv * h + o) * span) * k + sm * r;
        }
        acc[i] += modfx_tap(line, MASK, w + (uint32_t)i, fix + sm);
    }
    vc->sm = sm;
    vc->tg = tg;
}

static void voices(struct state *s, int n, const float *a, const float *b, const float *l2, float wet[2][WVFX_MAX_FRAMES])
{
    const struct design *d = &s->d;
    const float k = 0.0108795166f, r = 0.989120483f;      /* per-frame smoothing, ~2 ms */
    const float h = d->h, o = d->o, span = d->span, l2r = d->l2r;
    const uint32_t w = s->w;
    for (int ch = 0; ch < 2; ch++) {
        const float *line = s->line[ch];
        const float fix = d->fix[ch];
        float *acc = wet[ch];
        for (int i = 0; i < n; i++)
            acc[i] = 0.0f;
        for (int v = 0; v < d->pairs; v++) {
            struct voice vc = {
                line, w, fix, d->tri ? (ch ? b : a) : a, b, l2, d->rot[v][ch][0], d->rot[v][ch][1],
                h, o, span, l2r, k, r, s->vs[v][ch], s->vt[v][ch],
            };
            const int odd = v & 1;
            if (d->analog) {
                if (d->tri)
                    voice_run(&vc, acc, n, 1, 1, 0);
                else if (odd)
                    voice_run(&vc, acc, n, 1, 0, 1);
                else
                    voice_run(&vc, acc, n, 1, 0, 0);
            } else {
                if (d->tri)
                    voice_run(&vc, acc, n, 0, 1, 0);
                else if (odd)
                    voice_run(&vc, acc, n, 0, 0, 1);
                else
                    voice_run(&vc, acc, n, 0, 0, 0);
            }
            s->vs[v][ch] = vc.sm;
            s->vt[v][ch] = vc.tg;
        }
    }
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const int n = io->frames;
    if (changed(s, io->knobs, io->beat_ms))
        design(s, io->knobs, io->beat_ms, MODFX_FS);
    const struct design *d = &s->d;
    float *x = io->io;
    /* the line is written ahead: frame i of this block lands at s->w + i */
    pre(s, x, n);
    /* output gains: wet and dry of the mix when on, the dry alone when bypassed */
    const float tw = io->enabled ? d->gw : 0.0f, td = io->enabled ? d->gd : 1.0f;
    if (!io->enabled && s->gw == 0.0f && s->gd == 1.0f) {
        for (int i = 0; i < n; i++) {                /* the LFOs keep running */
            s->ph = modfx_advance(s->ph, d->inc);
            s->ph2 = modfx_advance(s->ph2, d->inc2);
        }
        s->w = (s->w + (uint32_t)n) & MASK;
        return;
    }
    float a[WVFX_MAX_FRAMES], b[WVFX_MAX_FRAMES], l2[WVFX_MAX_FRAMES], wet[2][WVFX_MAX_FRAMES];
    lfo(s, n, a, b, l2);
    voices(s, n, a, b, l2, wet);
    s->w = (s->w + (uint32_t)n) & MASK;

    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n);
    for (int ch = 0; ch < 2; ch++) {
        const float g = d->wet[ch];
        for (int i = 0; i < n; i++)
            wet[ch][i] *= g;
        op_block(&hp20, &s->s_hp20[ch], wet[ch], n);
        op_block(&lp20k, &s->s_lp20[ch], wet[ch], n);
    }
    if (d->space) {                                  /* each side's voices, high-passed, into the other side */
        float p[2][WVFX_MAX_FRAMES];
        for (int ch = 0; ch < 2; ch++) {
            for (int i = 0; i < n; i++)
                p[ch][i] = d->g1 * wet[ch][i];
            bq_block(&space_hp, s->s_space[ch], p[ch], n);
        }
        const float g2 = d->g2;
        for (int i = 0; i < n; i++) {
            float l = (p[0][i] + wet[1][i]) + g2 * x[2 * i], r = (p[1][i] + wet[0][i]) + g2 * x[2 * i + 1];
            wet[0][i] = l;
            wet[1][i] = r;
        }
    }
    float gw = s->gw, gd = s->gd;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        x[2 * i] = gw * wet[0][i] + gd * x[2 * i];
        x[2 * i + 1] = gw * wet[1][i] + gd * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
}

const struct wvfx_desc chorus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 1),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Chorus",
    .category = "Chorus",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 2 * STRIDE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
