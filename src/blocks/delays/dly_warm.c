/*
 * Warm (DELAY): the original's Warm delay (QME-200 V1.2.2, library entry 9.0, id 0x0B000001) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A darker stereo delay: each input plus Feedback x its line's last
 * repeat, clipped to +-1.7, into a 4 s line read Time back (between two samples), through a 2nd-order low pass (its
 * output is the repeat fed back) and a second one (the wet signal). Single precision; the time in double.
 *
 * Out, glides, Sync and Trail as the Pure's (`dly_pure.c`); clearing also clears the filters. (Bypassed with Trail at
 * neither 0 nor 1, which the knob cannot give, the original repeats its last output; here the signal passes.)
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

static const struct sos LP1 = {
    0.01730719581246376f, 0.03461439162492752f, 0.01730719581246376f, 1.0f, -1.6475516557693481f, 0.7167804837226868f,
};
static const struct sos LP2 = {
    0.03478603810071945f, 0.0695720762014389f, 0.03478603810071945f, 1.0f, -1.4075053930282593f, 0.5466495156288147f,
};

struct channel {
    struct dly_line line;
    struct sos_state lp1, lp2;
    float repeat;                       /* LP1's last output: what goes back in */
};

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct channel l, r;
};

/* one channel: in, clipped, through the line and both low passes */
static float channel_run(struct channel *c, float in, float len)
{
    dly_write(&c->line, dly_hold(in, 1.7000000476837158f));
    float r = dly_read(&c->line, (int)len, len - __builtin_truncf(len));          /* VRINTZ */
    c->repeat = sos_run(&LP1, &c->lp1, r);
    return sos_run(&LP2, &c->lp2, c->repeat);
}

static void clear(struct state *s)
{
    float *buf = s->l.line.buf;
    s->l = (struct channel){ { buf, 0 } };
    s->r = (struct channel){ { buf + DLY_LINE, 0 } };
    __builtin_memset(buf, 0, DLY_BUFFER);
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
    clear(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time_d(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = dly_time_d(k[1], k[3], io->beat_ms);
    if (io->frames == 0)
        return;
    float df = (k[2] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix += dm;
        float xl = p[0], xr = p[1];
        s->feedback += df;
        float glided = s->len + dl;
        s->len = k[3] == 1.0f ? target : glided;
        float l, r;
        if (io->enabled) {
            l = channel_run(&s->l, st_fma(s->l.repeat, s->feedback, xl), s->len);
            r = channel_run(&s->r, st_fma(s->r.repeat, s->feedback, xr), s->len);
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            l = channel_run(&s->l, s->l.repeat * s->feedback, s->len);
            r = channel_run(&s->r, s->r.repeat * s->feedback, s->len);
            if (s->mix >= 0.5f) {
                p[0] = l + xl;
                p[1] = r + xr;
                continue;
            }
        } else {
            if (k[4] == 0.0f && !s->cleared)
                clear(s);
            continue;
        }
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            p[0] = st_fma(m, l, xl);
            p[1] = st_fma(m, r, xr);
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l);
            p[1] = st_fma(xr + xr, m, r);
        }
    }
}

const struct wvfx_desc dly_warm_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x100),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Warm",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
