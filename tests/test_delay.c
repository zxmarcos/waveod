/*
 * The Delay block (docs/delay.md) on the PC: what the firmware relies on. Its sound against the reference is
 * checked outside this repository; the ARM build against this one by tools/check_modfx.py.
 *
 *   - the descriptor: DLY, defaults in range, knobs not shared, the lines in the buffer, no poll;
 *   - every type: a click comes back at the Time (the first echo's energy around it), finite;
 *   - Feedback 100 % on every type for 10 s: bounded;
 *   - bypassed from the start: the input bit for bit; switched off while echoing: the echoes ring out over the
 *     input, then (rung out) the input bit for bit;
 *   - a Time jump while echoing: no step in the output (the heads crossfade);
 *   - every knob moved at random, on / off: finite, bounded.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc delay_block;

#define FRAMES 64
#define FS     44100.0f

enum { K_TYPE, K_TIME, K_FB, K_MIX, K_LEVEL, K_LOWCUT, K_HICUT, K_MODRATE, K_MODDEPTH, K_SYNC };

struct inst {
    void *state, *buffer;
    float knobs[WVFX_KNOBS];
};

static void start(struct inst *x)
{
    const struct wvfx_desc *d = &delay_block;
    x->state = calloc(1, d->state_size);
    x->buffer = calloc(1, d->buffer_size);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < d->nparams; i++)
        x->knobs[d->params[i].knob] = d->params[i].def;
}

static void begin(struct inst *x)
{
    struct wvfx_env env = { WVFX_ABI, FS, FRAMES, 0, 0, 0, 0 };
    delay_block.init(x->state, x->buffer, &env, x->knobs);
}

static void stop(struct inst *x)
{
    free(x->state);
    free(x->buffer);
}

static void run(struct inst *x, float *io, float *in, int enabled)
{
    float ret[2 * FRAMES] = { 0 }, send[2 * FRAMES];
    struct wvfx_io w = { io, in, send, ret, x->knobs, 500, FRAMES, (uint8_t)enabled, 0 };
    delay_block.process(x->state, &w);
}

static void test_descriptor(void)
{
    const struct wvfx_desc *d = &delay_block;
    CHECK(d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && d->menu == WVFX_DLY && WVFX_IS_ID(d->id) &&
          ((d->id >> 16) & 0xff) == WVFX_DLY && !d->poll, "Delay: descriptor (%#x), no poll", (unsigned)d->id);
    unsigned used = 0;
    int ok = 1;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        ok &= p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & (1u << p->knob));
        used |= 1u << p->knob;
    }
    CHECK(ok && d->nparams == 10, "Delay: %d parameters, defaults in range, knobs not shared", d->nparams);
    CHECK(d->buffer_size >= 2 * 4 * 44100 * sizeof(float) && d->state_size < 2048,
          "Delay: state %u bytes, lines %u bytes (4 s)", (unsigned)d->state_size, (unsigned)d->buffer_size);
}

/* a click at frame 2205 (both sides); the energy of the output (L + R, the dry removed by Mix 100 %) */
static void test_echo_time(void)
{
    enum { NB = 1400 };                                 /* ~2 s */
    int bad = 0, worst_t = -1;
    float worst = 1e9f;
    for (int t = 0; t < 24; t++) {
        struct inst x;
        start(&x);
        x.knobs[K_TYPE] = (float)t;
        x.knobs[K_TIME] = 300.0f;
        x.knobs[K_FB] = 0.0f;
        x.knobs[K_MIX] = 100.0f;
        begin(&x);
        static float e[NB * FRAMES];
        int finite = 1;
        for (int b = 0; b < NB; b++) {
            float io[2 * FRAMES], in[2 * FRAMES];
            for (int f = 0; f < FRAMES; f++) {
                long n = (long)b * FRAMES + f;
                io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = n == 2205 ? 0.5f : 0.0f;
            }
            run(&x, io, in, 1);
            for (int f = 0; f < FRAMES; f++) {
                finite &= isfinite(io[2 * f]) && isfinite(io[2 * f + 1]);
                e[b * FRAMES + f] = io[2 * f] * io[2 * f] + io[2 * f + 1] * io[2 * f + 1];
            }
        }
        stop(&x);
        /* the echo: 300 ms after the click; the right side of some types is up to 5 % shorter */
        const int at = 2205 + (int)(0.300f * FS);
        double near = 0, before = 0;
        for (int i = at - (int)(0.025f * FS); i < at + (int)(0.025f * FS); i++)
            near += e[i];
        for (int i = 2205 + 200; i < at - (int)(0.025f * FS); i++)
            before += e[i];
        const float r = (float)(10.0 * log10((near + 1e-30) / (before + 1e-30)));
        if (r < worst) {
            worst = r;
            worst_t = t;
        }
        bad += !finite || !(near > 1e-6) || r < 20.0f;
    }
    CHECK(!bad, "Delay: the 24 types, a click at Time 300 ms: the echo there, %.0f dB over what comes before it "
          "(least: type %d)", worst, worst_t);
}

static void test_runaway(void)
{
    int ok = 1;
    float peak = 0.0f;
    for (int t = 0; t < 24; t++) {
        struct inst x;
        start(&x);
        x.knobs[K_TYPE] = (float)t;
        x.knobs[K_TIME] = 120.0f;
        x.knobs[K_FB] = (t & 1) ? -100.0f : 100.0f;
        x.knobs[K_MIX] = 100.0f;
        begin(&x);
        for (int b = 0; b < 10 * 689; b++) {
            float io[2 * FRAMES], in[2 * FRAMES];
            for (int f = 0; f < FRAMES; f++)
                io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = b < 100 ? 0.5f * sinf(0.05f * (float)(b * FRAMES + f)) : 0.0f;
            run(&x, io, in, 1);
            for (int i = 0; i < 2 * FRAMES; i++) {
                ok &= isfinite(io[i]);
                peak = fabsf(io[i]) > peak ? fabsf(io[i]) : peak;
            }
        }
        stop(&x);
    }
    CHECK(ok && peak < 64.0f, "Delay: Feedback +-100 %% for 10 s on every type: finite, peak %.2f", peak);
}

static void test_bypass(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x);
    begin(&x);
    int same = 1;
    for (int b = 0; b < 50; b++) {
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = 0.3f * sinf(0.03f * (float)(b * 2 * FRAMES + i));
        run(&x, io, in, 0);
        same &= !memcmp(io, in, sizeof io);
    }
    stop(&x);
    CHECK(same, "Delay: bypassed from the start: the input bit for bit");
    start(&x);
    x.knobs[K_FB] = 60.0f;
    begin(&x);
    for (int b = 0; b < 300; b++) {                     /* echoes */
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = b < 50 ? 0.3f * sinf(0.03f * (float)(b * 2 * FRAMES + i)) : 0.0f;
        run(&x, io, in, 1);
    }
    int rings = 0, later_same = 1, blocks = 0;
    for (int b = 0; b < 6000; b++) {                    /* off: they ring out over the input, then the input */
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = 0.1f * sinf(0.02f * (float)(b * 2 * FRAMES + i));
        run(&x, io, in, 0);
        const int diff = memcmp(io, in, sizeof io) != 0;
        if (b < 200)
            rings |= diff;
        if (b >= 5000) {
            later_same &= !diff;
            blocks++;
        }
    }
    stop(&x);
    CHECK(rings && later_same, "Delay: switched off while echoing: the echoes ring out, then the input bit for bit "
          "(the last %d blocks)", blocks);
}

static void test_time_jump(void)
{
    struct inst x;
    start(&x);
    x.knobs[K_MIX] = 100.0f;
    x.knobs[K_FB] = 50.0f;
    begin(&x);
    float io[2 * FRAMES], in[2 * FRAMES], prev = 0.0f, step = 0.0f;
    for (int b = 0; b < 2000; b++) {
        if (b == 1000)
            x.knobs[K_TIME] = 150.0f;                   /* from 400 ms */
        for (int f = 0; f < FRAMES; f++)
            io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = 0.3f * sinf(6.2831853f * 220.0f * (float)(b * FRAMES + f) / FS);
        run(&x, io, in, 1);
        for (int f = 0; f < FRAMES; f++) {
            if (b >= 990 && b < 1100) {
                const float d = fabsf(io[2 * f] - prev);
                step = d > step ? d : step;
            }
            prev = io[2 * f];
        }
    }
    stop(&x);
    /* a 220 Hz tone at 0.3 x (the echoes' sum) moves at most ~0.1 per frame; a cut would jump by its level */
    CHECK(step < 0.15f, "Delay: Time 400 -> 150 ms while echoing: no step (largest frame-to-frame change %.3f)", step);
}

static void test_knobs(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x);
    begin(&x);
    int ok = 1;
    uint32_t seed = 777u;
    for (int b = 0; b < 3000; b++) {
        for (int i = 0; i < delay_block.nparams; i++) {
            seed = seed * 1664525u + 1013904223u;
            if ((seed >> 28) != (unsigned)(i & 15))
                continue;
            const struct wvfx_param *p = &delay_block.params[i];
            x.knobs[p->knob] = p->min + (p->max - p->min) * (float)(seed >> 8 & 0xffff) / 65535.0f;
        }
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = 0.3f * sinf(0.013f * (float)(b * 2 * FRAMES + i));
        run(&x, io, in, (b / 200) & 1 ? 0 : 1);
        for (int i = 0; i < 2 * FRAMES; i++)
            ok &= isfinite(io[i]) && fabsf(io[i]) < 64.0f;
    }
    stop(&x);
    CHECK(ok, "Delay: knobs moved at random every block, on / off: finite, bounded");
}

void test_delay(void)
{
    test_descriptor();
    test_echo_time();
    test_runaway();
    test_bypass();
    test_time_jump();
    test_knobs();
}
