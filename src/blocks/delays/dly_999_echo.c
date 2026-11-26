/*
 * 999 Echo (DELAY): the original's 999 Echo (QME-200 V1.2.2, library entry 9.11, id 0x0B000012) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A mono echo with a voiced repeat path: the left input through a
 * fixed 2nd-order section (y1); y1 plus Feedback (the repeat taper) x the last repeat through a 3rd-order low pass
 * into a 4 s line, read Time back (between two samples), through three 1st-order low passes whose corner follows the
 * time, a 3rd-order section and a 2nd-order one, clipped to +-1.7 (the repeat). Single precision.
 *
 * Out, on both outputs, through a fixed 2nd-order section: below Mix 0.5, y1 + Mix repeat; from 0.5, repeat / 2 +
 * 2 (1 - Mix) y1 (bypassed with Trail on: repeat / 2 + y1). Glides, Sync and Trail as the Pure's (`dly_pure.c`).
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x13, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 30.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x00, 0, 0, 0 },
};

static const struct sos IN = {
    -3.72346568107605f, -0.20727837085723877f, 3.5161874294281006f, 1.0f, -0.29290926456451416f,
    -0.2925339937210083f,
};
static const struct lp3 PRE = {
    0.005835116375237703f, 0.017505349591374397f, 0.017505349591374397f, 0.005835116375237703f, -2.3432841300964355f,
    1.9828330278396606f, -0.592867910861969f,
};
static const struct lp3 S5 = {
    0.006806586403399706f, 0.020419759675860405f, 0.020419759675860405f, 0.006806586403399706f, -2.0613627433776855f,
    1.3571312427520752f, -0.2413157969713211f,
};
static const struct sos S6 = {
    0.08546324819326401f, 0.17092649638652802f, 0.08546324819326401f, 1.0f, -1.5164352655410767f, 0.8582882285118103f,
};
static const struct sos OUT = {
    -0.23467852175235748f, -0.06880857050418854f, 0.16586995124816895f, 1.0f, -0.07292608916759491f,
    -0.8238610625267029f,
};
#define LPF_B 0.10812964290380478f

struct state {
    float mix, feedback, len;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct dly_line line;
    float repeat;
    struct sos_state in, s6, out;
    struct lp3_state pre, s5;
    struct fos_state lpf[3];
};

/* the repeat path: in through the line and the filters */
static float repeat_run(struct state *s, float in)
{
    dly_write(&s->line, lp3_run(&PRE, &s->pre, in));
    float y = dly_read(&s->line, (int)s->len, s->len - __builtin_truncf(s->len));     /* VRINTZ */
    float k = s->len * 5.697421329386998e-06f;
    const struct fos lpf = { LPF_B, LPF_B, -0.2618452310562134f - k };
    float a0 = k + 0.47810453176498413f;
    for (int i = 0; i < 3; i++)
        y = fos_run_div(&lpf, a0, &s->lpf[i], y);
    y = sos_run(&S6, &s->s6, lp3_run(&S5, &s->s5, y));
    return dly_hold(y, 1.7000000476837158f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = st_taper(st_repeat, k[2]);
    s->time0 = k[1];
    s->sync0 = k[3];
    s->line.buf = buffer;
    __builtin_memset(buffer, 0, DLY_LINE * sizeof(float));
    s->cleared = 1;
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
    float df = (st_taper(st_repeat, k[2]) - s->feedback) * 0.0002267600066261366f;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix += dm;
        s->feedback += df;
        float glided = s->len + dl;
        s->len = k[3] == 1.0f ? target : glided;
        float y1, wet;
        if (io->enabled) {
            y1 = sos_run(&IN, &s->in, p[0]);
            s->repeat = wet = repeat_run(s, st_fma(s->repeat, s->feedback, y1));
            s->cleared = 0;
        } else if (k[4] == 1.0f) {
            y1 = sos_run(&IN, &s->in, p[0]);
            s->repeat = wet = repeat_run(s, s->repeat * s->feedback);
        } else {
            if (k[4] == 0.0f && !s->cleared) {
                s->repeat = 0.0f;
                s->line.w = 0;
                __builtin_memset(s->line.buf, 0, DLY_LINE * sizeof(float));
                s->cleared = 1;
            }
            continue;
        }
        float y;
        if (s->mix < 0.5f)
            y = st_fma(s->mix, wet, y1);
        else if (!io->enabled && (int)k[4] == 1)
            y = y1 + wet * 0.5f;
        else
            y = st_fma(y1 + y1, 1.0f - s->mix, wet * 0.5f);
        y = sos_run(&OUT, &s->out, y);
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc dly_999_echo_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10B),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "999 Echo",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_LINE * sizeof(float),
    .init = init,
    .process = process,
    .poll = 0,
};
