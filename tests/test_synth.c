/*
 * The Synth block (docs/synth.md) and its pitch tracker on the PC. How it follows a real guitar was measured on DI
 * recordings outside this repository (docs/synth.md); these are what must not move:
 *
 *   - the descriptor: FX, defaults in range, knobs not shared, no buffer, the state small;
 *   - the tracker: steady tones E2..E6 within 3 cents; the latency of a note change, low and high;
 *   - the block: the synth's pitch is the guitar's (a triangle's zero crossings), Octave doubles it, Snap rounds
 *     it to the semitone; a second string ringing with the note does not silence it; silence and low noise
 *     give exact silence; bypassed from the start the input bit for
 *     bit, switched off the input again after the fade; every knob moving every block: finite, bounded.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include "blocks/synth/track.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc synth_block;

#define FRAMES 64
#define FS     44100.0f

static uint32_t rng = 12345u;

static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* a guitar-like steady tone: 6 harmonics falling off, a little noise; phase kept across calls */
static void tone(float *out, int frames, float hz, float amp, double *ph)
{
    for (int f = 0; f < frames; f++) {
        float v = 0.0f;
        for (int k = 1; k <= 6; k++)
            v += (float)sin(*ph * k) / (float)pow(k, 0.8);
        *ph += 2.0 * M_PI * hz / FS;
        if (*ph > 2.0 * M_PI * 1000.0)
            *ph -= 2.0 * M_PI * 1000.0;
        v = amp * v + amp * 0.002f * noise();
        out[2 * f] = v;
        out[2 * f + 1] = v;
    }
}

static float cents(float a, float b)
{
    return 1200.0f * log2f(a / b);
}

static int knob_of(const char *name)
{
    for (int i = 0; i < synth_block.nparams; i++)
        if (!strcmp(synth_block.params[i].name, name))
            return synth_block.params[i].knob;
    return -1;
}

struct inst {
    void *state;
    float knobs[WVFX_KNOBS];
};

static void start(struct inst *x)
{
    x->state = calloc(1, synth_block.state_size);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < synth_block.nparams; i++)
        x->knobs[synth_block.params[i].knob] = synth_block.params[i].def;
    struct wvfx_env env = { WVFX_ABI, FS, FRAMES, 0, 0, 0, 0 };
    synth_block.init(x->state, 0, &env, x->knobs);
}

/* one block: io = in (copied), aux = in */
static void run(struct inst *x, const float *in, float *out, int enabled)
{
    memcpy(out, in, sizeof(float) * 2 * FRAMES);
    struct wvfx_io io = { out, in, 0, 0, x->knobs, 500, FRAMES, (uint8_t)enabled, 0 };
    synth_block.process(x->state, &io);
}

/* the frequency of out (left) from its upward zero crossings, interpolated */
static float freq_of(const float *y, int n)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = 1; i < n; i++)
        if (y[2 * (i - 1)] < 0.0f && y[2 * i] >= 0.0f) {
            double t = (i - 1) + y[2 * (i - 1)] / (double)(y[2 * (i - 1)] - y[2 * i]);
            if (first < 0)
                first = t;
            last = t;
            count++;
        }
    return count > 2 ? (float)((count - 1) * FS / (last - first)) : 0.0f;
}

static void test_descriptor(void)
{
    const struct wvfx_desc *d = &synth_block;
    CHECK(d->magic == WVFX_MAGIC && d->menu == WVFX_FX && d->id == WVFX_ID(WVFX_FX, 1), "id %08x", (unsigned)d->id);
    CHECK(d->nparams <= WVFX_MAX_PARAMS && d->buffer_size == 0 && d->state_size < 24 * 1024 && !d->poll,
          "params %d state %u", d->nparams, (unsigned)d->state_size);
    unsigned used = 0;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        CHECK(p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & 1u << p->knob), "%s", p->name);
        used |= 1u << p->knob;
    }
}

static void test_tracker(void)
{
    static const float notes[] = { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f, 659.26f, 1318.5f };
    static struct trk t;
    float buf[2 * FRAMES];
    for (unsigned i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        trk_init(&t);
        double ph = 0;
        float worst = 0.0f;
        for (int b = 0; b < 200; b++) {                 /* 290 ms, the last 150 ms checked */
            tone(buf, FRAMES, notes[i], 0.05f, &ph);
            trk_feed(&t, buf, FRAMES);
            trk_estimate(&t);
            if (b >= 97) {
                float c = t.hz > 0.0f ? fabsf(cents(t.hz, notes[i])) : 1200.0f;
                worst = c > worst ? c : worst;
            }
        }
        CHECK(worst < 3.0f, "%.2f Hz: worst %.2f cents (hz %.2f clarity %.3f)", notes[i], worst, t.hz, t.clarity);
    }
    /* note changes: the first block on the new semitone */
    static const struct { float a, b, max_ms; } steps[] = {
        { 82.41f, 110.0f, 30.0f }, { 110.0f, 82.41f, 36.0f }, { 329.63f, 440.0f, 10.0f }, { 659.26f, 880.0f, 8.0f },
        { 1046.5f, 1318.5f, 8.0f },
    };
    for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        trk_init(&t);
        double ph = 0;
        float ms = -1.0f;
        for (int b = 0; b < 400 && ms < 0.0f; b++) {
            const int after = b >= 200;
            tone(buf, FRAMES, after ? steps[i].b : steps[i].a, 0.05f, &ph);
            trk_feed(&t, buf, FRAMES);
            trk_estimate(&t);
            if (after && t.hz > 0.0f && fabsf(cents(t.hz, steps[i].b)) < 50.0f)
                ms = (b - 199) * FRAMES * 1000.0f / FS;
        }
        CHECK(ms >= 0.0f && ms <= steps[i].max_ms, "%.0f -> %.0f Hz: %.1f ms", steps[i].a, steps[i].b, ms);
    }
}

/* the synth's frequency playing a tone of hz for 0.5 s, the last 0.3 s measured */
static float played(struct inst *x, float hz)
{
    static float out[2 * 13824];
    float in[2 * FRAMES], y[2 * FRAMES];
    double ph = 0;
    int o = 0;
    for (int b = 0; b < 345; b++) {
        tone(in, FRAMES, hz, 0.05f, &ph);
        run(x, in, y, 1);
        if (b >= 129 && o + FRAMES <= 13824) {
            memcpy(out + 2 * o, y, sizeof y);
            o += FRAMES;
        }
    }
    return freq_of(out, o);
}

static void test_pitch(void)
{
    struct inst x;
    start(&x);
    x.knobs[knob_of("Wave")] = 3.0f;                   /* triangle: two zero crossings a period */
    x.knobs[knob_of("Cutoff")] = 100.0f;
    x.knobs[knob_of("Resonance")] = 0.0f;
    x.knobs[knob_of("Env Amt")] = 0.0f;
    float f = played(&x, 220.0f);
    CHECK(fabsf(cents(f, 220.0f)) < 5.0f, "220 Hz played as %.2f Hz", f);
    free(x.state);

    start(&x);
    x.knobs[knob_of("Wave")] = 3.0f;
    x.knobs[knob_of("Cutoff")] = 100.0f;
    x.knobs[knob_of("Env Amt")] = 0.0f;
    x.knobs[knob_of("Resonance")] = 0.0f;
    x.knobs[knob_of("Octave")] = 1.0f;
    f = played(&x, 146.83f);
    CHECK(fabsf(cents(f, 293.66f)) < 5.0f, "Octave +1: 146.83 Hz played as %.2f Hz", f);
    free(x.state);

    start(&x);
    x.knobs[knob_of("Wave")] = 3.0f;
    x.knobs[knob_of("Cutoff")] = 100.0f;
    x.knobs[knob_of("Env Amt")] = 0.0f;
    x.knobs[knob_of("Resonance")] = 0.0f;
    x.knobs[knob_of("Mode")] = 1.0f;                   /* Follow Snap */
    f = played(&x, 225.0f);                            /* A3 + 39 cents */
    CHECK(fabsf(cents(f, 220.0f)) < 5.0f, "Snap: 225 Hz played as %.2f Hz", f);
    free(x.state);
}

static void test_silence_bypass(void)
{
    struct inst x;
    float in[2 * FRAMES], y[2 * FRAMES];
    start(&x);
    int quiet = 1;
    for (int b = 0; b < 300; b++) {                    /* nothing, then noise at -80 dBFS */
        for (int i = 0; i < 2 * FRAMES; i++)
            in[i] = b < 100 ? 0.0f : 0.0001f * noise();
        run(&x, in, y, 1);
        for (int i = 0; i < 2 * FRAMES; i++)
            quiet &= y[i] == 0.0f;
    }
    CHECK(quiet, "silence and low noise: no sound");
    free(x.state);

    start(&x);
    double ph = 0;
    int same = 1;
    for (int b = 0; b < 200; b++) {                    /* bypassed from the start */
        tone(in, FRAMES, 196.0f, 0.05f, &ph);
        run(&x, in, y, 0);
        same &= !memcmp(in, y, sizeof y);
    }
    CHECK(same, "bypassed: the input bit for bit");
    for (int b = 0; b < 200; b++) {                    /* on, playing */
        tone(in, FRAMES, 196.0f, 0.05f, &ph);
        run(&x, in, y, 1);
    }
    run(&x, in, y, 0);                                 /* off: one block of fade */
    same = 1;
    for (int b = 0; b < 50; b++) {
        tone(in, FRAMES, 196.0f, 0.05f, &ph);
        run(&x, in, y, 0);
        same &= !memcmp(in, y, sizeof y);
    }
    CHECK(same, "switched off: the input again after one block");
    free(x.state);
}

/* a note, then a second string ringing with it (no clear pitch): the synth goes on, never 20 dB under its level */
static void test_two_strings(void)
{
    struct inst x;
    float in[2 * FRAMES], in2[2 * FRAMES], y[2 * FRAMES];
    start(&x);
    double ph = 0, ph2 = 0;
    float level = 0.0f, lowest = 1e9f, e = 0.0f;
    for (int b = 0; b < 600; b++) {
        tone(in, FRAMES, 110.0f, 0.05f, &ph);
        if (b >= 200) {
            tone(in2, FRAMES, 293.66f, 0.05f, &ph2);
            for (int i = 0; i < 2 * FRAMES; i++)
                in[i] += in2[i];
        }
        run(&x, in, y, 1);
        for (int i = 0; i < FRAMES; i++)
            e += y[2 * i] * y[2 * i];
        if (b % 16 == 15) {                            /* RMS over 16 blocks: longer than the low note's period */
            const float r = sqrtf(e / (16 * FRAMES));
            e = 0.0f;
            if (b >= 128 && b < 192)
                level = r > level ? r : level;
            if (b >= 208)
                lowest = r < lowest ? r : lowest;
        }
    }
    CHECK(level > 0.0f && lowest > 0.1f * level, "two strings: lowest %.1f dB under the note's level",
          20.0f * log10f(level / (lowest + 1e-12f)));
    free(x.state);
}

/* an open-string arpeggio let ring: the six strings (a little out of tune with each other, harmonics decaying,
   the high ones faster, a click of noise at the attack) one every 0.8 s, each going on under the next. The synth must play each new string from
   80 ms after its attack to the next one (two windows measured: no jumping back to a string still ringing) */
static void test_arpeggio(void)
{
    static const float strings[] = { 82.6f, 109.8f, 147.1f, 195.7f, 247.3f, 329.2f };
    enum { N = 6, SEG = 552 };                           /* blocks per string: 0.8 s */
    static float out[2 * SEG * FRAMES];
    struct inst x;
    start(&x);
    x.knobs[knob_of("Wave")] = 3.0f;
    x.knobs[knob_of("Cutoff")] = 100.0f;
    x.knobs[knob_of("Env Amt")] = 0.0f;
    x.knobs[knob_of("Resonance")] = 0.0f;
    double ph[N] = { 0 };
    float in[2 * FRAMES], y[2 * FRAMES];
    for (int sgm = 0; sgm < N; sgm++) {
        for (int b = 0; b < SEG; b++) {
            for (int f = 0; f < FRAMES; f++) {
                float v = 0.0f;
                for (int k = 0; k <= sgm; k++) {
                    const float t = ((sgm - k) * SEG * FRAMES + b * FRAMES + f) / FS;   /* since string k's pick */
                    for (int h = 1; h <= 6; h++)
                        v += 0.05f * expf(-t * (0.7f + 0.6f * h)) * (float)sin(ph[k] * h) / (float)pow(h, 0.8);
                    ph[k] += 2.0 * M_PI * strings[k] / FS;
                }
                if (b * FRAMES + f < 220)                      /* the pick: 5 ms of noise */
                    v += 0.02f * noise();
                in[2 * f] = in[2 * f + 1] = v;
            }
            run(&x, in, y, 1);
            memcpy(out + 2 * b * FRAMES, y, sizeof y);
        }
        const int a = 55 * FRAMES, mid = (SEG / 2) * FRAMES, end = SEG * FRAMES;  /* 80 ms .. 0.4 s .. 0.8 s */
        const float f1 = freq_of(out + 2 * a, mid - a), f2 = freq_of(out + 2 * mid, end - mid);
        CHECK(fabsf(cents(f1, strings[sgm])) < 25.0f && fabsf(cents(f2, strings[sgm])) < 25.0f,
              "arpeggio string %d (%.1f Hz): played %.1f then %.1f Hz", sgm + 1, strings[sgm], f1, f2);
    }
    free(x.state);
}

/* the Gate: a note at about -55 dBFS RMS plays at Gate -70 and at 0 (a preset from before the Gate, its word
   held Sub's level: the default), and gives exact silence at Gate -45 */
static float gated_peak(float gate)
{
    struct inst x;
    float in[2 * FRAMES], y[2 * FRAMES], peak = 0.0f;
    rng = 12345u;                                       /* the same noise in every run */
    start(&x);
    x.knobs[knob_of("Gate")] = gate;
    double ph = 0;
    for (int b = 0; b < 300; b++) {
        tone(in, FRAMES, 196.0f, 0.0015f, &ph);
        run(&x, in, y, 1);
        for (int i = 0; b >= 4 && i < 2 * FRAMES; i++)     /* past the first block's fade from the dry signal */
            peak = fabsf(y[i]) > peak ? fabsf(y[i]) : peak;
    }
    free(x.state);
    return peak;
}

static void test_gate(void)
{
    const float open = gated_peak(-70.0f), old = gated_peak(0.0f), shut = gated_peak(-45.0f);
    CHECK(open > 0.0f && old == open && shut == 0.0f, "Gate -70: %g, 0 (old preset): %g, -45: %g", open, old, shut);
}

static void test_stress(void)
{
    struct inst x;
    float in[2 * FRAMES], y[2 * FRAMES];
    start(&x);
    double ph = 0;
    int finite = 1;
    float peak = 0.0f;
    for (int b = 0; b < 3000; b++) {
        for (int i = 0; i < synth_block.nparams; i++) {
            const struct wvfx_param *p = &synth_block.params[i];
            float r = 0.5f + 0.5f * noise();
            x.knobs[p->knob] = b % 300 < 100 ? p->max : b % 300 < 200 ? p->min + r * (p->max - p->min) : p->min;
        }
        tone(in, FRAMES, b % 500 < 250 ? 82.41f : 1318.5f, b % 7 ? 0.5f : 0.9f, &ph);
        run(&x, in, y, b % 400 < 380);
        for (int i = 0; i < 2 * FRAMES; i++) {
            finite &= isfinite(y[i]);
            peak = fabsf(y[i]) > peak ? fabsf(y[i]) : peak;
        }
    }
    /* bounded: the ladder's input clip (~3.3) times the largest output gain (Level +20 dB x SYNTH_GAIN 0.5 = 5) */
    CHECK(finite && peak < 17.0f, "every knob moving: finite %d, peak %.2f", finite, peak);
    free(x.state);
}

void test_synth(void)
{
    test_descriptor();
    test_tracker();
    test_pitch();
    test_silence_bypass();
    test_two_strings();
    test_arpeggio();
    test_gate();
    test_stress();
}
