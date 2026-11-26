/*
 * Tube (DELAY): the original's Tube delay (QME-200 V1.2.2, library entry 9.3, id 0x0B00000B) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A stereo delay whose repeats feed back unfiltered (each input plus
 * Feedback x the line's last read, clipped to +-1.7) and reach the output through two 2nd-order low passes. Single
 * precision.
 *
 * Out: below Mix 0.5, x + Mix wet; from 0.5, wet / 2 + 2 (1 - Mix) x (bypassed with Trail on: wet / 2 + x). Glides,
 * Sync and Trail as the Pure's (`dly_pure.c`).
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 30.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

static const struct sos LP1 = {
    0.017840545624494553f, 0.035681091248989105f, 0.017840545624494553f, 1.0f, -1.6983237266540527f,
    0.7696859240531921f,
};
static const struct sos LP2 = {
    0.03918171674013138f, 0.07836343348026276f, 0.03918171674013138f, 1.0f, -1.5853623151779175f, 0.7420892119407654f,
};

struct channel {
    struct dly_line line;
    struct sos_state lp1, lp2;
    float repeat;                       /* the line's last read: what goes back in */
};

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct channel l, r;
};

static inline float clip17(float x)
{
    return dly_hold(x, 1.7000000476837158f);
}

static float channel_run(struct channel *c, float in, float len)
{
    dly_write(&c->line, clip17(in));
    c->repeat = dly_read(&c->line, (int)len, len - (float)(int)len);
    return sos_run(&LP2, &c->lp2, sos_run(&LP1, &c->lp1, c->repeat));
}

static void clear(struct state *s)
{
    s->l.line.w = s->r.line.w = 0;
    s->l.repeat = s->r.repeat = 0.0f;
    __builtin_memset(s->l.line.buf, 0, DLY_BUFFER);
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[3];
    s->l.line.buf = buffer;
    s->r.line.buf = (float *)buffer + DLY_LINE;
    clear(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = dly_time(k[1], k[3], io->beat_ms);
    if (io->frames == 0)
        return;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float df = (k[2] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float glided = s->len + dl;
        s->mix += dm;
        s->feedback += df;
        s->len = k[3] == 1.0f ? target : glided;
        float xl = p[0], xr = p[1], l, r;
        if (io->enabled) {
            l = channel_run(&s->l, st_fma(s->l.repeat, s->feedback, xl), s->len);
            r = channel_run(&s->r, st_fma(s->r.repeat, s->feedback, xr), s->len);
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            l = channel_run(&s->l, s->l.repeat * s->feedback, s->len);
            r = channel_run(&s->r, s->r.repeat * s->feedback, s->len);
        } else {
            if (k[4] == 0.0f && !s->cleared)
                clear(s);
            continue;
        }
        if (s->mix < 0.5f) {
            p[0] = st_fma(l, s->mix, xl);
            p[1] = st_fma(r, s->mix, xr);
        } else if (!io->enabled) {
            p[0] = l * 0.5f + xl;
            p[1] = r * 0.5f + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l * 0.5f);
            p[1] = st_fma(xr + xr, m, r * 0.5f);
        }
    }
}

const struct wvfx_desc dly_tube_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x103),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Tube",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
