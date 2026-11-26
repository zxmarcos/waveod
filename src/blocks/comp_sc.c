/*
 * Comp SC (DYN menu, waveod's): a feed-forward compressor whose key can be the clean guitar at the chain's input
 * (Input) or the signal at the block (Chain), stereo-linked.
 *
 *   gain computer   above threshold - knee / 2 the level is reduced by (1 - 1 / ratio), with a quadratic soft knee
 *   detector        the key's peak (both channels), falling with the release time (a decoupled peak detector, so a
 *                   steady tone is measured at its peak); the reduction it asks for rises with the attack time
 *                   and falls with the detector
 *   makeup, mix     makeup gain on the compressed signal, then mixed with the dry one (parallel compression)
 *
 * Bypassed it fades to the dry signal over one block (and back), so switching it never clicks.
 */
#include "sdk/wvfx.h"

enum { K_THRESHOLD, K_RATIO, K_ATTACK, K_RELEASE, K_KNEE, K_MAKEUP, K_MIX, K_KEY };

static const struct wvfx_param params[] = {
    { "Threshold", -60.0f, 0.0f, 0.5f, -20.0f, K_THRESHOLD, 0, 0, 0, 0, "%.1f dB" },
    { "Ratio", 1.0f, 20.0f, 0.1f, 4.0f, K_RATIO, 0, 0, 0, 0, "%.1f:1" },
    { "Attack", 0.1f, 100.0f, 0.1f, 5.0f, K_ATTACK, 0, 0, 0, 0, "%.1f ms" },
    { "Release", 10.0f, 2000.0f, 5.0f, 150.0f, K_RELEASE, 0, 0, 0, 0, "%.0f ms" },
    { "Knee", 0.0f, 12.0f, 0.5f, 6.0f, K_KNEE, 0, 0, 0, 0, "%.1f dB" },
    { "Makeup", 0.0f, 24.0f, 0.5f, 0.0f, K_MAKEUP, 0, 0, 0, 0, "%.1f dB" },
    { "Mix", 0.0f, 100.0f, 1.0f, 100.0f, K_MIX, 0, 0, 0, 0, "%.0f%%" },
    { "Key", 0.0f, 1.0f, 1.0f, 1.0f, K_KEY, 0, 0, 0, "Input\0Chain\0", 0 },
};

struct state {
    float rate;
    float env;                      /* the key's peak, falling with the release time */
    float reduce;                   /* the gain reduction, dB >= 0 */
    float on;                       /* 0..1: dry .. processed, ramped per block (bypass) */
};

/* the static curve: dB of reduction for a key level of `db` */
static float reduction(float db, float thr, float ratio, float knee)
{
    float over = db - thr, slope = 1.0f - 1.0f / ratio;
    if (knee > 0.0f && over > -0.5f * knee && over < 0.5f * knee) {
        float t = over + 0.5f * knee;
        return slope * t * t / (2.0f * knee);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    (void)buffer;
    (void)knobs;
    struct state *s = state;
    s->rate = env->rate;
    s->env = 0.0f;
    s->reduce = 0.0f;
    s->on = 1.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float thr = wvfx_clamp(k[K_THRESHOLD], -60.0f, 0.0f), ratio = wvfx_clamp(k[K_RATIO], 1.0f, 20.0f);
    float knee = wvfx_clamp(k[K_KNEE], 0.0f, 12.0f);
    float att = wvfx_coef(wvfx_clamp(k[K_ATTACK], 0.1f, 100.0f), s->rate);
    float rel = wvfx_coef(wvfx_clamp(k[K_RELEASE], 10.0f, 2000.0f), s->rate);
    float makeup = wvfx_clamp(k[K_MAKEUP], 0.0f, 24.0f), mix = wvfx_clamp(k[K_MIX], 0.0f, 100.0f) * 0.01f;
    const float *key = k[K_KEY] >= 0.5f || !io->aux ? io->io : io->aux;
    float on = s->on, to = io->enabled ? 1.0f : 0.0f, d_on = wvfx_ramp_step(on, to, io->frames);
    float env = s->env, r = s->reduce, *x = io->io;
    for (int f = 0; f < io->frames; f++) {
        float a = key[2 * f] < 0 ? -key[2 * f] : key[2 * f], b = key[2 * f + 1] < 0 ? -key[2 * f + 1] : key[2 * f + 1];
        float peak = a > b ? a : b;
        env = peak > env ? peak : env * rel;
        float want = reduction(wvfx_to_db(env), thr, ratio, knee);
        r = want > r ? want + (r - want) * att : want;
        on += d_on;
        float wet = wvfx_db(makeup - r) * mix + (1.0f - mix);       /* processed gain, mixed with the dry */
        float g = on * wet + (1.0f - on);
        x[2 * f] *= g;
        x[2 * f + 1] *= g;
    }
    s->env = env;
    s->reduce = r;
    s->on = to;
}

const struct wvfx_desc comp_sc_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 2),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .flags = 0,
    .name = "Comp SC",
    .category = "Sidechain",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
