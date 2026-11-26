/*
 * M-Boost (DYN): the original's M-Boost (QME-200 V1.2.2, library entry 0.3, id 0x00000014) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A boost, mono (the left input; both outputs the same): two 2nd-order
 * sections, x pow3(Gain), then the amps' 4-points stage (amp.h) with an asymmetric exponential clipper (3 - 3
 * e^(-2.5 v) above zero, 3.5 e^(2 v) - 3.5 below) and the 40-tap FIR. Gain glides 0.0001 per sample. Bypassed it
 * passes the signal. Single precision.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PF1 = {
    3.2815377712249756f, -6.259716510772705f, 2.9782471656799316f, 1.0f, -1.9048837423324585f, 0.9051073789596558f,
};
static const struct sos PF2 = {
    6.259716510772705f, 4.4543023109436035f, 0.22258180379867554f, 1.0f, 0.649136483669281f, 0.004611699841916561f,
};

struct state {
    float gain;
    struct sos_state pf1, pf2;
    struct stage st;
};

/* 3 - 3 e^(-2.5 v) above zero, 3.5 e^(2 v) - 3.5 below, by v's sign bit, not a compare (at +0 both exponents are 0:
   e = 1, both sides 0); amp.h exp_np */
static inline float clip(float v)
{
    float e = exp_np(pick_sign(v, v * -2.5f, v + v));
    return pick_sign(v, st_fma(-e, 3.0f, 3.0f), st_fma(e, 3.5f, -3.5f));                        /* VFMS */
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow3, k[0]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow3, io->knobs[0]);
    /* stage by stage over the block (Gain's glide once per sample): the same samples as sample by sample */
    float *p = io->io, y[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0)
        return;
    for (int i = 0; i < n; i++)
        y[i] = p[2 * i];
    sos_block(&PF1, &s->pf1, y, n);
    sos_block(&PF2, &s->pf2, y, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        y[i] = y[i] * s->gain;
    }
    STAGE_BLOCK_POINTS(&s->st, y, n, buf, clip(v));
    stage_block_fir(&s->st, buf, y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i];
}

const struct wvfx_desc dyn_m_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x103),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "M-Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
