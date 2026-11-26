/*
 * The Flanger, Phaser, Rotary, Vibe and Tremolo blocks (docs/modfx.md) on the PC: what the firmware relies on. Their sound against
 * the reference implementations is checked outside this repository; the ARM builds against these by
 * tools/check_modfx.py.
 *
 *   - the descriptor: MOD, defaults in range, knobs not shared, the state small, no poll;
 *   - every type (the Rotary: its defaults), Mix 100 %: finite, the level within -15..+12 dB of the input's, and
 *     the sweep moving a 689 Hz tone (its phase or its level over 3 s);
 *   - bypassed from the start: the input bit for bit; switched off: one block of fade, then the input;
 *   - every knob moved every block, on / off: finite, bounded;
 *   - the Tremolo's Panner: L and R move against each other, their sum stays at the input's.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc flanger_block, phaser_block, rotary_block, vibe_block, tremolo_block;

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
    x->buffer = calloc(1, d->buffer_size ? d->buffer_size : 1);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < d->nparams; i++)
        x->knobs[d->params[i].knob] = d->params[i].def;
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

/* one block of a tone of one cycle per block (the same on both sides); in[] keeps the input */
static void block(struct inst *x, int enabled, float *io, float *in)
{
    for (int f = 0; f < FRAMES; f++) {
        float v = 0.25f * sinf(6.2831853f * (float)f / FRAMES);
        io[2 * f] = io[2 * f + 1] = in[2 * f] = in[2 * f + 1] = v;
    }
    float ret[2 * FRAMES] = { 0 }, send[2 * FRAMES];
    struct wvfx_io w = { io, in, send, ret, x->knobs, 500, FRAMES, (uint8_t)enabled, 0 };
    x->d->process(x->state, &w);
}

static void test_descriptor(const struct wvfx_desc *d)
{
    CHECK(d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && d->menu == WVFX_MOD && WVFX_IS_ID(d->id) &&
          ((d->id >> 16) & 0xff) == WVFX_MOD && !d->poll, "%s: descriptor (%#x), no poll", d->name, (unsigned)d->id);
    unsigned used = 0;
    int ok = 1;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        ok &= p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & (1u << p->knob));
        used |= 1u << p->knob;
    }
    CHECK(ok && d->nparams > 0, "%s: %d parameters, defaults in range, knobs not shared", d->name, d->nparams);
    CHECK(d->state_size < 2048, "%s: state %u bytes (fast pool), buffer %u bytes", d->name, (unsigned)d->state_size,
          (unsigned)d->buffer_size);
}

static void test_types(const struct wvfx_desc *d)
{
    const int kt = knob_of(d, "Type"), km = knob_of(d, "Mix");
    int ntypes = 1;
    for (int i = 0; i < d->nparams; i++)
        if (d->params[i].knob == kt)
            ntypes = (int)d->params[i].max + 1;
    int bad = 0, still = 0;
    float lo = 1e9f, hi = -1e9f, least = 1e9f;
    for (int t = 0; t < ntypes; t++) {
        struct inst x;
        start(&x, d);
        if (kt >= 0)
            x.knobs[kt] = (float)t;
        if (km >= 0)
            x.knobs[km] = 100.0f;
        begin(&x);
        double e = 0, ein = 0;
        int finite = 1;
        float io[2 * FRAMES], in[2 * FRAMES], pmin = 1e9f, pmax = -1e9f, lmin = 1e9f, lmax = -1e9f;
        for (int b = 0; b < 3 * 689; b++) {
            block(&x, 1, io, in);
            double re = 0, im = 0, eb = 0;
            for (int f = 0; f < FRAMES; f++) {
                finite &= isfinite(io[2 * f]) && isfinite(io[2 * f + 1]);
                re += io[2 * f] * cos(6.283185307 * f / FRAMES);
                im += io[2 * f] * sin(6.283185307 * f / FRAMES);
                eb += (double)io[2 * f] * io[2 * f];
            }
            if (b < 100)
                continue;
            for (int i = 0; i < 2 * FRAMES; i++) {
                e += (double)io[i] * io[i];
                ein += (double)in[i] * in[i];
            }
            float ph = (float)atan2(re, im), lv = (float)(10.0 * log10(eb + 1e-20));
            pmin = ph < pmin ? ph : pmin;
            pmax = ph > pmax ? ph : pmax;
            lmin = lv < lmin ? lv : lmin;
            lmax = lv > lmax ? lv : lmax;
        }
        float db = (float)(10.0 * log10(e / ein)), moves = (pmax - pmin) + 0.1f * (lmax - lmin);
        lo = db < lo ? db : lo;
        hi = db > hi ? db : hi;
        least = moves < least ? moves : least;
        bad += !finite || !(db > -15.0f && db < 12.0f);
        still += !(moves > 0.05f);
        stop(&x);
    }
    CHECK(!bad && !still, "%s: %d type(s) at Mix 100 %%: finite, level %.1f .. %.1f dB re the input, the sweep moves "
          "a tone (least %.2f)", d->name, ntypes, lo, hi, least);
}

static void test_bypass(const struct wvfx_desc *d)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x, d);
    begin(&x);
    int same = 1;
    for (int b = 0; b < 50; b++) {
        block(&x, 0, io, in);
        same &= !memcmp(io, in, sizeof io);
    }
    CHECK(same, "%s: bypassed from the start: the input bit for bit", d->name);
    for (int b = 0; b < 50; b++)
        block(&x, 1, io, in);
    int differs = memcmp(io, in, sizeof io) != 0;
    block(&x, 0, io, in);
    int fade = memcmp(io, in, sizeof io) != 0;
    same = 1;
    for (int b = 0; b < 20; b++) {
        block(&x, 0, io, in);
        same &= !memcmp(io, in, sizeof io);
    }
    for (int b = 0; b < 20; b++)                        /* and back on */
        block(&x, 1, io, in);
    int back = memcmp(io, in, sizeof io) != 0;
    stop(&x);
    CHECK(differs && fade && same && back, "%s: switched off: one block of fade, then the input bit for bit; back on",
          d->name);
}

static void test_knobs(const struct wvfx_desc *d)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x, d);
    begin(&x);
    int ok = 1;
    uint32_t seed = 12345u;
    for (int b = 0; b < 1500; b++) {
        for (int i = 0; i < d->nparams; i++) {          /* one knob at a random value per block */
            seed = seed * 1664525u + 1013904223u;
            if ((seed >> 28) != (unsigned)(i & 15))
                continue;
            const struct wvfx_param *p = &d->params[i];
            float u = (float)(seed >> 8 & 0xffff) / 65535.0f;
            x.knobs[p->knob] = p->min + (p->max - p->min) * u;
        }
        block(&x, (b / 100) & 1 ? 0 : 1, io, in);
        for (int i = 0; i < 2 * FRAMES; i++)
            ok &= isfinite(io[i]) && fabsf(io[i]) < 64.0f;
    }
    stop(&x);
    CHECK(ok, "%s: knobs moved at random every block, on / off: finite, bounded", d->name);
}

static void test_panner(void)
{
    float io[2 * FRAMES], in[2 * FRAMES];
    struct inst x;
    start(&x, &tremolo_block);
    x.knobs[knob_of(&tremolo_block, "Mode")] = 1.0f;
    x.knobs[knob_of(&tremolo_block, "Wave")] = 0.0f;
    begin(&x);
    double worst = 0, cross = 0, el = 0, er = 0;
    for (int b = 0; b < 1300; b++) {                    /* after the depth's fade-in (33 ms time constant) */
        block(&x, 1, io, in);
        if (b < 400)
            continue;
        double l = 0, r = 0, s = 0;
        for (int f = 0; f < FRAMES; f++) {
            l += (double)io[2 * f] * io[2 * f];
            r += (double)io[2 * f + 1] * io[2 * f + 1];
            double sum = (double)io[2 * f] + io[2 * f + 1], want = 2.0 * (1.0 - 0.4) * in[2 * f];
            s = fabs(sum - want) > s ? fabs(sum - want) : s;
        }
        worst = s > worst ? s : worst;
        cross += (l - r) * (l - r);
        el += l;
        er += r;
    }
    stop(&x);
    CHECK(worst < 1e-5 && cross > 0.01 * el * el / 900.0,
          "Tremolo Panner (Depth 80 %%): L + R = 2 (1 - d/2) x the input within %.1e, L and R move against each other",
          worst);
}

void test_modfx(void)
{
    const struct wvfx_desc *const blocks[] = { &flanger_block, &phaser_block, &rotary_block, &vibe_block, &tremolo_block };
    for (unsigned i = 0; i < sizeof blocks / sizeof blocks[0]; i++) {
        test_descriptor(blocks[i]);
        test_types(blocks[i]);
        test_bypass(blocks[i]);
        test_knobs(blocks[i]);
    }
    test_panner();
}
