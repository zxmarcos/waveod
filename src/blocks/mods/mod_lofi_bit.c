/*
 * Lofi Bit (MOD › Special): the original's Lofi Bit (QME-200 V1.2.2, library entry 8.22, id 0x0100002E) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input, the result on both outputs. Single
 * precision except the knob mappings and the rounding.
 *
 * The wet path: a sample-and-hold every trunc(2 + 254 s) samples (s = 0.9 pow3(Krush); the held value interpolated
 * by the overshoot), quantized to 2^n - 1 levels each side (n = round(1 + 15 (1 - 0.8 Bit))), a bilinear 2nd-order
 * low pass (Hi Cut) and high pass (Lo Cut). Out: below Mix 0.5, x + 2 Mix wet; from 0.5, wet + 2 (1 - Mix) x. Mix
 * glides 0.00022676 of the way to the knob each sample. The filters are recomputed when their knob changes; the
 * Init sets the high pass from the raw knob with its scaling folded otherwise (kept).
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Krush", 0.0f, 100.0f, 1.0f, 20.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Bit", 0.0f, 100.0f, 1.0f, 20.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Hi Cut", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Lo Cut", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    float mix, hicut, locut;            /* Mix glided; the filters' knobs as last set */
    float increment, hold;
    struct sos lp, hp;
    struct sos_state lps, hps;
};

static void lowpass_set(struct sos *c, float w)
{
    float s = st_sinf(w) * 0.7072135806083679f, cs = st_cosf(w), b1 = 1.0f - cs;
    *c = (struct sos){ b1 * 0.5f, b1, b1 * 0.5f, s + 1.0f, cs * -2.0f, 1.0f - s };
}

static void highpass_set(struct sos *c, float w)
{
    float s = st_sinf(w) * 0.7072135806083679f, cs = st_cosf(w), b = cs + 1.0f;
    *c = (struct sos){ b * 0.5f, -b, b * 0.5f, s + 1.0f, cs * -2.0f, 1.0f - s };
}

/* the bilinear sections: b1 x1 + b2 x2 summed first, b0 x last (the low pass); b0 x first (the high pass) */
static inline float lp_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(x, c->b0, st_fma(c->b2, s->x2, c->b1 * s->x1));
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, (n - d) / c->a0);
}

static float krush_of(float k)
{
    return (float)((double)st_taper(st_pow3, k) * 0.9);
}

static float bit_of(float k)
{
    return (float)st_fmad((double)(k * 0.01f), -0.8, 1.0);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->hicut = st_fma(-k[3], 0.009999999776482582f, 1.0f);
    s->locut = k[4] * 0.01f;
    lowpass_set(&s->lp, st_fma(s->hicut, 0.6411413550376892f, 0.07123792916536331f));
    highpass_set(&s->hp, st_fma(k[4], 0.0004274275852367282f, 0.028495172038674355f));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float krush = krush_of(k[1]), bit = bit_of(k[2]);
    float hicut = st_fma(-k[3], 0.009999999776482582f, 1.0f), locut = k[4] * 0.01f;
    if (!io->enabled || io->frames == 0)
        return;
    float dm = st_fma(k[0], 0.009999999776482582f, -s->mix) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->mix += dm;
        float x = p[0];
        if (s->hicut != hicut) {
            s->hicut = hicut;
            lowpass_set(&s->lp, st_fma(hicut, 0.6411413550376892f, 0.07123792916536331f));
        }
        if (s->locut != locut) {
            s->locut = locut;
            highpass_set(&s->hp, st_fma(locut, 0.042742758989334106f, 0.028495172038674355f));
        }
        float over = s->increment - __builtin_truncf(st_fma(krush, 254.0f, 2.0f));          /* VRINTZ */
        if (over >= 0.0f) {
            s->hold = st_fma(s->hold - x, over, x);
            s->increment = over;
        }
        s->increment += 1.0f;
        int n = (int)((double)st_fma(bit, 15.0f, 1.0f) + 0.5);
        float q = __builtin_truncf((float)(1 << n) + -1.0f);
        float v = (float)(uint32_t)(int32_t)((double)(st_abs(s->hold) * q) + 0.5);
        float wet = (s->hold >= 0.0f ? v : -v) / q;
        wet = sos_run_div(&s->hp, &s->hps, lp_run(&s->lp, &s->lps, wet));
        float y = s->mix < 0.5f ? st_fma(wet + wet, s->mix, x) : st_fma(x + x, 1.0f - s->mix, wet);
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc mod_lofi_bit_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x116),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Lofi Bit",
    .category = "Special",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
