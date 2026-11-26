/*
 * Pure (DELAY): the original's Pure delay (QME-200 V1.2.2, library entry 9.1, id 0x0B000000) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A clean stereo delay: each input into its own 4 s line with
 * Feedback x the line's last read (clipped to +-2), read Time back between two samples. Single precision.
 *
 * Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x. Mix, Feedback and the time glide 0.00022676 of
 * the way to the knobs each sample (with Sync on the time jumps). Bypassed with Trail on, the lines keep running on
 * their feedback alone and the tail is added to the dry signal; with Trail at 0 the lines are cleared once and the
 * signal passes. The Init reads the tempo: here the first process call does, from the Init's knobs.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct dly_line l, r;
    float out_l, out_r;                 /* each line's last read */
};

static void clear(struct state *s)
{
    s->l.w = s->r.w = 0;
    s->out_l = s->out_r = 0.0f;
    __builtin_memset(s->l.buf, 0, DLY_BUFFER);
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
    s->l.buf = buffer;
    s->r.buf = (float *)buffer + DLY_LINE;
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
        float xl = p[0], xr = p[1];
        int whole = (int)s->len;
        float frac = s->len - __builtin_truncf(s->len);                            /* VRINTZ */
        if (io->enabled) {
            dly_write(&s->l, dly_clip2(st_fma(s->out_l, s->feedback, xl)));
            s->out_l = dly_read(&s->l, whole, frac);
            dly_write(&s->r, dly_clip2(st_fma(s->out_r, s->feedback, xr)));
            s->out_r = dly_read(&s->r, whole, frac);
            if (s->mix < 0.5f) {
                float m = s->mix + s->mix;
                p[0] = st_fma(m, s->out_l, xl);
                p[1] = st_fma(m, s->out_r, xr);
            } else {
                float m = 1.0f - s->mix;
                p[0] = st_fma(xl + xl, m, s->out_l);
                p[1] = st_fma(xr + xr, m, s->out_r);
            }
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            dly_write(&s->l, dly_clip2(s->out_l * s->feedback));
            s->out_l = dly_read(&s->l, whole, frac);
            dly_write(&s->r, dly_clip2(s->out_r * s->feedback));
            s->out_r = dly_read(&s->r, whole, frac);
            if (s->mix < 0.5f) {
                float m = s->mix + s->mix;
                p[0] = st_fma(m, s->out_l, xl);
                p[1] = st_fma(m, s->out_r, xr);
            } else {
                p[0] = s->out_l + xl;
                p[1] = s->out_r + xr;
            }
        } else if (k[4] == 0.0f && !s->cleared) {
            clear(s);
        }
    }
}

const struct wvfx_desc dly_pure_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x101),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Pure",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
