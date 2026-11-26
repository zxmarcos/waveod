/*
 * Sol 100 CL (AMP › Sol): the original's Sol 100 CL amp (QME-200 V1.2.2, library entry 4.24, id 0x07000023) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * The shared pieces are in sol.h; this model:
 *   pre-filters, x Gain, a fixed section (divided by its a0), the stage +-50 (k 1 / 5120), the full tone stack in
 *   double, x 0.2 Volume (in double), the presence (its exponents p x 3.2 and p x -2.2 in double, then x 0.1661), one
 *   power section (divided), x 0.66.
 * Presence is recomputed while it differs from its taper: below the taper's point 5 (held at 0.01) that is every
 * sample, with the same result.
 */
#include "sol.h"

static const struct wvfx_param params[] = SOL_PARAMS(30.0f);

static const struct sos PF3 = { 1.0424000024795532f, -1.9967999458312988f, 0.9575999975204468f, 1.1195000410079956f,
                                -1.9967999458312988f, 0.8805000185966492f };

static const struct sos POWER = { 1.056399941444397f, -1.98989999294281f, 0.9435999989509583f, 1.0884000062942505f,
                                  -1.98989999294281f, 0.9115999937057495f };

static void presence_set(struct sos *c, float p)
{
    shelves_set_x(c, st_scale(p, 3.2) * 0.16609640419483185f, st_scale(p, -2.2) * 0.16609640419483185f,
                  SOL_PRES_K, SOL_PRES_G);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct sol_state *s = state;
    (void)buffer; (void)env;
    sol_init(s, k);
    tmb_tone_set_d(&s->tone, &SOL_TS, s->bass, s->middle, s->treble);
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
    stage_pow_block(&s->stage, x, n, 50.0f, 0.0001953125f);
    for (int i = 0; i < n; i++) {
        if (sol_glide_post(s, io->knobs, bass)) {
            s->bass = bass;
            tmb_tone_set_d(&s->tone, &SOL_TS, s->bass, s->middle, s->treble);
        }
        x[i] = (float)((double)s->volume * 0.2 * (double)tone3_run_b1b0(&s->tone, x[i]));
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    sos_block_div(&POWER, &s->power[0], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.6600000262260437f;
}

const struct wvfx_desc sol_cl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x23),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sol 100 CL",
    .category = "Sol",
    .params = params,
    .state_size = sizeof(struct sol_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
