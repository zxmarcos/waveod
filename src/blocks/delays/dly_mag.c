/*
 * Mag (DELAY): the original's Mag delay (QME-200 V1.2.2, library entry 9.2, id 0x0B000002) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A tape-style stereo delay: each input first goes through a short
 * wow line (618 samples, read 352 + 265 |sin(4.274e-05 n)| samples back, n counting to 73500), the input and that
 * read averaged, plus Feedback x the channel's last repeat, into a 4 s line read Time back (between two samples),
 * through a 2nd-order low pass and a 1st-order high pass (the repeat). Single precision; the wow phase in double.
 *
 * Out: below Mix 0.5, x + 2 Mix repeat; from 0.5, repeat + 2 (1 - Mix) x. Glides, Sync and Trail as the Pure's
 * (`dly_pure.c`; bypassed with Trail on, the wow line stands still and the lines run on their feedback alone; a clear
 * also restarts the wow).
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

#define WOW 618
#define BUFFER (DLY_BUFFER + 2 * WOW * (int)sizeof(float))

static const struct sos LP = {
    0.01730719581246376f, 0.03461439162492752f, 0.01730719581246376f, 1.0f, -1.6475516557693481f, 0.7167804837226868f,
};
static const struct fos HP = { 0.9929264783859253f, -0.9929264783859253f, -0.9858529567718506f };

struct channel {
    struct dly_line line;
    float *wow;
    int ww;
    float n;                            /* the wow LFO's sample count */
    struct sos_state lp;
    struct fos_state hp;
    float repeat;
};

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct channel l, r;
};

/* the wow line: write x, read it (352 + 265 |sin|) samples back; the average of x and that read */
static float wow_run(struct channel *c, float x)
{
    float s = st_sinf((float)((double)c->n * 4.27427572e-05));
    float next = c->n + 1.0f;
    c->n = 73500.0f > next ? next : 1.0f;
    int old = c->ww;
    c->wow[old] = x;
    c->ww = old > WOW - 2 ? 0 : old + 1;
    float d = st_abs(s) * 265.0f, whole = __builtin_truncf(d);                     /* VRINTZ */
    int i = (int)((float)(old - 352) - whole);
    if (i < 0)
        i += WOW;
    int older = i < 1 ? i + WOW - 1 : i - 1;
    float a = c->wow[i];
    return st_fma(c->wow[older] - a, d - whole, a + x);
}

static float line_run(struct channel *c, float in, float len)
{
    dly_write(&c->line, in);
    float y = dly_read(&c->line, (int)len, len - __builtin_truncf(len));
    c->repeat = fos_run(&HP, &c->hp, sos_run(&LP, &c->lp, y));
    return c->repeat;
}

static void clear(struct state *s)
{
    s->l.repeat = s->r.repeat = 0.0f;
    s->l.n = s->r.n = 0.0f;
    s->l.ww = s->r.ww = 0;
    __builtin_memset(s->l.line.buf, 0, BUFFER);
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    float *b = buffer;
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[3];
    s->l.line.buf = b;
    s->r.line.buf = b + DLY_LINE;
    s->l.wow = b + 2 * DLY_LINE;
    s->r.wow = b + 2 * DLY_LINE + WOW;
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
        s->feedback += df;
        s->mix += dm;
        float glided = s->len + dl;
        s->len = k[3] == 1.0f ? target : glided;
        float xl = p[0], xr = p[1], l, r;
        if (io->enabled) {
            float wl = wow_run(&s->l, xl), wr = wow_run(&s->r, xr);
            l = line_run(&s->l, st_fma(s->l.repeat, s->feedback, wl * 0.5f), s->len);
            r = line_run(&s->r, st_fma(wr, 0.5f, s->r.repeat * s->feedback), s->len);
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            l = line_run(&s->l, s->l.repeat * s->feedback, s->len);
            r = line_run(&s->r, s->r.repeat * s->feedback, s->len);
        } else {
            if (k[4] == 0.0f && !s->cleared)
                clear(s);
            continue;
        }
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            p[0] = st_fma(m, l, xl);
            p[1] = st_fma(m, r, xr);
        } else if (!io->enabled && (int)k[4] == 1) {
            p[0] = l + xl;
            p[1] = r + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l);
            p[1] = st_fma(xr + xr, m, r);
        }
    }
}

const struct wvfx_desc dly_mag_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x102),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Mag",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
