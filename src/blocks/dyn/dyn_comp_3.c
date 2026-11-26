/*
 * COMP 3 (DYN): the original's COMP 3 (QME-200 V1.2.2, library entry 0.2, id 0x00000003) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A threshold / ratio compressor, mono (the left input; both outputs
 * the same). Single precision.
 *
 * The level, 20 log10 |x|, over the threshold T = -0.5 Threshold dB is divided by 1 + 0.19 Ratio; the gain in dB is
 * the difference, moving toward it by 1 - attack of the way per sample falling and 1 - release rising (attack =
 * e^(-2.1972 / (132.3 + 4410 pow6(Attack))), release = e^(-2.1972 / (441 + 44100 pow3(Release))): the 10 % - 90 %
 * times). Blend mixes the compressed signal into the dry one (below 0.5: x + 4.2 Blend g x; from 0.5:
 * (2 - 2 Blend) x + 2.1 g x); x Volume, through a 2nd-order tone section set by Tone (divided by its a0). Volume and
 * Blend glide 0.005 per process call. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Threshold", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Ratio", 0.0f, 100.0f, 1.0f, 30.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Attack", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Release", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Blend", 0.0f, 100.0f, 1.0f, 50.0f, 6, 0x15, 0x00, 0, 0, 0 },
};

struct state {
    float attack_k, release_k, tone_k, volume, blend, gain_db, attack, release;
    struct sos tone;                    /* divided by a0 */
    struct sos_state t;
};

static float attack_coeff(float a)
{
    return st_expf(-2.197200059890747f / st_fma(a, 4410.0f, 132.3000030517578f));
}

static float release_coeff(float r)
{
    return st_expf(-2.197200059890747f / st_fma(r, 44100.0f, 441.0f));
}

/* t: Tone x 0.01, knob: Tone */
static struct sos tone_set(float t, float knob)
{
    float k1 = knob * 0.020721452310681343f, k2 = knob * 0.010640012100338936f;
    return (struct sos){ t + 1.5582548379898071f, -2.6694726943969727f - k1, k2 + 1.1327182054519653f,
                         t + -2.5582549571990967f, 4.7416181564331055f - k1, k2 + -2.1967194080352783f };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->tone_k = k[5] * 0.01f;
    s->attack_k = st_taper(st_pow6, k[3]);
    s->volume = k[2] * 0.01f;
    s->release_k = st_taper(st_pow3, k[4]);
    s->blend = k[6] * 0.01f;
    s->attack = attack_coeff(s->attack_k);
    s->release = release_coeff(s->release_k);
    s->tone = tone_set(s->tone_k, k[5]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    s->volume = st_glide_by(s->volume, k[2] * 0.01f, 0.004999999888241291f);
    s->blend = st_glide_by(s->blend, k[6] * 0.01f, 0.004999999888241291f);
    if (io->frames == 0)
        return;
    float att = st_taper(st_pow6, k[3]), rel = st_taper(st_pow3, k[4]);
    float attack = attack_coeff(att), release = release_coeff(rel);
    if (!io->enabled)
        return;
    float tone = k[5] * 0.01f, thr = k[0] * -0.5f, ratio = st_fma(k[1], 0.1899999976158142f, 1.0f);
    float b = s->blend, wet_lo = b * 4.200000286102295f, dry_hi = st_fma(-b, 2.0f, 2.0f);      /* VFMS */
    struct sos tc = tone_set(tone, k[5]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float x = p[0];
        if (s->attack_k != att) {
            s->attack_k = att;
            s->attack = attack;
        }
        if (s->release_k != rel) {
            s->release_k = rel;
            s->release = release;
        }
        if (s->tone_k != tone) {
            s->tone_k = tone;
            s->tone = tc;
        }
        float db = st_log10f(st_abs(x) + 9.99999991097579e-38f) * 20.0f;
        float c = (db - thr) / ratio + thr;
        float g = (thr > db ? db : c) - db;
        s->gain_db = st_fma(g <= s->gain_db ? s->attack : s->release, s->gain_db - g, g);
        float gx = st_exp2f(s->gain_db * 0.16609640419483185f) * x, y;
        if (b < 0.5f)
            y = st_fma(wet_lo, gx, x);
        else
            y = st_fma(gx, 2.1000001430511475f, dry_hi * x);
        p[0] = p[1] = sos_run_div(&s->tone, &s->t, y * s->volume);
    }
}

const struct wvfx_desc dyn_comp_3_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x102),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "COMP 3",
    .category = "Compressor",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
