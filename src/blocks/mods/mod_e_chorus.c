/*
 * E-Chorus (MOD › Chorus): the original's E-Chorus (QME-200 V1.2.2, library entry 8.0, id 0x04000001) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a chorus voice per output, swept in
 * opposite phase. Single precision.
 *
 * The path: x / 4 through a fixed 1st-order section (y2), then per output y2 + 0.9 x that output's last voice, a
 * fixed 3rd-order low pass, a 300-sample delay line read 82 + 174 d (0.5 +- t) samples back (t = asin(sin(4 pi r n))
 * / pi, a triangle; linear interpolation), a 1st-order low pass that follows d and a fixed 4th-order low pass (the
 * voice). Out: 0.066666 y2 + 0.68 Volume (0.85 + 0.15 (0.5 +- t)) x the voice.
 *
 * d = 0.1 + 0.006 Depth glides 0.001 per process call, also while bypassed; the low pass is recomputed only while d is
 * still on its way (the Init sets it with the constants folded otherwise), so it keeps the value one step short of
 * the knob (kept). Volume and the rate glide 0.00022676 of the way to the knobs each sample; the rate starts at the
 * knob's (the Init reads the tempo: here the first process call does, from the Init's knobs). The count n starts
 * over at one period.
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Depth", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x13, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x21, 0, 0, 0 },
};

struct voice {
    struct lp3_state lp3;
    MOD_LINE(300) line;
    struct fos_state lp;
    struct ce_lp4_state lp4;
    float last;                         /* the voice's last output (the feedback) */
};

struct state {
    float depth, volume, rate;
    float rate0, sync0;                 /* the Init's Rate and Sync, for the first process call */
    int n, started;
    struct fos lpc;                     /* the 1st-order low pass, a0 in lpa0 */
    float lpa0;
    struct fos_state in;
    struct voice l, r;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    float d = k[0] * 0.006000000052154064f;
    s->depth = d + 0.10000000149011612f;
    s->volume = k[2] * 0.01f;
    s->rate0 = k[1];
    s->sync0 = k[3];
    s->lpc = (struct fos){ 0.6055259704589844f, 0.6055259704589844f, -1.4944740533828735f - d };
    s->lpa0 = d + 2.7055258750915527f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->rate = mod_rate(s->rate0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = st_fma(k[0], 0.006000000052154064f, 0.10000000149011612f);
    float rate = mod_rate(k[1], k[3], io->beat_ms);
    s->depth = st_glide_by(s->depth, target, 0.0010000000474974513f);
    if (!io->enabled || io->frames == 0)
        return;
    if (s->depth != target) {
        s->lpc = (struct fos){ 0.6055259704589844f, 0.6055259704589844f, -1.3944740295410156f - s->depth };
        s->lpa0 = s->depth + 2.6055259704589844f;
    }
    float dv = (k[2] * 0.01f - s->volume) * 0.0002267600066261366f;
    float dr = (rate - s->rate) * 0.0002267600066261366f;
    float width = s->depth * 174.0f;
    /* stage by stage over the block (the same operations on the same values as voice_run sample by sample): the
       lines' delays (82+ samples) are longer than the block, so each voice's reads come from before it: per voice
       the reads, the low pass and the 4th-order one (its outputs, the feedback), then per sample the 3rd-order
       input section on the input plus the feedback of the output before, written */
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float *p = io->io, y2[WVFX_MAX_FRAMES], tv[2][WVFX_MAX_FRAMES], gv[WVFX_MAX_FRAMES], o[2][WVFX_MAX_FRAMES];
    for (int i = 0; i < n; i++) {
        s->rate += dr;
        s->volume += dv;
        y2[i] = fos_run(&CE_IN, &s->in, p[2 * i] * 0.25f);
        float period = 1.0f / s->rate;
        float t = st_asinf(st_sinf((s->rate * 12.566370964050293f) * (float)s->n)) * 0.31830987334251404f;
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        tv[0][i] = t + 0.5f;
        tv[1][i] = 0.5f - t;
        gv[i] = s->volume * 0.6800000071525574f;
    }
    struct voice *vs[2] = { &s->l, &s->r };
    for (int c = 0; c < 2; c++) {
        struct voice *v = vs[c];
        const float *buf = v->line.buf;
        const int len = (int)(sizeof v->line.buf / sizeof v->line.buf[0]);
        int w = v->line.w;
        for (int i = 0; i < n; i++) {
            w = w >= len - 1 ? 0 : w + 1;       /* the position after sample i's write (line_run) */
            float d = st_fma(width, tv[c][i], 82.0f);
            int j = w - (int)d;
            if (j < 0)
                j += len;
            int older = j < 1 ? j + len - 1 : j - 1;
            float a = buf[j];
            o[c][i] = st_fma(buf[older] - a, d - __builtin_truncf(d), a);                 /* VRINTZ */
        }
        for (int i = 0; i < n; i++)
            o[c][i] = ce_lp4_run(&v->lp4, fos_run_div(&s->lpc, s->lpa0, &v->lp, o[c][i]));
        float last = v->last;
        int wr = v->line.w;
        for (int i = 0; i < n; i++) {
            v->line.buf[wr] = lp3_run(&CE_LP3, &v->lp3, st_fma(last, 0.8999999761581421f, y2[i]));
            wr = wr >= len - 1 ? 0 : wr + 1;
            last = o[c][i];
        }
        v->line.w = wr;
        v->last = last;
    }
    for (int i = 0; i < n; i++) {
        float dry = y2[i] * 0.06666599959135056f;
        p[2 * i] = st_fma(gv[i] * st_fma(tv[0][i], 0.15000000596046448f, 0.8500000238418579f), o[0][i], dry);
        p[2 * i + 1] = st_fma(gv[i] * st_fma(tv[1][i], 0.15000000596046448f, 0.8500000238418579f), o[1][i], dry);
    }
}

const struct wvfx_desc mod_e_chorus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x100),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "E-Chorus",
    .category = "Chorus",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
