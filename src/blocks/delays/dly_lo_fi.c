/*
 * Lo-Fi (DELAY): the original's Lo-Fi delay (QME-200 V1.2.2, library entry 9.13, id 0x0B000026) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A stereo delay with a degraded repeat: each input plus Feedback x
 * the channel's last read into a 4 s line read Time back (between two samples); the read through a sample rate
 * reduction (held, and taken anew moved back toward the held value, whenever the channel's counter has run K = trunc(2
 * + 62 pow3(Crush)) samples), a bit reduction (2^n - 1 steps each side, n = round(8 + 8 Bit')) and a 2nd-order filter:
 * the wet. Bit' = 1 - Bit. Single precision; the rounding in double.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x (bypassed with Trail on: wet + x). Mix, Feedback,
 * Crush (by the taper), Bit' and the time glide 0.00022676 of the way to the knobs each sample (with Sync on the time
 * jumps). Bypassed with Trail on the lines run on their feedback alone; with Trail at 0 they are cleared once (the
 * reduction and the filter run on) and the signal passes. The Init reads the tempo: here the first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x15, 0, 0, 0 },
    { "Feedback", -100.0f, 100.0f, 1.0f, 20.0f, 2, 0x1d, 0x00, 0, 0, 0 },
    { "Crush", 0.0f, 100.0f, 1.0f, 20.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Bit", 0.0f, 100.0f, 1.0f, 20.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 7, 0x00, 0x00, 0, 0, 0 },
};

static const float NUM[3] = { 0.016818728297948837f, 0.033637456595897675f, 0.016818728297948837f };
static const float DEN[2] = { -1.6010522842407227f, 0.6683272123336792f };

struct channel {
    struct dly_line line;
    float repeat;                       /* the line's last read */
    float held, count;                  /* the rate reduction */
    float x1, x2, y1, y2;               /* the filter */
};

struct state {
    float mix, feedback, crush, bit, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct channel l, r;
};

/* the rate reduction: c->repeat the new read, k the hold length */
static float hold(struct channel *c, float k)
{
    float n = c->count - k;
    if (n >= 0.0f) {
        c->held = st_fma(c->held - c->repeat, n, c->repeat);
        c->count = n;
    }
    c->count += 1.0f;
    return c->held;
}

/* the bit reduction to 2^n - 1 steps each side */
static float crush_bits(float x, float bit)
{
    int n = (int)((double)st_fma(bit, 8.0f, 8.0f) + 0.5);
    float q = __builtin_truncf(st_pow2i(n) + -1.0f);                                  /* VRINTZ */
    float v = (float)(uint32_t)(int32_t)((double)(st_abs(x) * q) + 0.5) / q;
    return x >= 0.0f ? v : -v;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->bit = st_fma(-k[4], 0.01f, 1.0f);                                             /* VFNMA */
    s->feedback = k[2] * 0.01f;
    s->crush = st_taper(st_pow3, k[3]);
    s->time0 = k[1];
    s->sync0 = k[6];
    s->l.line.buf = buffer;
    s->r.line.buf = (float *)buffer + DLY_LINE;
    __builtin_memset(buffer, 0, DLY_BUFFER);
    s->cleared = 1;
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
    float target = dly_time_d(k[1], k[6], io->beat_ms);
    if (io->frames == 0)
        return;
    const float c = 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * c;
    float df = (k[2] * 0.01f - s->feedback) * c;
    float dl = (target - s->len) * c;
    float dk = (st_taper(st_pow3, k[3]) - s->crush) * c;
    float db = st_fma(-(k[4] * 0.01f + s->bit), c, c);       /* (1 - Bit - bit) c, folded (VFMS) */
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float xl = p[0], xr = p[1];
        float glided = s->len + dl;
        s->feedback += df;
        s->mix += dm;
        s->bit += db;
        s->crush += dk;
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
                s->l.repeat = s->r.repeat = 0.0f;
                s->l.line.w = s->r.line.w = 0;
                __builtin_memset(s->l.line.buf, 0, DLY_BUFFER);
                s->cleared = 1;
            }
            continue;
        }
        int whole = (int)s->len;
        float frac = s->len - __builtin_truncf(s->len);                              /* VRINTZ */
        dly_write(&s->l.line, il);
        s->l.repeat = dly_read(&s->l.line, whole, frac);
        dly_write(&s->r.line, ir);
        s->r.repeat = dly_read(&s->r.line, whole, frac);
        float kk = __builtin_truncf(st_fma(s->crush, 62.0f, 2.0f));
        float ql = crush_bits(hold(&s->l, kk), s->bit), qr = crush_bits(hold(&s->r, kk), s->bit);
        struct channel *a = &s->l, *b = &s->r;
        float yl = st_fma(ql, NUM[0], st_fma(NUM[2], a->x2, NUM[1] * a->x1)) - st_fma(DEN[1], a->y2, DEN[0] * a->y1);
        float yr = st_fma(b->x2, NUM[2], st_fma(b->x1, NUM[1], qr * NUM[0])) - st_fma(b->y2, DEN[1], b->y1 * DEN[0]);
        a->x2 = a->x1;
        a->x1 = ql;
        a->y2 = a->y1;
        a->y1 = yl;
        b->x2 = b->x1;
        b->x1 = qr;
        b->y2 = b->y1;
        b->y1 = yr;
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            p[0] = st_fma(m, yl, xl);
            p[1] = st_fma(m, yr, xr);
        } else if ((int)k[7] == 1 && !io->enabled) {
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

const struct wvfx_desc dly_lo_fi_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10D),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Lo-Fi",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
