/*
 * Slapback (DELAY): the original's Slapback (QME-200 V1.2.2, library entry 9.6, id 0x0B000005) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A short stereo delay (20 .. 300 ms): each input plus Feedback x
 * its line's last read goes through a fixed 30-sample pre-delay (an 80-sample ring) into a 300 ms line read Time back
 * (between two samples), the read through a 2nd-order low pass. Single precision.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x. Mix, Feedback and the time glide 0.00022676 of
 * the way to the knobs each sample. Bypassed with Trail on, the lines ring on their feedback alone and the tail is
 * added to the dry signal; with Trail at 0 the lines are cleared once (write positions kept) and the signal passes.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 300.0f, 1.0f, 150.0f, 1, 0x82, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

#define LINE 13230                      /* 300 ms */
#define PRE 80
#define BUFFER ((2 * LINE + 2 * PRE) * (int)sizeof(float))

static const struct sos LP = {
    0.016818728297948837f, 0.033637456595897675f, 0.016818728297948837f, 1.0f, -1.6010522842407227f,
    0.6683272123336792f,
};

struct channel {
    float *line, *pre;
    int w, pw;
    float out;                          /* the line's last read */
    struct sos_state lp;
};

struct state {
    float mix, feedback, len;
    int cleared;
    struct channel l, r;
};

static float time_of(float k)
{
    return __builtin_fmaxf(__builtin_fminf(__builtin_truncf(k) * 44.099998474121094f, 13230.0f), 882.0f);
}

static float channel_run(struct channel *c, float in, float len)
{
    int old = c->pw;
    c->pre[old] = in;
    c->pw = old >= PRE - 1 ? 0 : old + 1;
    float delayed = c->pre[c->pw + (c->pw <= 29 ? 50 : -30)];                      /* 30 samples back */
    c->line[c->w] = delayed;
    c->w = c->w >= LINE - 1 ? 0 : c->w + 1;
    int whole = (int)len, i = c->w - whole;
    if (i < 0)
        i += LINE;
    int older = i < 1 ? i + LINE - 1 : i - 1;
    float a = c->line[i];
    c->out = st_fma(c->line[older] - a, len - (float)whole, a);
    return sos_run(&LP, &c->lp, c->out);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    float *b = buffer;
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->len = time_of(k[1]);
    s->l.line = b;
    s->r.line = b + LINE;
    s->l.pre = b + 2 * LINE;
    s->r.pre = b + 2 * LINE + PRE;
    __builtin_memset(b, 0, BUFFER);
    s->cleared = 1;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (io->frames == 0)
        return;
    float dm = st_fma(k[0], 0.009999999776482582f, -s->mix) * 0.0002267600066261366f;
    float df = st_fma(k[2], 0.009999999776482582f, -s->feedback) * 0.0002267600066261366f;
    float dl = (time_of(k[1]) - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix += dm;
        s->feedback += df;
        s->len += dl;
        float xl = p[0], xr = p[1], l, r;
        if (io->enabled) {
            l = channel_run(&s->l, st_fma(s->l.out, s->feedback, xl), s->len);
            r = channel_run(&s->r, st_fma(s->r.out, s->feedback, xr), s->len);
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            l = channel_run(&s->l, s->l.out * s->feedback, s->len);
            r = channel_run(&s->r, s->r.out * s->feedback, s->len);
        } else {
            if (k[4] == 0.0f && !s->cleared) {
                s->l.out = s->r.out = 0.0f;
                __builtin_memset(s->l.line, 0, BUFFER);
                s->cleared = 1;
            }
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

const struct wvfx_desc dly_slapback_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x106),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Slapback",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
