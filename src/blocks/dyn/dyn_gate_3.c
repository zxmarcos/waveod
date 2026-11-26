/*
 * Gate 3 (DYN): the original's Gate 3 (QME-200 V1.2.2, library entry 0.13, id 0x00000021) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A downward expander keyed by the chain's input (the right channel of
 * the side-chain buffer), with hysteresis and hold. Single precision.
 *
 * The key's level e follows |key| at once when it rises and by 0.0016 of the way per sample when it falls (at least
 * 1e-6). The gate opens (target 1) while e exceeds the threshold, which drops 3 dB once open (Threshold through the
 * pow0_2 taper, 2^(16.61 t - 16.61), -100 dB .. 0 dB); the gate state moves toward the target by 1 - attack of the
 * way per sample rising and, after Hold ms with the target below it, by 1 - release falling (attack and release
 * e^(-2.1972 / (44.1 ms)): the 10 % - 90 % time). Once per block the gain is (e / threshold)^(r - r state) while e is
 * under the threshold, else 1 (r = 3 pow0_7(Ratio)), and the output gain ramps toward it over the block (twice the
 * difference spread over the frames). Both outputs are the left input x the gain. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Threshold", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Ratio", 0.0f, 100.0f, 1.0f, 20.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Attack", 1.0f, 500.0f, 1.0f, 20.0f, 2, 0xbf, 0x00, 0, 0, 0 },
    { "Release", 10.0f, 10000.0f, 1.0f, 200.0f, 3, 0xbf, 0x00, 0, 0, 0 },
    { "Hold", 0.0f, 1000.0f, 1.0f, 500.0f, 4, 0xbf, 0x00, 0, 0, 0 },
};

struct state {
    float thr_knob, att_knob, rel_knob, hold_knob;     /* the knob values the settings below were set from */
    float threshold, threshold_hy, attack, release, hold, ratio;
    float level, open_at, gate, count, out_gain;       /* open_at: the threshold in force */
};

static float level_db(float t, float offset)
{
    return st_exp2f(t * 16.60964012145996f + offset);
}

static float time_coeff(float ms)
{
    return st_expf(-2.197200059890747f / st_fma(ms, 44.10000228881836f, 0.00044100001105107367f));
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->thr_knob = st_pow0_2[(int)k[0]];
    s->ratio = st_pow0_7[(int)k[1]] * 3.0f;
    s->att_knob = k[2];
    s->rel_knob = k[3];
    s->hold_knob = k[4];
    s->threshold = level_db(s->thr_knob, -16.60964012145996f);
    s->threshold_hy = level_db(s->thr_knob, -17.107929229736328f);
    s->attack = time_coeff(k[2]);
    s->release = time_coeff(k[3]);
    s->hold = k[4] * 44.10000228881836f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    int frames = io->frames;
    if (frames == 0)
        return;
    float thr = st_pow0_2[(int)k[0]];
    float threshold = level_db(thr, -16.60964012145996f), threshold_hy = level_db(thr, -17.107929229736328f);
    float attack = time_coeff(k[2]), release = time_coeff(k[3]);
    if (!io->enabled)
        return;
    float ratio = st_pow0_7[(int)k[1]] * 3.0f, hold = k[4] * 44.10000228881836f;
    const float *key = io->aux;
    for (int i = 0; i < frames; i++, key += 2) {
        s->ratio = ratio;
        if (s->thr_knob != thr) {
            s->thr_knob = thr;
            s->threshold = threshold;
            s->threshold_hy = threshold_hy;
        }
        if (s->att_knob != k[2]) {
            s->att_knob = k[2];
            s->attack = attack;
        }
        if (s->rel_knob != k[3]) {
            s->rel_knob = k[3];
            s->release = release;
        }
        if (s->hold_knob != k[4]) {
            s->hold_knob = k[4];
            s->hold = hold;
        }
        float a = st_abs(key[1]), e = s->level;
        e = e > a ? st_fma(a - e, 0.0015999999595806003f, e) : a;
        e = __builtin_fmaxf(e, 9.999999974752427e-07f);                                  /* VMAXNM */
        s->level = e;
        int open = e > s->open_at;
        float target = open ? 1.0f : 0.0f;
        s->open_at = open ? s->threshold_hy : s->threshold;
        if (target > s->gate) {
            s->gate = st_fma(s->attack, s->gate - target, target);
            s->count = 0.0f;
        } else if (!(s->count > s->hold)) {
            s->count += 1.0f;
        } else {
            s->gate = st_fma(s->release, s->gate - target, target);
        }
    }
    float t = 1.0f;
    if (s->level < s->open_at)
        t = st_powf(s->level / s->open_at, st_fma(-s->gate, s->ratio, s->ratio));              /* VFMS */
    float n = (float)(unsigned)frames, g = s->out_gain, *p = io->io;
    int i = 0;
    if (frames >= 4) {
        float step = 1.0f / n;
        for (; i < (frames & ~3); i++, p += 2) {
            float d = t - g;
            g = st_fma(d + d, step, g);
            p[0] = p[1] = g * p[0];
        }
    }
    for (; i < frames; i++, p += 2) {
        float d = t - g;
        g = (d + d) / n + g;
        p[0] = p[1] = g * p[0];
    }
    s->out_gain = g;
}

const struct wvfx_desc dyn_gate_3_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x10D),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "Gate 3",
    .category = "Gate",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
