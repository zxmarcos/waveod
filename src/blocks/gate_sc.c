/*
 * Gate SC (DYN menu, waveod's): a noise gate whose key is the clean guitar at the chain's input (or, if asked,
 * the signal at the block), so it can sit after a high-gain amp and still open and close on what is played,
 * not on the amp's hiss.
 *
 *   key       Input (the guitar before the chain, io->aux) / Chain (the signal arriving at the block)
 *   threshold the key's level that opens it; it closes 6 dB lower, after `hold`
 *   range     how far it turns the signal down when closed (90 dB = silence)
 *   attack    the time to open, release the time to close (one-pole, per sample)
 *
 * Detector: the key's peak (both channels), decaying with a 10 ms time constant. Bypassed, the gain glides back
 * to unity with the attack time, so switching it never clicks.
 */
#include "sdk/wvfx.h"

enum { K_THRESHOLD, K_RANGE, K_ATTACK, K_HOLD, K_RELEASE, K_KEY };

static const struct wvfx_param params[] = {
    { "Threshold", -90.0f, 0.0f, 1.0f, -55.0f, K_THRESHOLD, 0, 0, 0, 0, "%.0f dB" },
    { "Range", 0.0f, 90.0f, 1.0f, 90.0f, K_RANGE, 0, 0, 0, 0, "%.0f dB" },
    { "Attack", 0.1f, 50.0f, 0.1f, 1.0f, K_ATTACK, 0, 0, 0, 0, "%.1f ms" },
    { "Hold", 0.0f, 500.0f, 5.0f, 50.0f, K_HOLD, 0, 0, 0, 0, "%.0f ms" },
    { "Release", 5.0f, 2000.0f, 5.0f, 100.0f, K_RELEASE, 0, 0, 0, 0, "%.0f ms" },
    { "Key", 0.0f, 1.0f, 1.0f, 0.0f, K_KEY, 0, 0, 0, "Input\0Chain\0", 0 },
};

#define HYSTERESIS_DB 6.0f
#define DETECT_MS     10.0f

struct state {
    float rate;
    float env;                      /* the key's peak, decaying */
    float gain;                     /* applied now, 0..1 */
    uint32_t hold_left;             /* samples to stay open below the threshold */
    uint8_t open;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *knobs)
{
    (void)buffer;
    (void)knobs;
    struct state *s = state;
    s->rate = env->rate;
    s->env = 0.0f;
    s->gain = 1.0f;                 /* starts open: no dip when the preset loads */
    s->hold_left = 0;
    s->open = 1;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float open_at = wvfx_db(wvfx_clamp(k[K_THRESHOLD], -90.0f, 0.0f));
    float close_at = open_at * wvfx_db(-HYSTERESIS_DB);
    float floor = wvfx_db(-wvfx_clamp(k[K_RANGE], 0.0f, 90.0f));
    if (k[K_RANGE] >= 89.5f)
        floor = 0.0f;
    float att = wvfx_coef(wvfx_clamp(k[K_ATTACK], 0.1f, 50.0f), s->rate);
    float rel = wvfx_coef(wvfx_clamp(k[K_RELEASE], 5.0f, 2000.0f), s->rate);
    float det = wvfx_coef(DETECT_MS, s->rate);
    uint32_t hold = (uint32_t)(wvfx_clamp(k[K_HOLD], 0.0f, 500.0f) * 0.001f * s->rate);
    const float *key = k[K_KEY] >= 0.5f || !io->aux ? io->io : io->aux;
    float *x = io->io;
    float env = s->env, g = s->gain;
    for (int f = 0; f < io->frames; f++) {
        float a = key[2 * f] < 0 ? -key[2 * f] : key[2 * f], b = key[2 * f + 1] < 0 ? -key[2 * f + 1] : key[2 * f + 1];
        float peak = a > b ? a : b;
        env = peak > env ? peak : env * det;
        if (env >= open_at) {
            s->open = 1;
            s->hold_left = hold;
        } else if (s->open && env < close_at) {
            if (s->hold_left)
                s->hold_left--;
            else
                s->open = 0;
        }
        float target = !io->enabled || s->open ? 1.0f : floor;
        g = target > g ? target + (g - target) * att : target + (g - target) * rel;
        x[2 * f] *= g;
        x[2 * f + 1] *= g;
    }
    s->env = env;
    s->gain = g;
}

const struct wvfx_desc gate_sc_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 1),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .flags = 0,
    .name = "Gate SC",
    .category = "Sidechain",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
