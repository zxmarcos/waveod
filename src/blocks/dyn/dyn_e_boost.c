/*
 * E-Boost (DYN): the original's E-Boost (QME-200 V1.2.2, library entry 0.4, id 0x0000001A) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). An enhancer boost, mono (the left input; both outputs the same).
 * Single precision.
 *
 * x through a 1st-order section set by Gain (g = 0.115 + 0.885 C25(Gain): zeros 0.742 (g +- 1), poles g +- 0.0002,
 * the latter in double; divided by its a0), then the amps' 4-points stage (amp.h) with a rational clipper of its own
 * each side (P(|v|) / Q(|v|), odd), the 40-tap FIR, a 1st-order Bright section and a 1st-order +3dB (bass) section,
 * each picked by its switch (0 or 1; other values keep the last). The original keeps the FIR's output negated as
 * the Bright section's past input, which negates the result (kept). Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "+3dB", 0.0f, 1.0f, 1.0f, 1.0f, 1, 0x00, 0x00, 0, 0, 0 },
    { "Bright", 0.0f, 1.0f, 1.0f, 1.0f, 2, 0x00, 0x00, 0, 0, 0 },
};

/* [switch]: den, num (b0 = b1) / den, b1, b0 */
static const float BRIGHT[2][2] = {
    { -0.6464532017707825f, 0.17677339911460876f }, { 0.5491147041320801f, 0.7745572924613953f },
};
static const float BASS[2][3] = {
    { -0.9999650716781616f, -0.7241783738136292f, 0.7241783738136292f },
    { -0.9999518990516663f, -0.9978528022766113f, 0.9978528022766113f },
};

struct state {
    float gain_k, bass_k, bright_k;     /* the knob values the sections were set from */
    float pn0, pn1, pd0, pd1;           /* the Gain section: b0, b1 / a0, a1 */
    float bd, bn;                       /* Bright */
    float sd, sn1, sn0;                 /* +3dB */
    float px1, py1, bx1, by1, sx1, sy1;
    struct stage st;
};

static void pre_set(struct state *s, float g)
{
    float m = g * 0.7421875f;
    s->pn0 = m + 0.7423378229141235f;
    s->pn1 = -0.7420371770858765f - m;
    s->pd0 = (float)((double)g + 0.0002024619);
    s->pd1 = (float)(0.0002024619 - (double)g);
}

static float clip(float v)
{
    float a = st_abs(v), v2 = v * v, a3 = a * v2, p, q;
    if (v >= 0.0f) {
        q = st_fma(v2, 0.27379998564720154f, 51.40999984741211f) + a3;
        q = st_fma(a, -9.043999671936035f, q);
        p = st_fma(a3, 5.321000099182129f, st_fma(a, 55.150001525878906f, v2 * -17.020000457763672f));
    } else {
        q = st_fma(v2, -0.09519000351428986f, -26.6299991607666f) - a3;
        q = st_fma(a, 4.979000091552734f, q);
        p = st_fma(a3, 4.373000144958496f, st_fma(a, 28.899999618530273f, v2 * -10.699999809265137f));
    }
    return p / q;
}

static float gain_of(float knob)
{
    return st_fma(st_taper(st_c25, knob), 0.8849999904632568f, 0.11500000208616257f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->gain_k = gain_of(k[0]);
    s->bass_k = k[1];
    s->bright_k = k[2];
    pre_set(s, s->gain_k);
    if (k[2] == 0.0f || k[2] == 1.0f) {
        s->bd = BRIGHT[(int)k[2]][0];
        s->bn = BRIGHT[(int)k[2]][1];
    }
    if (k[1] == 0.0f || k[1] == 1.0f) {
        s->sd = BASS[(int)k[1]][0];
        s->sn1 = BASS[(int)k[1]][1];
        s->sn0 = BASS[(int)k[1]][2];
    }
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float g = gain_of(k[0]);
    float *p = io->io, y[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0)
        return;
    /* stage by stage over the block (the knobs set before the first sample, as the original sets them at it): the
       same samples as sample by sample */
    {
        if (s->gain_k != g) {
            s->gain_k = g;
            pre_set(s, g);
        }
        if (s->bass_k != k[1]) {
            s->bass_k = k[1];
            if (k[1] == 0.0f || k[1] == 1.0f) {
                s->sd = BASS[(int)k[1]][0];
                s->sn1 = BASS[(int)k[1]][1];
                s->sn0 = BASS[(int)k[1]][2];
            }
        }
        if (s->bright_k != k[2]) {
            s->bright_k = k[2];
            if (k[2] == 1.0f || k[2] == 0.0f) {
                s->bd = BRIGHT[(int)k[2]][0];
                s->bn = BRIGHT[(int)k[2]][1];
            }
        }
    }
    for (int i = 0; i < n; i++) {
        float x = p[2 * i];
        y[i] = st_fma(-s->pd1, s->py1, st_fma(s->pn1, s->px1, s->pn0 * x)) / s->pd0;          /* VFMS */
        s->px1 = x;
        s->py1 = y[i];
    }
    STAGE_BLOCK_POINTS(&s->st, y, n, buf, clip(v));
    stage_block_fir(&s->st, buf, y, n);
    for (int i = 0; i < n; i++, p += 2) {
        float fir = y[i];
        float b = st_fma(s->bx1, s->bn, -st_fma(fir, s->bn, s->by1 * s->bd));                  /* VFNMS */
        s->bx1 = -fir;
        s->by1 = b;
        float o = st_fma(-s->sd, s->sy1, st_fma(s->sx1, s->sn1, s->sn0 * b));                    /* VFMS */
        s->sx1 = b;
        s->sy1 = o;
        p[0] = p[1] = o;
    }
}

const struct wvfx_desc dyn_e_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x104),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "E-Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
