/*
 * waveod's circuit blocks (docs/drives.md) on the PC: what the firmware relies on. Their sound against the circuits
 * is tools/check_drives.py's (the reference simulator, the ARM build).
 *
 *   - the descriptors: ids in DRV, defaults in range, knobs not shared, the workspace in the buffer, a poll;
 *   - a tone at 0.5 and at full scale: finite, not silent, mono (L = R);
 *   - poll: nothing to do with the same knobs; a moved knob builds into the other matrix set and flips to it;
 *   - bypassed from the start: the input, bit for bit; switched off: one block of crossfade, then the input.
 */
#include "test.h"
#include "sdk/wvfx.h"
#include "sdk/wvdk.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern const struct wvfx_desc dark_muff_block, dark_muff_lite_block, green_drive_block, rodent_dist_block,
    round_fuzz_block, treble_boost_block;

static const struct wvfx_desc *const drives[] = {
    &dark_muff_block, &dark_muff_lite_block, &green_drive_block, &rodent_dist_block, &round_fuzz_block,
    &treble_boost_block,
};

#define FRAMES 64

struct inst {
    const struct wvfx_desc *d;
    void *state, *buffer;
    float knobs[WVFX_KNOBS];
    long phase;
};

static void start(struct inst *x, const struct wvfx_desc *d)
{
    x->d = d;
    x->state = calloc(1, d->state_size);
    x->buffer = calloc(1, d->buffer_size ? d->buffer_size : 1);
    memset(x->knobs, 0, sizeof x->knobs);
    for (int i = 0; i < d->nparams; i++)
        x->knobs[d->params[i].knob] = d->params[i].def;
    struct wvfx_env env = { WVFX_ABI, 44100.0f, FRAMES, 0, 0, 0, 0 };
    x->phase = 0;
    d->init(x->state, x->buffer, &env, x->knobs);
}

static void stop(struct inst *x)
{
    free(x->state);
    free(x->buffer);
}

/* one block of a 220 Hz tone at `level` through the block: in[] keeps the input */
static void block(struct inst *x, float level, int enabled, float *io, float *in)
{
    for (int f = 0; f < FRAMES; f++) {
        float v = level * sinf(6.2831853f * 220.0f * (float)(x->phase + f) / 44100.0f);
        io[2 * f] = in[2 * f] = v;
        io[2 * f + 1] = in[2 * f + 1] = 0.5f * v;
    }
    x->phase += FRAMES;
    float ret[2 * FRAMES] = { 0 }, send[2 * FRAMES];
    struct wvfx_io w = { io, in, send, ret, x->knobs, 500, FRAMES, (uint8_t)enabled, 0 };
    x->d->process(x->state, &w);
}

static void test_one(const struct wvfx_desc *d)
{
    CHECK(d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && d->menu == WVFX_DRV && WVFX_IS_ID(d->id) &&
          ((d->id >> 16) & 0xff) == WVFX_DRV, "%s: descriptor (%#x)", d->name, (unsigned)d->id);
    unsigned used = 0;
    int ok = 1;
    for (int i = 0; i < d->nparams; i++) {
        const struct wvfx_param *p = &d->params[i];
        ok &= p->def >= p->min && p->def <= p->max && p->knob < WVFX_KNOBS && !(used & (1u << p->knob));
        used |= 1u << p->knob;
    }
    CHECK(ok && d->nparams > 0, "%s: %d parameters, defaults in range, knobs not shared", d->name, d->nparams);
    CHECK(d->buffer_size == sizeof(dk_work) && d->poll && d->state_size > 0,
          "%s: state %u bytes, the workspace in the buffer (%u)", d->name, (unsigned)d->state_size,
          (unsigned)d->buffer_size);

    struct inst x;
    float io[2 * FRAMES], in[2 * FRAMES];
    start(&x, d);
    double sum = 0.0;
    int finite = 1, mono = 1;
    for (int b = 0; b < 200; b++) {
        block(&x, b < 100 ? 0.5f : 1.0f, 1, io, in);
        for (int i = 0; i < 2 * FRAMES; i++) {
            finite &= isfinite(io[i]) && fabsf(io[i]) < 8.0f;
            if (b >= 20 && b < 100)
                sum += (double)io[i] * io[i];
        }
        for (int f = 0; f < FRAMES; f++)
            mono &= io[2 * f] == io[2 * f + 1];
    }
    double rms = sqrt(sum / (80.0 * 2 * FRAMES));
    CHECK(finite && mono && rms > 1e-3, "%s: a tone at 0.5 and full scale: finite, mono, rms %.4f", d->name, rms);

    int none = d->poll(x.state, x.knobs);
    const struct wvfx_param *p0 = &d->params[0];
    x.knobs[p0->knob] = p0->def > 50.0f ? p0->def - 30.0f : p0->def + 30.0f;
    uint32_t before = *(const uint32_t *)x.state;          /* struct drive comes first: cur */
    int built = d->poll(x.state, x.knobs);
    uint32_t after = *(const uint32_t *)x.state;
    block(&x, 0.5f, 1, io, in);
    finite = 1;
    for (int i = 0; i < 2 * FRAMES; i++)
        finite &= isfinite(io[i]);
    CHECK(none == 0 && built == 1 && after == (before ^ 1u) && finite,
          "%s: poll: nothing with the same knobs, %s moved: set %u -> %u", d->name, p0->name, (unsigned)before,
          (unsigned)after);

    block(&x, 0.5f, 0, io, in);                             /* switched off: the crossfade block */
    int ramp = memcmp(io, in, sizeof io) != 0;
    block(&x, 0.5f, 0, io, in);
    CHECK(ramp && memcmp(io, in, sizeof io) == 0, "%s: switched off: one block of crossfade, then the input", d->name);
    stop(&x);

    start(&x, d);
    int same = 1;
    for (int b = 0; b < 5; b++) {
        block(&x, 0.5f, 0, io, in);
        same &= memcmp(io, in, sizeof io) == 0;
    }
    CHECK(same, "%s: bypassed from the start: the input, bit for bit", d->name);
    stop(&x);
}

void test_drives(void)
{
    for (unsigned i = 0; i < sizeof drives / sizeof drives[0]; i++)
        test_one(drives[i]);
    uint32_t ids = 0;
    for (unsigned i = 0; i < sizeof drives / sizeof drives[0]; i++)
        ids |= 1u << (drives[i]->id & 31);
    CHECK(__builtin_popcount(ids) == (int)(sizeof drives / sizeof drives[0]), "the circuit blocks' ids are distinct");
}
