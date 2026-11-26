/*
 * The Plate and Spring blocks (docs/plate.md) on the PC: what the firmware relies on. Their sound against the
 * reference is checked outside this repository; the ARM builds against these by tools/check_modfx.py.
 *
 *   - the descriptor: RVB, defaults in range, knobs not shared, the lines in the buffer, no poll;
 *   - every type (the Plate at every quality): a click decays, finite, the tail -60 dB within 0.3..3 x Time
 *     (Time 1 s);
 *   - bypassed from the start: the input bit for bit; switched off with Spillover: the tail rings over the input,
 *     then the input bit for bit; without: the input after one block's fade;
 *   - every knob moved at random, on / off: finite, bounded.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc plate_block, spring_block;

#define FRAMES 64
#define FS     44100.0f

struct inst {
    const struct wvfx_desc *d;
    void *state, *buffer;
    float knobs[WVFX_KNOBS];
};

static int knob_of(const struct wvfx_desc *d, const char *name)
{
    for (int i = 0; i < d->nparams; i++)
        if (!strcmp(d->params[i].name, name))
            return d->params[i].knob;
    return -1;
}

static void start(struct inst *x, const struct wvfx_desc *d)
{
    x->d = d;
    x->state = calloc(1, d->state_size);
    x->buffer = calloc(1, d->buffer_size);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < d->nparams; i++)
        x->knobs[d->params[i].knob] = d->params[i].def;
}

static void set(struct inst *x, const char *name, float v)
{
    const int k = knob_of(x->d, name);
    if (k >= 0)
        x->knobs[k] = v;
}

static void begin(struct inst *x)
{
    struct wvfx_env env = { WVFX_ABI, FS, FRAMES, 0, 0, 0, 0 };
    x->d->init(x->state, x->buffer, &env, x->knobs);
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
    x->d->process(x->state, &w);
}

static void test_descriptor(const struct wvfx_desc *d, int nparams)
{
    CHECK(d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && d->menu == WVFX_RVB && WVFX_IS_ID(d->id) &&
          ((d->id >> 16) & 0xff) == WVFX_RVB && !d->poll, "%s: descriptor (%#x), no poll", d->name, (unsigned)d->id);
    unsigned used = 0;
    int ok = 1;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        ok &= p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & (1u << p->knob));
        used |= 1u << p->knob;
    }
    CHECK(ok && d->nparams == nparams, "%s: %d parameters, defaults in range, knobs not shared", d->name, d->nparams);
    CHECK(d->state_size < 4096 && d->buffer_size < 1024u * 1024u, "%s: state %u bytes, lines %u bytes", d->name,
          (unsigned)d->state_size, (unsigned)d->buffer_size);
}

/* a click, Time 1 s, Mix 100 %: the time for the tail (per block energy) to fall 60 dB from its peak */
static void test_decay(const struct wvfx_desc *d, int ntypes, int nq)
{
    enum { NB = 4 * 689 };
    int bad = 0, bad_t = -1, bad_q = -1;
    float lo = 1e9f, hi = -1e9f;
    static float e[NB];
    for (int q = 0; q < nq; q++)
        for (int t = 0; t < ntypes; t++) {
            struct inst x;
            start(&x, d);
            set(&x, "Type", (float)t);
            set(&x, "Time", 1.0f);
            set(&x, "Mix", 100.0f);
            set(&x, "Quality", (float)q);
            set(&x, "High Cut", 20000.0f);
            set(&x, "Low Cut", 20.0f);
            begin(&x);
            int finite = 1;
            for (int b = 0; b < NB; b++) {
                float io[2 * FRAMES], in[2 * FRAMES];
                for (int f = 0; f < FRAMES; f++)
                    io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = (b == 20 && f == 0) ? 0.5f : 0.0f;
                run(&x, io, in, 1);
                double s = 0;
                for (int i = 0; i < 2 * FRAMES; i++) {
                    finite &= isfinite(io[i]);
                    s += (double)io[i] * io[i];
                }
                e[b] = (float)s;
            }
            stop(&x);
            int pk = 0;
            for (int b = 0; b < NB; b++)
                pk = e[b] > e[pk] ? b : pk;
            int end = pk;
            for (int b = pk; b < NB; b++)
                if (e[b] > e[pk] * 1e-6f)
                    end = b;
            const float t60 = (float)(end - 20) * FRAMES / FS;
            lo = t60 < lo ? t60 : lo;
            hi = t60 > hi ? t60 : hi;
            if (!finite || !(t60 > 0.3f && t60 < 3.0f)) {
                bad++;
                bad_t = t;
                bad_q = q;
            }
        }
    CHECK(!bad, "%s: the %d types at %d qualities, Time 1 s: the tail -60 dB after %.2f .. %.2f s (%d off, last type "
          "%d quality %d)", d->name, ntypes, nq, lo, hi, bad, bad_t, bad_q);
}

static void test_bypass(const struct wvfx_desc *d)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x, d);
    begin(&x);
    int same = 1;
    for (int b = 0; b < 50; b++) {
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = 0.3f * sinf(0.03f * (float)(b * 2 * FRAMES + i));
        run(&x, io, in, 0);
        same &= !memcmp(io, in, sizeof io);
    }
    stop(&x);
    CHECK(same, "%s: bypassed from the start: the input bit for bit", d->name);
    for (int spill = 0; spill < 2; spill++) {
        start(&x, d);
        set(&x, "Spillover", (float)spill);
        set(&x, "Time", 1.0f);
        begin(&x);
        for (int b = 0; b < 300; b++) {
            for (int i = 0; i < 2 * FRAMES; i++)
                io[i] = in[i] = b < 100 ? 0.3f * sinf(0.03f * (float)(b * 2 * FRAMES + i)) : 0.0f;
            run(&x, io, in, 1);
        }
        int rings = 0, later = 1, soon = 1;
        for (int b = 0; b < 6000; b++) {
            for (int i = 0; i < 2 * FRAMES; i++)
                io[i] = in[i] = 0.1f * sinf(0.02f * (float)(b * 2 * FRAMES + i));
            run(&x, io, in, 0);
            const int diff = memcmp(io, in, sizeof io) != 0;
            if (b > 0 && b < 100) {
                rings |= diff;
                soon &= !diff;
            }
            if (b >= 5000)
                later &= !diff;
        }
        stop(&x);
        if (spill)
            CHECK(rings && later, "%s: switched off with Spillover: the tail rings out, then the input bit for bit", d->name);
        else
            CHECK(soon && later, "%s: switched off without Spillover: the input bit for bit after the fade", d->name);
    }
}

static void test_knobs(const struct wvfx_desc *d)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x, d);
    begin(&x);
    int ok = 1;
    uint32_t seed = 4242u;
    for (int b = 0; b < 3000; b++) {
        for (int i = 0; i < d->nparams; i++) {
            seed = seed * 1664525u + 1013904223u;
            if ((seed >> 28) != (unsigned)(i & 15))
                continue;
            const struct wvfx_param *p = &d->params[i];
            x.knobs[p->knob] = p->min + (p->max - p->min) * (float)(seed >> 8 & 0xffff) / 65535.0f;
        }
        for (int i = 0; i < 2 * FRAMES; i++)
            io[i] = in[i] = 0.3f * sinf(0.013f * (float)(b * 2 * FRAMES + i));
        run(&x, io, in, (b / 200) & 1 ? 0 : 1);
        for (int i = 0; i < 2 * FRAMES; i++)
            ok &= isfinite(io[i]) && fabsf(io[i]) < 64.0f;
    }
    stop(&x);
    CHECK(ok, "%s: knobs moved at random every block, on / off: finite, bounded", d->name);
}

void test_plate(void)
{
    test_descriptor(&plate_block, 9);
    test_descriptor(&spring_block, 8);
    test_decay(&plate_block, 8, 4);
    test_decay(&spring_block, 10, 1);
    test_bypass(&plate_block);
    test_bypass(&spring_block);
    test_knobs(&plate_block);
    test_knobs(&spring_block);
}
