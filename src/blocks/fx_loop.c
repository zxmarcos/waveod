/*
 * FX Loop, SND and RTN (LOOP, SND and RTN menus): the effects loop through the FX SEND jack and the FX RETURN
 * input, waveod's own code in place of the original's modules (WVFX_REPLACE: a slot keeps the original's id,
 * name and knobs, so a preset plays on either firmware). The knobs mean what they mean in the original:
 *
 *   Send Level    0 .. 100 %: the chain at this point, x the level, onto FX SEND
 *   Return Level  0 .. 100 %: FX RETURN x the level
 *   Mix           0 .. 100: dry = min(1, 2 (1 - mix)), return = min(1, 2 mix), as the original: 0 = the dry
 *                 signal, 50 = both at full level (a parallel loop), 100 = the return alone (a series loop)
 * and waveod adds, after them:
 *   Return Phase  Normal / Invert: the return's polarity, for a parallel loop through a pedal that inverts it
 *
 * SND has Send Level only and passes the signal; RTN has Return Level and Mix (and Return Phase) and sends
 * nothing; FX Loop has all. What differs from the original:
 *   - every gain moves over one block (knobs and the on / off switch), so nothing clicks;
 *   - the send is added to the FX send the engine zeroes every block, so two send blocks mix there instead of
 *     the last one winning, and a bypassed block leaves it alone instead of clearing it;
 *   - the send is the mono sum of the chain, (L + R) / 2, not its left channel (the same for a mono chain).
 * Bypassed: the dry signal, nothing sent.
 */
#include "sdk/wvfx.h"

enum { HAS_SEND = 1, HAS_RETURN = 2 };

/* the parameters after the original entry's: FX Loop's knobs 0..2 = Send, Return, Mix; RTN's 0..1 = Return, Mix */
static const struct wvfx_param loop_params[] = {
    { "Return Phase", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0, 0, 0, "Normal\0Invert\0", 0 },
};

static const struct wvfx_param return_params[] = {
    { "Return Phase", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0, 0, 0, "Normal\0Invert\0", 0 },
};

struct gains {
    float send, dry, ret;
};

struct state {
    struct gains g;                 /* where the last block ended */
};

/* the gains the knobs ask for: Send Level first if there is one, then Return Level, Mix, Return Phase */
static struct gains target(const float *k, int has, int enabled)
{
    struct gains t = { 0.0f, 1.0f, 0.0f };
    if (!enabled)
        return t;
    if (has & HAS_SEND)
        t.send = wvfx_clamp(*k++, 0.0f, 100.0f) * 0.01f;
    if (has & HAS_RETURN) {
        float level = wvfx_clamp(k[0], 0.0f, 100.0f) * 0.01f, mix = wvfx_clamp(k[1], 0.0f, 100.0f) * 0.01f;
        t.dry = wvfx_clamp(2.0f * (1.0f - mix), 0.0f, 1.0f);
        t.ret = wvfx_clamp(2.0f * mix, 0.0f, 1.0f) * level * (k[2] >= 0.5f ? -1.0f : 1.0f);
    }
    return t;
}

static void run(struct state *s, const struct wvfx_io *io, int has)
{
    struct gains to = target(io->knobs, has, io->enabled), g = s->g;
    float ds = wvfx_ramp_step(g.send, to.send, io->frames), dd = wvfx_ramp_step(g.dry, to.dry, io->frames);
    float dr = wvfx_ramp_step(g.ret, to.ret, io->frames);
    s->g = to;
    float *x = io->io, *send = io->send;
    const float *ret = io->ret;
    int sending = send && (g.send != 0.0f || to.send != 0.0f);
    int mixing = g.dry != 1.0f || to.dry != 1.0f || g.ret != 0.0f || to.ret != 0.0f;
    for (int f = 0; f < io->frames; f++, x += 2) {
        g.send += ds;
        g.dry += dd;
        g.ret += dr;
        if (sending) {
            float m = (x[0] + x[1]) * 0.5f * g.send;
            send[2 * f] += m;
            send[2 * f + 1] += m;
        }
        if (mixing) {
            float rl = ret ? ret[2 * f] : 0.0f, rr = ret ? ret[2 * f + 1] : 0.0f;    /* no return: silence */
            x[0] = x[0] * g.dry + rl * g.ret;
            x[1] = x[1] * g.dry + rr * g.ret;
        }
    }
}

#define LOOP_BLOCK(sym, mn, nm, ps, np, has)                                                                   \
    static int sym##_init(void *state, void *buffer, const struct wvfx_env *env, const float *k)              \
    {                                                                                                          \
        (void)buffer;                                                                                          \
        (void)env;                                                                                             \
        ((struct state *)state)->g = target(k, has, 1);                                                        \
        return 0;                                                                                              \
    }                                                                                                          \
    static void sym##_process(void *state, const struct wvfx_io *io) { run(state, io, has); }                 \
    const struct wvfx_desc sym = {                                                                             \
        .magic = WVFX_MAGIC, .abi = WVFX_ABI, .id = WVFX_ID(mn, 1), .menu = mn, .nparams = np,                 \
        .flags = WVFX_REPLACE | WVFX_INHERIT, .name = nm, .category = "Routing", .params = ps,                 \
        .state_size = sizeof(struct state), .buffer_size = 0, .init = sym##_init, .process = sym##_process,   \
        .poll = 0,                                                                                             \
    }

LOOP_BLOCK(fx_loop_block, WVFX_LOOP, "FX Loop", loop_params, 1, HAS_SEND | HAS_RETURN);
LOOP_BLOCK(fx_send_block, WVFX_SND, "SND", 0, 0, HAS_SEND);
LOOP_BLOCK(fx_return_block, WVFX_RTN, "RTN", return_params, 1, HAS_RETURN);
