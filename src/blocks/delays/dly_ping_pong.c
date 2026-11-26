/*
 * Ping Pong (DELAY): the original's Ping Pong (QME-200 V1.2.2, library entry 9.5, id 0x0B000004) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The left input plus Feedback x the A repeat goes into line B;
 * line B's repeat (through a fixed 2nd-order low pass and high pass, its a0 multiplied in as a reciprocal) goes into
 * line A, whose repeat (the same filters) closes the loop; each line is clipped to +-2 at its input. Single precision.
 *
 * Out, crossed: the left output is the right input mixed with the B repeats, the right output the left input mixed
 * with the A repeats (below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x). Glides, Sync and Trail as the
 * Pure's (`dly_pure.c`; bypassed with Trail on, line B takes Feedback x the A repeat alone).
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 2, 0x19, 0x13, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

static const struct sos LP = {
    0.34413713216781616f, 0.6882742643356323f, 0.34413713216781616f, 1.4750860929489136f, -0.6234514117240906f,
    0.5249139070510864f,
};
static const struct sos HP = {
    0.999188244342804f, -1.998376488685608f, 0.999188244342804f, 1.035599708557129f, -1.9967529773712158f,
    0.9644002914428711f,
};

struct side {
    struct dly_line line;
    struct sos_state lp, hp;
    float out;                          /* the side's last filtered repeat */
};

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct side a, b;
};

/* a section with its a0 applied as the reciprocal r (computed per block) */
static inline float sos_run_rcp(const struct sos *c, struct sos_state *s, float x, float r)
{
    float n = c->b0 * x;
    n = st_fma(c->b1, s->x1, n);
    n = st_fma(c->b2, s->x2, n);
    float d = c->a1 * s->y1;
    d = st_fma(c->a2, s->y2, d);
    return sos_out(s, x, (n - d) * r);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[1] * 0.01f;
    s->time0 = k[2];
    s->sync0 = k[3];
    s->a.line.buf = buffer;
    s->b.line.buf = (float *)buffer + DLY_LINE;
    __builtin_memset(buffer, 0, DLY_BUFFER);
    s->cleared = 1;
    return 0;
}

static float mix_of(float mix, float x, float wet, int trailing)
{
    if (mix < 0.5f)
        return st_fma(mix + mix, wet, x);
    if (trailing)
        return wet + x;
    return st_fma(x + x, 1.0f - mix, wet);
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = dly_time(k[2], k[3], io->beat_ms);
    if (io->frames == 0)
        return;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float df = (k[1] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float rl = 1.0f / LP.a0, rh = 1.0f / HP.a0;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float glided = s->len + dl;
        s->mix += dm;
        s->feedback += df;
        s->len = k[3] == 1.0f ? target : glided;
        float xl = p[0], xr = p[1];
        int trailing = 0;
        if (io->enabled) {
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            trailing = (int)k[4] == 1;
        } else {
            if (k[4] == 0.0f && !s->cleared) {
                s->a.out = s->b.out = 0.0f;
                __builtin_memset(s->a.line.buf, 0, DLY_BUFFER);
                s->cleared = 1;
            }
            continue;
        }
        float in_b = io->enabled ? st_fma(s->a.out, s->feedback, xl) : s->a.out * s->feedback;
        dly_write(&s->a.line, dly_clip2(s->b.out));
        dly_write(&s->b.line, dly_clip2(in_b));
        int whole = (int)s->len;
        float frac = s->len - __builtin_truncf(s->len);                            /* VRINTZ */
        float ra = dly_read(&s->a.line, whole, frac), rb = dly_read(&s->b.line, whole, frac);
        s->a.out = sos_run_rcp(&HP, &s->a.hp, sos_run_rcp(&LP, &s->a.lp, ra, rl), rh);
        s->b.out = sos_run_rcp(&HP, &s->b.hp, sos_run_rcp(&LP, &s->b.lp, rb, rl), rh);
        p[0] = mix_of(s->mix, xr, s->b.out, trailing);
        p[1] = mix_of(s->mix, xl, s->a.out, trailing);
    }
}

const struct wvfx_desc dly_ping_pong_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x105),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Ping Pong",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
