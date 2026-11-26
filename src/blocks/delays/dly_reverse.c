/*
 * Reverse (DELAY): the original's Reverse delay (QME-200 V1.2.2, library entry 9.14, id 0x0B000028) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Each input is written backwards into segments of the delay
 * time (a ring of two segments) and read forwards, so each segment plays reversed; a feedback line repeats the result.
 * Single precision; the time in double.
 *
 * Per channel: the input, faded in over the first 256 samples of a segment and out over the last 256, is written at
 * the mirrored position of the ring; the read (between two samples by the time's fraction, taken when a channel's
 * ring position wraps to 0) is the reversed signal r. A 4 s line takes Feedback x w (r + its last output) and is read
 * Time back; the channel's output is r + that read. w fades from 0 to 1 by 0.0001 per sample after the Init or a
 * clear (which also restarts the rings and lines). Out: below Mix 0.5, (x + 2 Mix wet) 2 Level; from 0.5, (wet + 2 (1 - Mix) x) 2 Level (bypassed with Trail
 * on: (wet + x) 2 Level). Mix, Feedback and Level glide 0.0001 per sample, the time 0.00022676 of the way. (The
 * original jumps the time with Sync on only when a second tempo word is 1; the waveod has no such word: it glides.)
 *
 * The rings are laid out as the original's (each channel's ring 352800 samples from its start, the right one's
 * running on into the feedback lines for times over 2 s, as there).
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x14, 0, 0, 0 },
    { "Feedback", -100.0f, 100.0f, 1.0f, 20.0f, 2, 0x1d, 0x00, 0, 0, 0 },
    { "Level", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 4, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x00, 0, 0, 0 },
};

#define BUFFER (2 * DLY_BUFFER)          /* the reverse rings, then the feedback lines */

struct state {
    float mix, feedback, level, len, wet;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    float target;                       /* this block's time (the rings' segment comes from it) */
    int started, cleared;
    int seg;                            /* the segment length in samples */
    float seg_frac;
    float *ring, *line;
    int rw[2], lw[2];                   /* ring positions, line write positions */
    float out[2];
};

static void segment_set(struct state *s, float len)
{
    s->seg = (int)len;
    s->seg_frac = len - __builtin_truncf(len);                                      /* VRINTZ */
}

/* one channel: x into its ring, the reversed read into its feedback line; r + the line's read */
static float channel_run(struct state *s, int ch, float x, float prev, float len)
{
    int whole = (int)len, b = s->rw[ch];
    if (b == 0)
        segment_set(s, s->target);
    int d = s->seg, w = (2 * (b / d) + 1) * d + ~b;                                  /* the mirrored position */
    if (w >= 2 * d)
        w -= 2 * d;
    int pos = w - (w / d) * d;
    float *ring = s->ring + ch * DLY_LINE;
    float in = pos >= 256 ? x : (x * 0.00390625f) * (float)pos;                      /* fade in */
    if (whole - pos <= 255)
        in = ((float)(~pos + d) * 0.00390625f) * in;                                 /* fade out */
    int next = b < 2 * d - 1 ? b + 1 : 0, i = next - d;
    if (i < 0)
        i += 2 * d;
    int older = i <= 0 ? i + 2 * d : i;
    ring[w] = in;
    float a = ring[i], r = st_fma(s->seg_frac, ring[older - 1] - a, a);
    s->rw[ch] = next;
    float *line = s->line + ch * DLY_LINE;
    line[s->lw[ch]] = (s->wet * s->feedback) * (r + prev);
    s->lw[ch] = s->lw[ch] > DLY_LINE - 2 ? 0 : s->lw[ch] + 1;
    int j = s->lw[ch] - whole;
    if (j < 0)
        j += DLY_LINE;
    int jo = j < 1 ? j + DLY_LINE - 1 : j - 1;
    float a2 = line[j];
    return st_fma(line[jo] - a2, len - (float)whole, a2 + r);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->level = k[3] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[4];
    s->ring = buffer;
    s->line = (float *)buffer + 2 * DLY_LINE;
    __builtin_memset(buffer, 0, BUFFER);
    s->cleared = 1;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time_d(s->time0, s->sync0, io->beat_ms);
        segment_set(s, s->len);
        s->started = 1;
    }
    float mix = k[0] * 0.01f, fb = k[2] * 0.01f, level = k[3] * 0.01f;
    s->target = dly_time_d(k[1], k[4], io->beat_ms);
    if (io->frames == 0)
        return;
    float dl = (s->target - s->len) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float xl = p[0], xr = p[1];
        s->feedback = st_glide(s->feedback, fb);
        s->mix = st_glide(s->mix, mix);
        s->level = st_glide(s->level, level);
        s->len = s->len + dl;
        int trailing = 0;
        if (io->enabled) {
            s->out[0] = channel_run(s, 0, xl, s->out[0], s->len);
            s->out[1] = channel_run(s, 1, xr, s->out[1], s->len);
            s->cleared = 0;
        } else if (k[5] == 1.0f) {
            s->out[0] = channel_run(s, 0, 0.0f, s->out[0], s->len);
            s->out[1] = channel_run(s, 1, 0.0f, s->out[1], s->len);
            trailing = (int)k[5] == 1;
        } else {
            if (k[5] == 0.0f && !s->cleared) {
                s->out[0] = s->out[1] = 0.0f;
                s->rw[0] = s->rw[1] = s->lw[0] = s->lw[1] = 0;
                __builtin_memset(s->ring, 0, BUFFER);
                s->wet = 0.0f;
                segment_set(s, s->len);
                s->cleared = 1;
            }
            continue;
        }
        s->wet = st_glide(s->wet, 1.0f);
        float l, r;
        if (s->mix < 0.5f) {
            float m = s->mix + s->mix;
            l = st_fma(m, s->out[0], xl);
            r = st_fma(m, s->out[1], xr);
        } else if (trailing) {
            l = s->out[0] + xl;
            r = s->out[1] + xr;
        } else {
            float m = 1.0f - s->mix;
            l = st_fma(xl + xl, m, s->out[0]);
            r = st_fma(xr + xr, m, s->out[1]);
        }
        p[0] = l * (s->level + s->level);
        p[1] = s->level * (r + r);
    }
}

const struct wvfx_desc dly_reverse_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10E),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Reverse",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
