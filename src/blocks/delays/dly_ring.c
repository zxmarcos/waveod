/*
 * Ring (DELAY): the original's Ring delay (QME-200 V1.2.2, library entry 9.8, id 0x0B000009) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A stereo delay with a ring modulator in its repeats: each input plus
 * Feedback x the channel's last repeat (clipped to +-1.7) into a 4 s line read Time back (between two samples); the
 * read a ring modulated, a + (a cos(2 pi f n) - a) Ring Mix (f = (100 + 2000 Freq^3) / 44100 per sample, n counting
 * samples up to 1 / f, rescaled when f moves so the phase runs on), is the repeat; through a 2nd-order low pass set by
 * Tone (a bilinear design from tan), the wet. Single precision; the time in double.
 *
 * Out: below Dly Mix 0.5, x + 2 Dly Mix wet; from 0.5, wet + 2 (1 - Dly Mix) x (bypassed with Trail on: wet + x).
 * Dly Mix, Feedback, Freq, Tone and the time glide 0.00022676 of the way to the knobs each sample (with Sync on the
 * time jumps; the low pass is recomputed while Tone moves); Ring Mix acts at once (the original glides a copy it
 * never reads). Bypassed with Trail on the lines run on their feedback alone; with Trail at 0 they are cleared once
 * (the filters and the modulator run on) and the signal passes. The Init reads the tempo: here the first process
 * call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Dly Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 2, 0x19, 0x16, 0, 0, 0 },
    { "Ring Mix", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Freq", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 7, 0x00, 0x00, 0, 0, 0 },
};

struct channel {
    struct dly_line line;
    float repeat;                       /* the last ring modulated read */
    float n, f;                         /* the modulator's sample count and the frequency it was counted at */
    float x[3], y[3];                   /* the low pass */
};

struct state {
    float mix, feedback, freq, tone, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    float b0, b1, b2, a1, a2;
    struct channel l, r;
};

static void lowpass_set(struct state *s, float w)
{
    float t = st_tanf(w), t2 = t * t, h = t2 * 0.7070000171661377f;
    float r = 1.0f / ((t + 0.7070000171661377f) + h);
    s->b0 = s->b2 = r * h;
    s->b1 = s->b0 + s->b0;
    s->a1 = r * st_fma(t2, 1.4140000343322754f, -1.4140000343322754f);
    s->a2 = r * ((0.7070000171661377f - t) + h);
}

/* x held at +-1.7 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static float clip(float x)
{
    return __builtin_fminf(__builtin_fmaxf(x, -1.7000000476837158f), 1.7000000476837158f);
}

/* the read ring modulated (f the frequency per sample, rf its reciprocal, w = 2 pi f) */
static float ring(struct channel *c, float a, float f, float rf, float w, float mix)
{
    if (c->f != f) {
        c->n = (c->n * c->f) / f;
        c->f = f;
    }
    float next = c->n + 1.0f, phase = c->n * w;
    c->n = next - (next > rf ? rf : 0.0f);
    return st_fma(st_fma(st_cosf(phase), a, -a), mix, a);                            /* VFNMS */
}

static float lowpass(const struct state *s, struct channel *c, float x)
{
    float n = st_fma(s->b2, c->x[2], st_fma(s->b1, c->x[1], s->b0 * x));
    float d = st_fma(s->a2, c->y[2], s->a1 * c->y[1]);
    float y = n - d;
    c->x[2] = c->x[1];
    c->x[1] = c->x[0] = x;
    c->y[2] = c->y[1];
    c->y[1] = c->y[0] = y;
    return y;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[1] * 0.01f;
    s->freq = k[4] * 0.01f;
    s->tone = k[5] * 0.01f;
    s->time0 = k[2];
    s->sync0 = k[6];
    s->l.line.buf = buffer;
    s->r.line.buf = (float *)buffer + DLY_LINE;
    __builtin_memset(buffer, 0, DLY_BUFFER);
    s->cleared = 1;
    lowpass_set(s, st_fma(k[5], 0.003205706598237157f, 0.035618964582681656f));
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
    float target = dly_time_d(k[2], k[6], io->beat_ms), tone = k[5] * 0.01f, ring_mix = k[3] * 0.01f;
    if (io->frames == 0)
        return;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float df = (k[1] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float dq = (k[4] * 0.01f - s->freq) * 0.0002267600066261366f;
    float dt = (tone - s->tone) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float xl = p[0], xr = p[1];
        if (s->tone != tone) {
            s->tone += dt;
            lowpass_set(s, st_fma(s->tone, 0.3205706775188446f, 0.035618964582681656f));
        }
        s->mix += dm;
        s->feedback += df;
        float glided = s->len + dl;
        s->freq += dq;
        s->len = k[6] == 1.0f ? target : glided;
        float il, ir;
        if (io->enabled) {
            il = st_fma(s->l.repeat, s->feedback, xl);
            ir = st_fma(s->r.repeat, s->feedback, xr);
        } else if (k[7] == 1.0f) {
            il = s->l.repeat * s->feedback;
            ir = s->r.repeat * s->feedback;
        } else {
            if (k[7] == 0.0f && !s->cleared) {
                s->l.line.w = s->r.line.w = 0;
                s->l.repeat = s->r.repeat = 0.0f;
                __builtin_memset(s->l.line.buf, 0, DLY_BUFFER);
                s->cleared = 1;
            }
            continue;
        }
        int whole = (int)s->len;
        float frac = s->len - (float)whole;
        dly_write(&s->l.line, clip(il));
        dly_write(&s->r.line, clip(ir));
        float al = dly_read(&s->l.line, whole, frac), ar = dly_read(&s->r.line, whole, frac);
        float q = s->freq, f = st_fma((q * q) * 0.04535147175192833f, q, 0.0022675737272948027f);
        float rf = 1.0f / f, w = f * 6.2831854820251465f;
        s->l.repeat = ring(&s->l, al, f, rf, w, ring_mix);
        s->r.repeat = ring(&s->r, ar, f, rf, w, ring_mix);
        float yl = lowpass(s, &s->l, s->l.repeat), yr = lowpass(s, &s->r, s->r.repeat);
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            p[0] = st_fma(m, yl, xl);
            p[1] = st_fma(m, yr, xr);
        } else if (!io->enabled) {
            p[0] = yl + xl;
            p[1] = yr + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, yl);
            p[1] = st_fma(xr + xr, m, yr);
        }
        if (io->enabled)
            s->cleared = 0;
    }
}

const struct wvfx_desc dly_ring_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x108),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Ring",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
