/*
 * Multi Tape (DELAY): the original's Multi Tape delay (QME-200 V1.2.2, library entry 9.9, id 0x0B00000C) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). A multi-head tape echo, mono (the left input; both
 * outputs the same): x / 2 into a 4 s tape read by four heads at 1/4, 2/4, 3/4 and 4/4 of Time (each read moved
 * toward the head's previous read by the time's fraction) through the same 2nd-order low pass (Tone: 1000 + 40 Tone^2
 * Hz, a bilinear design from tan); Mode picks which heads sound (1: head 2; 2-7: pairs averaged; 8-11: three x 0.33;
 * 12: all four / 4). That sum, 351 samples later and through a short wow line (read 10 + d (0.5 + 0.5 sin) samples
 * back, d 56.5, 96.05 or 28.25 by the mode, the sine's period 17640 samples), plus Feedback x the last repeat (clipped
 * to +-1.7) goes into a second 4 s line read Time / 2 back and through the low pass again: the repeat. Wet = heads +
 * repeat / 2. Single precision; the filter's frequency and the wow phase in double.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x (bypassed with Trail on: wet + x). Mix, Feedback
 * and the time glide 0.00022676 of the way to the knobs each sample (with Sync on the time jumps). Bypassed with
 * Trail on the heads run on silence; with Trail at 0 the lines are cleared once (the filters, the wow and the repeat
 * run on) and the signal passes (the original leaves the last output there for other Trail values: here it passes).
 * The Init reads the tempo: here the first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x15, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 30.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Mode", 1.0f, 12.0f, 1.0f, 5.0f, 4, 0x1a, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

#define PRE 352                         /* the 351-sample pre-delay ring */
#define WOW 133
#define BUFFER (DLY_BUFFER + (PRE + WOW) * (int)sizeof(float))

struct biquad {
    float x[3], y[3];
};

struct state {
    float mix, feedback, len, tone;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    float b0, b1, b2, a1, a2;
    struct dly_line tape, line;         /* the heads' tape, the repeat's line */
    float prev[4];                      /* the heads' previous reads */
    struct biquad head[4], rep;
    float heads, repeat;                /* the heads' sum, the repeat (the feedback) */
    float wow_depth, depth_word;        /* the wow line's depth; the original's word for it (0.5, 0.85, 0.25) */
    float n;                            /* the wow sine's sample count */
    float *pre, *wow;
    int pw, pr, ww;
};

static void lowpass_set(struct state *s, float tone_knob, float tone)
{
    float t = st_tanf((float)((double)st_fma(tone_knob * 40.0f, tone, 1000.0f) * 7.12379286e-05));
    float t2 = t * t, h = t2 * 0.8071067929267883f, r = 1.0f / ((t + 0.8071067929267883f) + h);
    s->b0 = s->b2 = r * h;
    s->b1 = s->b0 + s->b0;
    s->a1 = r * st_fma(t2, 1.6142135858535767f, -1.6142135858535767f);
    s->a2 = r * ((0.8071067929267883f - t) + h);
}

static float biquad_run(const struct state *s, struct biquad *f, float x)
{
    float n = st_fma(s->b2, f->x[2], st_fma(s->b1, f->x[1], s->b0 * x));
    float d = st_fma(s->a2, f->y[2], s->a1 * f->y[1]);
    float y = n - d;
    f->x[2] = f->x[1];
    f->x[1] = f->x[0] = x;
    f->y[2] = f->y[1];
    f->y[1] = f->y[0] = y;
    return y;
}

/* the read whole samples back, without the step to the older sample */
static float tape_read(const struct dly_line *l, int whole)
{
    int i = l->w - whole;
    if (i < 0)
        i += DLY_LINE;
    return l->buf[i];
}

/* the heads' sum for Mode (m = Mode - 1), and the wow depth that goes with it */
static float heads_mix(struct state *s, float m, const float *y)
{
    float out, word;
    if (m == 0.0f) {
        out = y[1];
        word = 0.5f;
    } else if (m == 1.0f || m == 2.0f || m == 3.0f || m == 4.0f || m == 5.0f || m == 6.0f) {
        static const int A[6] = { 1, 2, 3, 2, 3, 3 }, B[6] = { 0, 1, 2, 0, 1, 0 };
        int i = (int)m - 1;
        out = (y[A[i]] + y[B[i]]) * 0.5f;
        word = 0.5f;
    } else if (m == 7.0f) {
        out = ((y[1] + y[0]) + y[2]) * 0.33000001311302185f;
        word = 0.8500000238418579f;
    } else if (m == 8.0f) {
        out = ((y[2] + y[1]) + y[3]) * 0.33000001311302185f;
        word = 0.5f;
    } else if (m == 9.0f) {
        out = ((y[1] + y[0]) + y[3]) * 0.33000001311302185f;
        word = 0.5f;
    } else if (m == 10.0f) {
        out = ((y[2] + y[0]) + y[3]) * 0.33000001311302185f;
        word = 0.25f;
    } else if (m == 11.0f) {
        out = (((y[1] + y[0]) + y[2]) + y[3]) * 0.25f;
        word = 0.8500000238418579f;
    } else {                            /* no mode: silence, the depth from the last word */
        s->wow_depth = s->depth_word * 113.0f;
        return 0.0f;
    }
    s->depth_word = word;
    s->wow_depth = word == 0.5f ? 56.5f : word == 0.25f ? 28.25f : 96.05000305175781f;
    return out;
}

/* one sample of the tape: x in, the repeat out (s->heads the heads' sum) */
static float tape_run(struct state *s, float x, float len, float mode)
{
    dly_write(&s->tape, x);
    float frac = len - __builtin_truncf(len);                                         /* VRINTZ */
    float half = len * 0.5f, y[4];
    int whole[4] = { (int)(len * 0.25f), (int)half, (int)(len * 0.75f), (int)len };
    for (int h = 0; h < 4; h++) {
        float a = tape_read(&s->tape, whole[h]);
        float in = st_fma(s->prev[h] - a, frac, a);
        s->prev[h] = a;
        y[h] = biquad_run(s, &s->head[h], in);
    }
    float heads = heads_mix(s, mode, y);
    s->heads = heads;
    /* the pre-delay, then the wow line */
    float ph = (float)((double)s->n * 0.000356189625827544);
    s->pre[s->pw] = heads;
    s->pw = s->pw > PRE - 2 ? 0 : s->pw + 1;
    float pre = s->pre[s->pr];
    s->pr = s->pr > PRE - 2 ? 0 : s->pr + 1;
    float m = st_fma(st_sinf(ph), 0.5f, 0.5f), d = st_fma(s->wow_depth, m, 10.0f);
    float next = s->n + 1.0f;
    s->n = (double)next > 17640.00000112896 ? (float)((double)next + -17640.00000112896) : next;
    int old = s->ww, nw = old > WOW - 2 ? 0 : old + 1, i = nw - (int)d;
    if (i < 0)
        i += WOW;
    int older = i < 1 ? i + WOW - 1 : i - 1;
    s->wow[old] = pre;
    s->ww = nw;
    float a = s->wow[i], wv = st_fma(d - __builtin_truncf(d), s->wow[older] - a, a);
    wv = st_fma(s->feedback, s->repeat, wv);
    dly_write(&s->line, dly_hold(wv, 1.7000000476837158f));
    float r = dly_read(&s->line, whole[1], half - (float)whole[1]);
    s->repeat = biquad_run(s, &s->rep, r);
    return s->repeat;
}

static void clear(struct state *s)
{
    s->tape.w = s->line.w = 0;
    s->pw = PRE - 1;
    s->pr = s->ww = 0;
    for (int h = 0; h < 4; h++)
        s->prev[h] = 0.0f;
    __builtin_memset(s->tape.buf, 0, BUFFER);
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
    s->tone = k[3] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[5];
    s->tape.buf = b;
    s->line.buf = b + DLY_LINE;
    s->pre = b + 2 * DLY_LINE;
    s->wow = s->pre + PRE;
    clear(s);
    lowpass_set(s, k[3], s->tone);
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
    float mode = k[4] + -1.0f, tone = k[3] * 0.01f;
    if (s->tone != tone) {
        s->tone = tone;
        lowpass_set(s, k[3], tone);
    }
    float target = dly_time(k[1], k[5], io->beat_ms);
    if (io->frames == 0)
        return;
    float df = (k[2] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = p[0];
        s->mix += dm;
        s->feedback += df;
        float glided = s->len + dl;
        s->len = k[5] == 1.0f ? target : glided;
        float in;
        if (io->enabled) {
            in = x * 0.5f;
        } else if (k[6] == 1.0f) {
            in = 0.0f;
        } else {
            if (k[6] == 0.0f && !s->cleared)
                clear(s);
            continue;
        }
        float r = tape_run(s, in, s->len, mode);
        float wet = st_fma(r, 0.5f, s->heads), y;
        if (s->mix < 0.5f)
            y = st_fma(wet + wet, s->mix, x);
        else if ((int)k[6] == 1 && !io->enabled)
            y = wet + x;
        else
            y = st_fma(x + x, 1.0f - s->mix, wet);
        p[0] = p[1] = y;
        if (io->enabled)
            s->cleared = 0;
    }
}

const struct wvfx_desc dly_multi_tape_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x109),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Multi Tape",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
