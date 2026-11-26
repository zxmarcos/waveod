/*
 * The Chorus block (docs/chorus.md) on the PC: what the firmware relies on. Its sound against the reference is
 * checked outside this repository; the ARM build against this one by tools/check_modfx.py.
 *
 *   - the descriptor: MOD, defaults in range, knobs not shared, the line in the buffer, no poll;
 *   - every type, Mix 100 %: finite, the level within -9..+3 dB of the input's;
 *   - the sweep: a 689 Hz tone's phase moves by more than 1 rad and repeats at the Rate (and at the synced rate);
 *   - stereo: the Stereo types differ between L and R, a Mono type gives L = R bit for bit;
 *   - bypassed from the start: the input bit for bit; switched off: one block of fade, then the input;
 *   - knobs moved every block (Type, Delay, Wave, High Cut): finite, bounded.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc chorus_block;

#define FRAMES 64
#define FS     44100.0f

enum { K_RATE, K_DEPTH, K_DELAY, K_MIX, K_PHASE, K_WAVE, K_HICUT, K_LEVEL, K_SYNC, K_TYPE };

struct inst {
    void *state, *buffer;
    float knobs[WVFX_KNOBS];
    long t;
};

static void start(struct inst *x)
{
    const struct wvfx_desc *d = &chorus_block;
    x->state = calloc(1, d->state_size);
    x->buffer = calloc(1, d->buffer_size);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < d->nparams; i++)
        x->knobs[d->params[i].knob] = d->params[i].def;
    x->t = 0;
}

static void begin(struct inst *x)
{
    struct wvfx_env env = { WVFX_ABI, FS, FRAMES, 0, 0, 0, 0 };
    chorus_block.init(x->state, x->buffer, &env, x->knobs);
}

static void stop(struct inst *x)
{
    free(x->state);
    free(x->buffer);
}

/* one block of a tone of one cycle per block (689 Hz, the same on both sides); in[] keeps the input */
static void block(struct inst *x, int enabled, uint32_t beat, float *io, float *in)
{
    for (int f = 0; f < FRAMES; f++) {
        float v = 0.25f * sinf(6.2831853f * (float)f / FRAMES);
        io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = v;
    }
    x->t += FRAMES;
    float ret[2 * FRAMES] = { 0 }, send[2 * FRAMES];
    struct wvfx_io w = { io, in, send, ret, x->knobs, beat, FRAMES, (uint8_t)enabled, 0 };
    chorus_block.process(x->state, &w);
}

/* the phase of channel ch over one block, relative to the input's */
static float block_phase(const float *io, int ch)
{
    double re = 0, im = 0;
    for (int f = 0; f < FRAMES; f++) {
        double a = 6.283185307 * f / FRAMES;
        re += io[2 * f + ch] * cos(a);
        im += io[2 * f + ch] * sin(a);
    }
    return (float)atan2(re, im);
}

static void test_descriptor(void)
{
    const struct wvfx_desc *d = &chorus_block;
    CHECK(d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && d->menu == WVFX_MOD && WVFX_IS_ID(d->id) &&
          ((d->id >> 16) & 0xff) == WVFX_MOD && !d->poll, "Chorus: descriptor (%#x), no poll", (unsigned)d->id);
    unsigned used = 0;
    int ok = 1;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        ok &= p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & (1u << p->knob));
        used |= 1u << p->knob;
    }
    CHECK(ok && d->nparams == 10, "Chorus: %d parameters, defaults in range, knobs not shared", d->nparams);
    CHECK(d->buffer_size >= 2 * 4096 * sizeof(float) && d->state_size < 1024,
          "Chorus: state %u bytes (fast pool), line %u bytes (buffer pool)", (unsigned)d->state_size,
          (unsigned)d->buffer_size);
}

static void test_types(void)
{
    int bad = 0;
    float lo = 1e9f, hi = -1e9f;
    for (int t = 0; t <= 22; t++) {
        struct inst x;
        start(&x);
        x.knobs[K_TYPE] = (float)t;
        x.knobs[K_MIX] = 100.0f;
        begin(&x);
        double e = 0, ein = 0;
        int finite = 1;
        float io[2 * FRAMES], in[2 * FRAMES];
        for (int b = 0; b < 400; b++) {
            block(&x, 1, 500, io, in);
            for (int i = 0; i < 2 * FRAMES; i++) {
                finite &= isfinite(io[i]);
                if (b >= 40) {
                    e += (double)io[i] * io[i];
                    ein += (double)in[i] * in[i];
                }
            }
        }
        float db = (float)(10.0 * log10(e / ein));
        lo = db < lo ? db : lo;
        hi = db > hi ? db : hi;
        bad += !finite || !(db > -9.0f && db < 3.0f);
        stop(&x);
    }
    CHECK(!bad, "Chorus: the 23 types at Mix 100 %%: finite, level %.1f .. %.1f dB re the input", lo, hi);
}

/* the phase over 3 s at a rate; returns its range, and the largest difference one period apart */
static void sweep(float rate_knob, int sync, uint32_t beat, float period_s, float *range, float *repeat)
{
    struct inst x;
    start(&x);
    x.knobs[K_TYPE] = 1.0f;
    x.knobs[K_MIX] = 100.0f;
    x.knobs[K_RATE] = rate_knob;
    x.knobs[K_SYNC] = (float)sync;
    begin(&x);
    enum { NB = 3 * 44100 / FRAMES };
    static float ph[NB];
    float io[2 * FRAMES], in[2 * FRAMES], prev = 0.0f, acc = 0.0f;
    for (int b = 0; b < NB; b++) {
        block(&x, 1, beat, io, in);
        float p = block_phase(io, 0);
        if (b > 0) {                                    /* unwrapped */
            float dp = p - prev;
            while (dp > 3.14159265f)
                dp -= 6.28318531f;
            while (dp < -3.14159265f)
                dp += 6.28318531f;
            acc += dp;
        }
        prev = p;
        ph[b] = acc;
    }
    int per = (int)(period_s * FS / FRAMES + 0.5f), from = 100;
    float mn = 1e9f, mx = -1e9f, rep = 0.0f;
    for (int b = from; b < NB; b++) {
        mn = ph[b] < mn ? ph[b] : mn;
        mx = ph[b] > mx ? ph[b] : mx;
        if (b + per < NB) {
            float d = fabsf(ph[b + per] - ph[b]);
            rep = d > rep ? d : rep;
        }
    }
    *range = mx - mn;
    *repeat = rep;
    stop(&x);
}

static void test_sweep(void)
{
    float range, rep;
    sweep(1.0f, 0, 500, 1.0f, &range, &rep);
    CHECK(range > 1.0f && rep < 0.25f, "Chorus: at 1 Hz a 689 Hz tone's phase moves %.2f rad, one period later "
          "within %.3f rad", range, rep);
    sweep(4.0f, 1, 500, 0.5f, &range, &rep);                /* Sync, 1/4 at 120 bpm: 2 Hz */
    CHECK(range > 0.5f && rep < 0.25f, "Chorus: synced to 1/4 at 120 bpm: moves %.2f rad, repeats every 0.5 s "
          "within %.3f rad", range, rep);
}

static void test_stereo(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x);
    x.knobs[K_MIX] = 100.0f;
    begin(&x);
    double d = 0, e = 0;
    for (int b = 0; b < 300; b++) {
        block(&x, 1, 500, io, in);
        for (int f = 0; f < FRAMES; f++) {
            double l = io[2 * f], r = io[2 * f + 1];
            d += (l - r) * (l - r);
            e += l * l;
        }
    }
    stop(&x);
    CHECK(d / e > 0.01, "Chorus: Digital Stereo, Phase 90: L and R differ (%.1f dB)", 10.0 * log10(d / e));
    start(&x);
    x.knobs[K_TYPE] = 2.0f;                             /* Analog Mono */
    x.knobs[K_MIX] = 100.0f;
    begin(&x);
    int same = 1;
    for (int b = 0; b < 300; b++) {
        block(&x, 1, 500, io, in);
        for (int f = 0; f < FRAMES; f++)
            same &= io[2 * f] == io[2 * f + 1];
    }
    stop(&x);
    CHECK(same, "Chorus: Analog Mono, the same input on both sides: L = R bit for bit");
}

static void test_bypass(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x);
    begin(&x);
    int same = 1;
    for (int b = 0; b < 50; b++) {
        block(&x, 0, 500, io, in);
        same &= !memcmp(io, in, sizeof io);
    }
    CHECK(same, "Chorus: bypassed from the start: the input bit for bit");
    for (int b = 0; b < 50; b++)
        block(&x, 1, 500, io, in);
    block(&x, 1, 500, io, in);
    int differs = memcmp(io, in, sizeof io) != 0;
    block(&x, 0, 500, io, in);                          /* the fade */
    int fade = memcmp(io, in, sizeof io) != 0;
    same = 1;
    for (int b = 0; b < 20; b++) {
        block(&x, 0, 500, io, in);
        same &= !memcmp(io, in, sizeof io);
    }
    stop(&x);
    CHECK(differs && fade && same, "Chorus: switched off: one block of fade, then the input bit for bit");
}

static void test_knobs(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x);
    begin(&x);
    int ok = 1;
    for (int b = 0; b < 600; b++) {
        x.knobs[K_TYPE] = (float)(b % 23);
        x.knobs[K_DELAY] = 0.1f + (float)(b % 50);
        x.knobs[K_WAVE] = (float)((b / 7) & 1);
        x.knobs[K_HICUT] = b & 1 ? 20000.0f : 200.0f + 37.0f * (float)(b % 500);
        x.knobs[K_DEPTH] = (float)(b % 101);
        x.knobs[K_RATE] = 0.1f + 0.017f * (float)(b % 580);
        block(&x, (b / 50) & 1 ? 0 : 1, 500, io, in);
        for (int i = 0; i < 2 * FRAMES; i++)
            ok &= isfinite(io[i]) && fabsf(io[i]) < 4.0f;
    }
    stop(&x);
    CHECK(ok, "Chorus: knobs moved every block (Type, Delay, Wave, High Cut, Depth, Rate), on / off: finite, bounded");
}

void test_chorus(void)
{
    test_descriptor();
    test_types();
    test_sweep();
    test_stereo();
    test_bypass();
    test_knobs();
}
