/*
 * Eko D (DELAY): the original's Eko D (QME-200 V1.2.2, library entry 9.15, id 0x0B000003) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). Two independent delays: A on the left channel, B on the right, each
 * with its own Mix, Feedback (-100 .. 100: negative inverts the repeats), Time and Sync. Single precision; the times
 * in double.
 *
 * Each line takes its input plus 0.9 Feedback x its last read, clipped to +-1.7. Out per channel as the Pure's (below
 * Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x). The Mixes and Feedbacks glide 0.00022676 and the times
 * 0.00015117 of the way to the knobs each sample (with Sync on a time jumps). Bypassed with Trail on the lines ring
 * on their feedback alone, the tail added to the dry signal; with Trail at 0 they are cleared once (their write
 * positions kept) and the signal passes.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix A", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Feedback A", -100.0f, 100.0f, 1.0f, 20.0f, 1, 0x1d, 0x00, 0, 0, 0 },
    { "Time A", 20.0f, 4000.0f, 1.0f, 500.0f, 2, 0x19, 0x16, 0, 0, 0 },
    { "Mix B", 0.0f, 100.0f, 1.0f, 20.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Feedback B", -100.0f, 100.0f, 1.0f, 20.0f, 4, 0x1d, 0x00, 0, 0, 0 },
    { "Time B", 20.0f, 4000.0f, 1.0f, 500.0f, 5, 0x19, 0x17, 0, 0, 0 },
    { "A Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
    { "B Sync", 0.0f, 1.0f, 1.0f, 0.0f, 7, 0x00, 0x24, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 8, 0x00, 0x00, 0, 0, 0 },
};

struct side {
    float mix, feedback, len;
    struct dly_line line;
    float out;                          /* the line's last read */
};

struct state {
    float k0[8];                        /* the Init's knobs, for the first process call (the times need the tempo) */
    int started, cleared;
    struct side a, b;
};

static inline float clip17(float x)
{
    return dly_hold(x, 1.7000000476837158f);
}

static float side_run(struct side *s, float in)
{
    dly_write(&s->line, clip17(in));
    s->out = dly_read(&s->line, (int)s->len, s->len - __builtin_truncf(s->len));    /* VRINTZ */
    return s->out;
}

static float mix_of(const struct side *s, float x, float wet, int trailing)
{
    if (s->mix < 0.5f)
        return st_fma(s->mix + s->mix, wet, x);
    if (trailing)
        return wet + x;
    return st_fma(x + x, 1.0f - s->mix, wet);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    for (int i = 0; i < 8; i++)
        s->k0[i] = k[i];
    s->a.mix = k[0] * 0.01f;
    s->a.feedback = k[1] * 0.01f;
    s->b.mix = k[3] * 0.01f;
    s->b.feedback = k[4] * 0.01f;
    s->a.line.buf = buffer;
    s->b.line.buf = (float *)buffer + DLY_LINE;
    __builtin_memset(buffer, 0, DLY_BUFFER);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->a.len = dly_time_d(s->k0[2], s->k0[6], io->beat_ms);
        s->b.len = dly_time_d(s->k0[5], s->k0[7], io->beat_ms);
        s->started = 1;
    }
    float ta = dly_time_d(k[2], k[6], io->beat_ms), tb = dly_time_d(k[5], k[7], io->beat_ms);
    if (io->frames == 0)
        return;
    float dma = (k[0] * 0.01f - s->a.mix) * 0.0002267600066261366f;
    float dmb = (k[3] * 0.01f - s->b.mix) * 0.0002267600066261366f;
    float dfa = (k[1] * 0.01f - s->a.feedback) * 0.0002267600066261366f;
    float dfb = (k[4] * 0.01f - s->b.feedback) * 0.0002267600066261366f;
    float dla = (ta - s->a.len) * 0.00015117156726773828f, dlb = (tb - s->b.len) * 0.00015117156726773828f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float ga = s->a.len + dla, gb = s->b.len + dlb;
        s->a.len = k[6] == 1.0f ? ta : ga;
        s->b.len = k[7] == 1.0f ? tb : gb;
        s->a.mix += dma;
        s->b.mix += dmb;
        s->a.feedback += dfa;
        s->b.feedback += dfb;
        float xl = p[0], xr = p[1], l, r;
        if (io->enabled) {
            l = side_run(&s->a, st_fma(s->a.feedback * 0.8999999761581421f, s->a.out, xl));
            r = side_run(&s->b, st_fma(s->b.feedback * 0.8999999761581421f, s->b.out, xr));
            s->cleared = 0;
        } else if (k[8] == 1.0f) {
            l = side_run(&s->a, (s->a.feedback * 0.8999999761581421f) * s->a.out);
            r = side_run(&s->b, (s->b.feedback * 0.8999999761581421f) * s->b.out);
        } else {
            if (k[8] == 0.0f && !s->cleared) {
                s->a.out = s->b.out = 0.0f;
                __builtin_memset(s->a.line.buf, 0, DLY_BUFFER);
                s->cleared = 1;
            }
            continue;
        }
        int trailing = !io->enabled && (int)k[8] == 1;
        p[0] = mix_of(&s->a, xl, l, trailing);
        p[1] = mix_of(&s->b, xr, r, trailing);
    }
}

const struct wvfx_desc dly_eko_d_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10F),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Eko D",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
