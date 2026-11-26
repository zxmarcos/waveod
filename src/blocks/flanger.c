/*
 * Flanger (MOD › Flanger, waveod's): a light multi-type flanger, docs/modfx.md.
 *
 * Control rate, every 16 frames:
 *   LFO (sine, triangle, square or "exp", optionally quantised to steps, amplitude 1/2; the right channel Phase
 *   ahead) -> sweep x = (1 - Depth) Manual + Depth (lfo + 1/2) -> a one-pole smoother whose cutoff follows x ->
 *   1 - y on the inverted types -> the sweep law (linear, exponential (M^y - 1) / (M - 1) with M = Width / Delay x
 *   Shape, or square) -> the delay target Delay + (Width' - Delay) y, Width' = min(Delay + (Width - Delay) s,
 *   20 ms), s = 1 or Auto Depth 1/4, 1/2, 1 per Hz of Rate.
 * Audio, in sub-blocks of 8 frames (the loop is never shorter than 15 frames):
 *   the delay smoothed per frame -> a 4-point cubic read -> Low Cut, High Cut (1st..5th order) in the loop ->
 *   polarity = wet; wet x Feedback -> the type's feedback filter -> + input -> soft clip (the driven types) ->
 *   the line. The bucket-brigade types low-pass the wet after the feedback tap.
 * Then Thru-Zero (the dry from a line of its own, delayed by the sweep at the LFO's centre), the output Low Cut,
 * Spread, and the equal-power mix with the level, ramped over one block. Bypassed, the input keeps filling the
 * lines and the control rate keeps running; the output is the input bit for bit.
 *
 *   Rate 0.05..10 Hz (Sync)  Depth %  Feedback -99..99 %  Delay 0.34..2 ms  Width 2..20 ms  Mix %  Phase 0..180°
 *   Wave  High Cut  Level dB  Sync  Type (32)
 */
#include "modfx/modfx.h"

#define LINE   1024u                    /* 20 ms of sweep at 44.1 kHz, and the read's margin */
#define MASK   (LINE - 1u)
#define STRIDE (LINE + 4u)
#define CTRL   16                       /* frames per control step */
#define SUB    8                        /* frames per pass of the loop */
#define MIN_D  15.0f                    /* the shortest delay, frames */

enum { K_RATE, K_DEPTH, K_FB, K_DELAY, K_WIDTH, K_MIX, K_PHASE, K_WAVE, K_HICUT, K_LEVEL, K_SYNC, K_TYPE, NKNOBS };

#define TYPES \
    "Digital Mono\0" "Digital Stereo\0" "Analog Mono\0" "Analog Stereo\0" "Thru-Zero\0" "Wide Jet\0" \
    "Zero Cancel\0" "Pop\0" "Vintage Box\0" "Compact BBD\0" "Electric\0" "Deluxe\0" "Studio BBD\0" \
    "Fixed Zero\0" "Step\0" "Clean Wide\0" "Square\0" "Cancel\0" "Slow Jet\0" "Deep BBD\0" "Harmonic\0" \
    "Half Sweep\0" "Exp Sweep\0" "Melodic Steps\0" "Eighties Rack\0" "Stereo Glide\0" "Subtle\0" "Space Jet\0" \
    "Wide Trip\0" "Tube Sweep\0" "Talk Sweep\0" "Fixed Cancel\0"

static const struct wvfx_param params[] = {
    { "Rate", 0.05f, 10.0f, 0.01f, 0.5f, K_RATE, 0, 0x10 | K_SYNC, 0, 0, "%.2f Hz" },
    { "Depth", 0.0f, 100.0f, 1.0f, 70.0f, K_DEPTH, 0, 0, 0, 0, "%.0f%%" },
    { "Feedback", -99.0f, 99.0f, 1.0f, 60.0f, K_FB, 0, 0, 0, 0, "%.0f%%" },
    { "Delay", 0.34f, 2.0f, 0.01f, 1.0f, K_DELAY, 0, 0, 0, 0, "%.2f ms" },
    { "Width", 2.0f, 20.0f, 0.1f, 10.0f, K_WIDTH, 0, 0, 0, 0, "%.1f ms" },
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Phase", 0.0f, 180.0f, 1.0f, 90.0f, K_PHASE, WVFX_FMT_DEGREES, 0, 0, 0, 0 },
    { "Wave", 0.0f, 1.0f, 1.0f, 0.0f, K_WAVE, 0, 0, 0, "Sine\0Triangle\0", 0 },
    { "High Cut", 200.0f, 20000.0f, 10.0f, 20000.0f, K_HICUT, 0, 0, 0, 0, "%.0f Hz" },
    { "Level", -60.0f, 20.0f, 0.5f, 0.0f, K_LEVEL, 0, 0, 0, 0, "%.1f dB" },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, K_SYNC, 0, 0x20, 0, "Off\0On\0", 0 },
    { "Type", 0.0f, 31.0f, 1.0f, 1.0f, K_TYPE, 0, 0, 0, TYPES, 0 },
};

/* a filter of a type: kind, corner, Q */
enum { F_OFF, F_LP1, F_HP1, F_LP2, F_HP2 };
struct filt { uint8_t kind; float f, q; };

/*
 * The types. law: 0 linear, 1 exponential, 2 square; flip: 0 none, 3 both sides inverted; zero: Thru-Zero;
 * auto: Auto Depth 0 off, 1..3 = 1/4, 1/2, 1 per Hz; steps: LFO levels (1 = smooth); inv: the sweep inverted;
 * form: 0 knob (sine / triangle), 2 square, 7 exp; slope: High Cut order - 1; manual: where Depth 0 sits;
 * smf, smk: the sweep smoother's cutoff (Hz at the top of the sweep, the fraction at the bottom); shape: the
 * exponential law's curve; clip: gain into the soft clip in the loop (<= 0.005 off); spread; fbk: feedback scale;
 * olc: output low cut (Hz, 0 off); lowcut / fbf / bbd: in the loop, on the feedback, on the wet.
 */
struct type {
    uint8_t law, flip, zero, autod, steps, inv, form, slope;
    float manual, smf, smk, shape, clip, spread, fbk, olc;
    struct filt lowcut, fbf, bbd;
};

#define NOF  { F_OFF, 0.0f, 0.0f }
#define HP20 { F_HP1, 20.0f, 1.0f }
#define BBD10 { F_LP2, 10000.0f, 0.7071070f }
static const struct type types[] = {
    /* law fl ze au st in fo sl  manual   smf     smk   shape    clip     spread fbk  olc     lowcut                 fbf                        bbd */
    { 1, 0, 0, 0, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Digital Mono */
    { 1, 0, 0, 0, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Digital Stereo */
    { 1, 0, 0, 1, 1, 0, 0, 3, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Analog Mono */
    { 1, 0, 0, 1, 1, 0, 0, 3, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Analog Stereo */
    { 0, 0, 1, 1, 1, 0, 0, 3, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Thru-Zero */
    { 1, 0, 0, 1, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Wide Jet */
    { 0, 3, 1, 1, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Zero Cancel */
    { 0, 0, 0, 1, 1, 0, 0, 3, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Pop */
    { 0, 0, 0, 1, 1, 1, 1, 3, 0.5f,    3.40004f, 0.25f, 16.399084f, 0.350017f, 1.0f, 0.7f, 0.0f, { F_HP1, 45.000114f, 0.707107f }, { F_HP2, 92.0f, 0.707107f }, BBD10 },  /* Vintage Box */
    { 0, 0, 0, 1, 1, 1, 1, 4, 0.5f,    1.4f,   0.62f, 1.5701056f, 0.080003f, 1.0f, 1.0f, 0.0f, { F_HP2, 69.997658f, 0.707107f }, { F_HP2, 170.0f, 0.707107f }, { F_LP2, 11000.0f, 0.707107f } },  /* Compact BBD */
    { 0, 0, 0, 0, 1, 0, 1, 0, 0.0f,    4.799973f, 1.0f, 1.0f,  0.080003f, 1.0f,  1.0f, 200.0f, NOF,                   { F_HP1, 720.0f, 1.0f },   NOF },  /* Electric */
    { 0, 0, 0, 0, 1, 0, 1, 0, 0.0f,    4.799973f, 1.0f, 1.0f,  0.080003f, 1.0f,  1.0f, 0.0f,   NOF,                   NOF,                       NOF },  /* Deluxe */
    { 0, 0, 0, 2, 1, 1, 1, 1, 0.5f,    7.199969f, 1.0f, 1.0f,  0.080003f, 1.0f,  1.0f, 0.0f,   { F_HP1, 32.999313f, 0.707107f }, HP20,          { F_LP1, 16000.0f, 1.0f } },  /* Studio BBD */
    { 1, 0, 1, 1, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Fixed Zero */
    { 0, 0, 0, 0, 10, 0, 1, 3, 0.5f,   50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   { F_HP1, 45.000114f, 0.707107f }, { F_HP2, 92.0f, 0.707107f }, BBD10 },  /* Step */
    { 1, 0, 0, 1, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Clean Wide */
    { 0, 0, 0, 0, 1, 0, 2, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Square */
    { 0, 3, 1, 0, 1, 0, 1, 3, 0.0f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Cancel */
    { 1, 0, 0, 1, 1, 0, 0, 0, 0.099002f, 24.999739f, 1.0f, 1.0f, 0.005f, 1.0f, 1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Slow Jet */
    { 0, 0, 0, 1, 1, 1, 1, 4, 0.659993f, 1.4f, 0.62f, 1.5701056f, 0.080003f, 1.0f, 1.0f, 0.0f, { F_HP2, 69.997658f, 0.707107f }, { F_HP2, 170.0f, 0.707107f }, { F_LP2, 11000.0f, 0.707107f } },  /* Deep BBD */
    { 2, 0, 0, 1, 1, 0, 1, 3, 0.0f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Harmonic */
    { 1, 0, 0, 2, 1, 0, 1, 3, 0.0f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Half Sweep */
    { 1, 3, 0, 0, 1, 0, 7, 0, 0.0f,    15.809723f, 1.0f, 1.0f, 0.005f, 1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Exp Sweep */
    { 0, 0, 0, 1, 9, 0, 1, 0, 0.0f,    11.03006f, 1.0f, 1.0f,  0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Melodic Steps */
    { 1, 0, 0, 1, 1, 0, 0, 0, 0.099002f, 24.999739f, 1.0f, 1.0f, 0.005f, 1.0f, 1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Eighties Rack */
    { 0, 0, 0, 1, 1, 0, 1, 0, 0.146001f, 20.00042f, 1.0f, 1.0f, 0.005f, 1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Stereo Glide */
    { 1, 0, 0, 0, 1, 0, 0, 0, 0.024003f, 24.999739f, 1.0f, 1.0f, 0.005f, 1.0f, 1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Subtle */
    { 2, 3, 1, 1, 1, 0, 1, 3, 0.0f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Space Jet */
    { 1, 3, 0, 0, 1, 0, 0, 0, 0.0f,    24.999739f, 1.0f, 0.01f, 0.005f, 2.0f,  1.0f, 0.0f,   { F_HP1, 48.801365f, 0.707107f }, HP20,          NOF },  /* Wide Trip */
    { 1, 3, 0, 1, 1, 0, 0, 3, 0.200003f, 50.0f, 1.0f, 1.0f,   0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      BBD10 },/* Tube Sweep */
    { 0, 0, 0, 1, 1, 0, 0, 0, 0.0f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 92.086777f, NOF,               HP20,                      NOF },  /* Talk Sweep */
    { 1, 3, 1, 1, 1, 0, 0, 0, 0.5f,    50.0f,  1.0f, 1.0f,    0.005f,  1.0f,  1.0f, 0.0f,   NOF,                   HP20,                      NOF },  /* Fixed Cancel */
};
#define NTYPES ((int)(sizeof types / sizeof types[0]))

static struct bq filt_design(const struct filt *f, float fs)
{
    switch (f->kind) {
    case F_LP1: return bq_of_op(op_lp(f->f, fs));
    case F_HP1: return bq_of_op(op_hp(f->f, fs));
    case F_LP2: return bq_rbj(0, f->f, f->q, fs);
    case F_HP2: return bq_rbj(1, f->f, f->q, fs);
    default: return bq_through;
    }
}

/* what the knobs make; rebuilt when they change */
struct design {
    int form, law, inv, zero, autod, steps, lowcut_on, bbd_on, olc_on, nhc, spread_on;
    float inc;                          /* LFO, rad per control step */
    float ofs;                          /* the right channel's LFO phase, rad */
    float depth, manual, fk, fd, shape;
    float delay, width;                 /* s */
    float fbg, clip, rclip, spread;
    float rev[2];
    struct bq lowcut, fbf, bbd, hc[3];
    struct op olc_wet, olc_dry;
    float gw, gd;
};

struct state {
    float *line[2], *dry[2];
    uint32_t w;
    float knobs[NKNOBS];
    uint32_t beat;
    int designed;
    struct design d;
    float ph;
    float sm[2];                        /* sweep smoothers */
    float ds[2];                        /* delay smoothers, s */
    float s_low[2][2], s_hc[3][2][2], s_fbf[2][2], s_bbd[2][2], s_olcw[2], s_olcd[2];
    float gw, gd, gt;                   /* where the last block's gains ended: wet, processed dry, input */
};

static const float dk = 0x1.abeep-9f, dr = 0x1.fe5412p-1f;   /* delay smoother, ~7 ms */

static void design(struct state *s, const float *k, uint32_t beat_ms, float fs)
{
    struct design *d = &s->d;
    const struct type *t = &types[modfx_option(k[K_TYPE], NTYPES)];
    float rate = modfx_rate(k[K_RATE], k[K_SYNC] >= 0.5f, beat_ms, 0.05f, 10.0f);
    d->form = t->form == 2 || t->form == 7 ? t->form : k[K_WAVE] >= 0.5f;
    d->law = t->law;
    d->inv = t->inv;
    d->zero = t->zero;
    d->autod = t->autod;
    d->steps = t->steps;
    d->inc = rate * MODFX_TWO_PI / fs * (float)CTRL;
    d->ofs = wvfx_clamp(k[K_PHASE], 0.0f, 180.0f) * (MODFX_PI / 180.0f);
    d->depth = wvfx_clamp(k[K_DEPTH], 0.0f, 100.0f) * 0.01f;
    d->manual = t->manual;
    d->fk = t->smf * t->smk;
    d->fd = t->smf - d->fk;
    d->shape = t->shape;
    d->delay = wvfx_clamp(k[K_DELAY], 0.34f, 2.0f) * 0.001f;
    d->width = wvfx_clamp(k[K_WIDTH], 2.0f, 20.0f) * 0.001f;
    d->fbg = t->fbk * (wvfx_clamp(k[K_FB], -99.5f, 99.5f) * 0.01f);
    d->clip = t->clip > 0.005f ? t->clip : 0.0f;
    d->rclip = d->clip > 0.0f ? 1.0f / d->clip : 0.0f;
    d->spread_on = t->spread < 0.999f || t->spread > 1.001f;
    d->spread = t->spread;
    d->rev[0] = t->flip & 2 ? -1.0f : 1.0f;
    d->rev[1] = t->flip & 1 ? -1.0f : 1.0f;
    d->lowcut_on = t->lowcut.kind != F_OFF;
    d->lowcut = filt_design(&t->lowcut, fs);
    d->fbf = filt_design(&t->fbf, fs);
    d->bbd_on = t->bbd.kind != F_OFF;
    d->bbd = filt_design(&t->bbd, fs);
    d->olc_on = t->olc > 0.0f;
    if (d->olc_on) {
        float kk = modfx_prewarp(t->olc, fs), c = (1.0f - kk) / (1.0f + kk);
        d->olc_wet = op_hp(t->olc, fs);                          /* wet: high-pass; dry: (2 + s/w) / (1 + s/w) */
        d->olc_dry = (struct op){ (2.0f * kk + 1.0f) / (kk + 1.0f), (2.0f * kk - 1.0f) / (kk + 1.0f), c };
    }
    float hicut = wvfx_clamp(k[K_HICUT], 200.0f, 20000.0f);
    d->nhc = 0;
    if (hicut < 19990.0f) {                                      /* order slope + 1, Butterworth */
        static const float q[5][2] = { { 0 }, { 0.707107f }, { 1.0f }, { 0.5411841f, 1.306506f }, { 1.61803f, 0.61803f } };
        const int order = t->slope + 1;
        if (order & 1)
            d->hc[d->nhc++] = bq_of_op(op_lp(hicut, fs));
        for (int j = 0; j < order / 2; j++)
            d->hc[d->nhc++] = bq_lp(hicut, q[t->slope][j], fs);
    }
    float sw, cw;
    modfx_mix(wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f, &sw, &cw);
    float level = wvfx_db(wvfx_clamp(k[K_LEVEL], -60.0f, 20.0f));
    d->gw = sw * level;
    d->gd = cw * level;
    if (t->autod) {                                              /* Auto Depth: the sweep's width per Hz */
        float sc = (t->autod == 1 ? 0.25f : t->autod == 2 ? 0.5f : 1.0f) / rate;
        d->width = sc * (d->width - d->delay) + d->delay;
    }
    if (d->width > 0.02f)
        d->width = 0.02f;
}

/* the LFO's shape at phase p (turns) */
static inline float lfo_shape(int form, float p)
{
    if (form == 0) {
        float sn, cs;
        modfx_sincos(p, &sn, &cs);
        return -cs;
    }
    float tri = modfx_tri(p);
    if (form == 2)
        return tri > 0.0f ? 1.0f : -1.0f;
    if (form == 7) {
        float sn, cs;
        modfx_sincos(0.125f + 0.125f * tri, &sn, &cs);
        return 1.0f - 2.0f * sn;
    }
    return tri;
}

static inline float lfo_step(float v, int n)
{
    const float r = 1.0f / (float)(n - 1);
    const float x = (r * 0.5f + (0.5f + 0.5f * v)) * (float)(n - 1);
    const float q = r * (float)(int)x;
    return (q + q) - 1.0f;
}

/* the sweep smoother: a one-pole at the control rate whose cutoff follows x */
static inline float sweep_smooth(float *sm, float x, float f, float fs)
{
    const float h = 0.5f * ((float)CTRL * f * MODFX_TWO_PI / fs), g = 1.0f / (1.0f + h);
    const float a = h * g, c = g * (1.0f - h);
    const float y = *sm + x * a;
    *sm = y * c + a * x;
    return y;
}

static inline float law(const struct design *d, float m, float rm1, float y)
{
    if (d->law == 1) {
        if (m < 0.99f || m > 1.01f)
            return (modfx_pow(m, y) - 1.0f) * rm1;
        return y;
    }
    return d->law == 2 ? y * y : y;
}

/* one control step: the delay targets (s) and the Thru-Zero tap (frames) */
static void control(struct state *s, float fs, float tgt[2], int *ztap)
{
    const struct design *d = &s->d;
    s->ph = modfx_advance(s->ph, d->inc);
    float q = d->ofs + s->ph;
    if (q >= MODFX_TWO_PI)
        q -= MODFX_TWO_PI;
    float vl = lfo_shape(d->form, s->ph * (1.0f / MODFX_TWO_PI));
    float vr = d->ofs > 0.0f ? lfo_shape(d->form, q * (1.0f / MODFX_TWO_PI)) : vl;
    if (d->steps >= 2) {
        vl = lfo_step(vl, d->steps);
        vr = lfo_step(vr, d->steps);
    }
    vl *= 0.5f;
    vr *= 0.5f;
    const float man = (1.0f - d->depth) * d->manual;
    float xl = man + d->depth * (0.5f + vl), xr = d->depth * (0.5f + vr) + man;
    float yl = sweep_smooth(&s->sm[0], xl, d->fk + d->fd * xl, fs);
    float yr = sweep_smooth(&s->sm[1], xr, d->fd * xr + d->fk, fs);
    float mz = man + d->depth * 0.5f;
    if (d->inv) {
        yl = 1.0f - yl;
        yr = 1.0f - yr;
        mz = 1.0f - mz;
    }
    const float ratio = d->width / d->delay, m = ratio * d->shape, rm1 = 1.0f / (m - 1.0f);
    yl = law(d, m, rm1, yl);
    yr = law(d, m, rm1, yr);
    mz = law(d, m, rm1, mz);
    const float span = d->width - d->delay;
    tgt[0] = dk * (span * yl + d->delay);
    tgt[1] = dk * (yr * span + d->delay);
    *ztap = (int)((mz * span + d->delay) * fs);
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
    (void)env;
    for (unsigned i = 0; i < sizeof *s / sizeof(float); i++)
        ((float *)s)[i] = 0.0f;
    float *b = buffer;
    for (unsigned i = 0; i < 2 * STRIDE + 2 * LINE; i++)
        b[i] = 0.0f;
    s->line[0] = b;
    s->line[1] = b + STRIDE;
    s->dry[0] = b + 2 * STRIDE;
    s->dry[1] = b + 2 * STRIDE + LINE;
    s->ds[0] = s->ds[1] = 0.001f;
    changed(s, knobs, 500);
    design(s, knobs, 500, MODFX_FS);
    s->gd = 0.0f;
    s->gw = 0.0f;
    s->gt = 1.0f;
    return 0;
}

/* one pass of the loop over SUB frames of one channel at w */
static inline void loop_pass(struct state *s, int ch, float *wet, const float *in, uint32_t w, float tgt, float fs)
{
    const struct design *d = &s->d;
    float *line = s->line[ch];
    float x[SUB], fb[SUB];
    float sm = s->ds[ch];
    for (int i = 0; i < SUB; i++) {
        sm = tgt + dr * sm;
        float del = fs * sm - 1.0f;
        if (del < MIN_D)
            del = MIN_D;
        x[i] = modfx_tap(line, MASK, w + (uint32_t)i, del);
    }
    s->ds[ch] = sm;
    if (d->lowcut_on)
        bq_block(&d->lowcut, s->s_low[ch], x, SUB);
    for (int j = 0; j < d->nhc; j++)
        bq_block(&d->hc[j], s->s_hc[j][ch], x, SUB);
    const float rv = d->rev[ch], g = d->fbg;
    for (int i = 0; i < SUB; i++) {
        x[i] = x[i] * rv;
        fb[i] = x[i] * g;
    }
    bq_block(&d->fbf, s->s_fbf[ch], fb, SUB);
    if (d->bbd_on)
        bq_block(&d->bbd, s->s_bbd[ch], x, SUB);
    for (int i = 0; i < SUB; i++)
        fb[i] = fb[i] + in[2 * i];
    if (d->clip > 0.0f)
        for (int i = 0; i < SUB; i++)
            fb[i] = modfx_bbd_clip(fb[i], d->clip, d->rclip);
    for (int i = 0; i < SUB; i++) {
        line[(w + (uint32_t)i) & MASK] = fb[i];
        wet[i] = x[i];
    }
    modfx_mirror(line, LINE);
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
    float wet[2][WVFX_MAX_FRAMES], dry[2][WVFX_MAX_FRAMES];
    for (int c0 = 0; c0 < n; c0 += CTRL) {
        float tgt[2];
        int ztap;
        control(s, fs, tgt, &ztap);
        const int m = n - c0 < CTRL ? n - c0 : CTRL;
        if (idle) {                                     /* bypassed: the input fills the lines, nothing else */
            for (int ch = 0; ch < 2; ch++)
                for (int i = 0; i < m; i++)
                    s->line[ch][(s->w + (uint32_t)i) & MASK] = x[2 * (c0 + i) + ch];
            for (int ch = 0; ch < 2; ch++)
                modfx_mirror(s->line[ch], LINE);
        } else {
            for (int sb = 0; sb < m; sb += SUB)
                for (int ch = 0; ch < 2; ch++)
                    loop_pass(s, ch, &wet[ch][c0 + sb], &x[2 * (c0 + sb) + ch], s->w + (uint32_t)sb, tgt[ch], fs);
        }
        for (int ch = 0; ch < 2; ch++) {                /* the Thru-Zero line, always written */
            float *dl = s->dry[ch];
            for (int i = 0; i < m; i++) {
                dl[(s->w + (uint32_t)i) & MASK] = x[2 * (c0 + i) + ch];
                dry[ch][c0 + i] = d->zero ? dl[(s->w + (uint32_t)i - (uint32_t)ztap) & MASK] : x[2 * (c0 + i) + ch];
            }
        }
        s->w = (s->w + (uint32_t)m) & MASK;
    }
    if (idle)
        return;
    if (d->olc_on)
        for (int ch = 0; ch < 2; ch++) {
            op_block(&d->olc_wet, &s->s_olcw[ch], wet[ch], n);
            op_block(&d->olc_dry, &s->s_olcd[ch], dry[ch], n);
        }
    if (d->spread_on) {
        const float a = d->spread < 0.0f ? -d->spread : d->spread, g = 1.0f / (1.0f + a), gs = d->spread * g;
        for (int i = 0; i < n; i++) {
            float mm = (wet[0][i] + wet[1][i]) * g, sd = (wet[0][i] - wet[1][i]) * gs;
            wet[0][i] = mm + sd;
            wet[1][i] = mm - sd;
        }
    }
    const float dw = wvfx_ramp_step(s->gw, tw, n), dd = wvfx_ramp_step(s->gd, td, n), dt = wvfx_ramp_step(s->gt, tt, n);
    float gw = s->gw, gd = s->gd, gt = s->gt;
    for (int i = 0; i < n; i++) {
        gw += dw;
        gd += dd;
        gt += dt;
        x[2 * i] = (gw * wet[0][i] + gd * dry[0][i]) + gt * x[2 * i];
        x[2 * i + 1] = (gw * wet[1][i] + gd * dry[1][i]) + gt * x[2 * i + 1];
    }
    s->gw = tw;
    s->gd = td;
    s->gt = tt;
}

const struct wvfx_desc flanger_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 2),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Flanger",
    .category = "Flanger",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = (2 * STRIDE + 2 * LINE) * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
