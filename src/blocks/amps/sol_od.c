/*
 * Sol 100 OD (AMP › Sol): the original's Sol 100 OD amp (QME-200 V1.2.2, library entry 4.25, id 0x07000047) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The shared pieces are in sol.h; this model:
 *   the CL's pre-filters, x Gain, a fixed section and a low pass (both divided by their a0), the stage +-10
 *   (k 3 / 1024), the full tone stack, x 0.1 Volume, the presence, x 1.1088 (no power section).
 */
#include "sol.h"

static const struct wvfx_param params[] = SOL_PARAMS(50.0f);

static const struct sos PF3 = { 1.038100004196167f, -1.9926999807357788f, 0.961899995803833f, 1.0957000255584717f,
                                -1.9926999807357788f, 0.9042999744415283f };

static const struct sos LOWPASS = { 0.07902348041534424f, 0.15804696083068848f, 0.07902348041534424f,
                                    1.3815776109695435f, -1.683906078338623f, 0.6184223890304565f };

static void presence_set(struct sos *c, float p)
{
    shelves_set(c, p, 0.5315085053443909f, -0.3654121160507202f, SOL_PRES_K, SOL_PRES_G);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct sol_state *s = state;
    (void)buffer; (void)env;
    sol_init(s, k);
    tmb_tone_set(&s->tone, &SOL_TS, s->bass, s->middle, s->treble);
    presence_set(&s->pres_c, s->presence);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct sol_state *s = state;
    float bass = st_taper(st_pow3, io->knobs[SL_BASS]);
    float presence = st_taper(st_pow2, io->knobs[SL_PRESENCE]);
    float gain = st_taper(st_pow2_5, io->knobs[SL_GAIN]);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            sol_glide(s, io->knobs, bass);
        return;
    }
    if (s->presence != presence) {
        s->presence = sol_presence_knob(io->knobs[SL_PRESENCE]);
        presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&SOL_PF[0], &s->pf[0], x, n);
    sos_block(&SOL_PF[1], &s->pf[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        x[i] = sos_run_div(&PF3, &s->pf3, x[i] * s->gain);
    }
    sos_block_div(&LOWPASS, &s->lowpass, x, n);
    stage_pow_block(&s->stage, x, n, 10.0f, 0.0029296875f);
    for (int i = 0; i < n; i++) {
        if (sol_glide_post(s, io->knobs, bass)) {
            s->bass = bass;
            tmb_tone_set(&s->tone, &SOL_TS, s->bass, s->middle, s->treble);
        }
        x[i] = s->volume * 0.1f * tone3_run_b1b0(&s->tone, x[i]);
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 1.1088441610336304f;
}

const struct wvfx_desc sol_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x47),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sol 100 OD",
    .category = "Sol",
    .params = params,
    .state_size = sizeof(struct sol_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
