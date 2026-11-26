/*
 * Sol 100 LD (AMP › Sol): the original's Sol 100 LD amp (QME-200 V1.2.2, library entry 4.26, id 0x07000059) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The shared pieces are in sol.h; this model:
 *   its own pre-filters, a 3rd-order gain section from Gain (pow2_5), a low pass (divided by its a0), the stage +-1.5
 *   (k 1 / 409.6), the classic tone stack, x Volume, the presence, five power sections, x 1.0124.
 * Process holds Presence at 0.01 only at 0 (Init below the taper's point 5, as the others): from a knob at points
 * 1 to 4 the presence changes on the first sample.
 */
#include "sol.h"

static const struct wvfx_param params[] = SOL_PARAMS(50.0f);

static const struct sos PF[2] = {
    { 23.52809715270996f, -46.66475296020508f, 23.138200759887695f, 1.0f, -1.9310288429260254f,
      0.932539701461792f },
    { 46.66475296020508f, 27.0245304107666f, 0.9614920020103455f, 1.0f, -0.6675130128860474f,
      0.1544477939605713f },
};

static const struct sos LOWPASS = { 0.011375010013580322f, 0.022750020027160645f, 0.011375010013580322f,
                                    1.1499934196472168f, -1.9544999599456787f, 0.8500065803527832f };

static const struct sos POWER[5] = {
    { 0.6460756063461304f, -1.2878165245056152f, 0.641785204410553f, 1.0f, -1.9921696186065674f,
      0.9922720789909363f },
    { 0.6479961276054382f, -1.2878165245056152f, 0.6403282284736633f, 1.0f, -1.768524408340454f,
      0.7845985293388367f },
    { 0.6863203048706055f, -1.2878165245056152f, 0.6101462244987488f, 1.0f, -1.986055612564087f,
      0.9867311120033264f },
    { 1.2878165245056152f, -0.14118559658527374f, 0.0006817000103183091f, 1.0f, -0.9463592171669006f,
      0.11090680211782455f },
    { 1.2878165245056152f, -1.0170186758041382f, 0.11604160070419312f, 1.0f, -0.05773530155420303f,
      0.03105900064110756f },
};

/* the classic tone stack (amp.h, tone_brit_f) with the other Sols' constants */
static const struct brit_ts TS = {
    .nb0 = 0.01778791f, .nk0 = 0.0004446978f, .nb1 = 0.01540551f, .nk1 = -0.0003851377f,
    .nm = { 0.01744181f, -0.04897936f, 0.04574968f, -0.01421213f },
    .nt = { 0.02614062f, -0.07612695f, 0.07385938f, -0.02387305f },
    .mb = { 0.6844468f, -1.948277f, 1.843213f, -0.5793829f },
    .m2 = { -0.01711117f, 0.04870692f, -0.04608032f, 0.01448457f },
    .db = { 1.11428f, -3.111898f, 2.88572f, -0.888102f },
    .dk = { 0.0291077f, -0.07881302f, 0.07089494f, -0.02118434f },
    .dm = { -0.009970506f, 0.02843295f, -0.026838f, 0.008375557f },
};

/* the gain section from g: bi = ci g^2 + ki g, ai = di + ci g^2 + ji g (c0 = 1) */
static void gain_set(struct tone3 *o, float g)
{
    float g2 = g * g, g2c1 = g2 * -2.9761641025543213f, g2c2 = g2 * 2.95232892036438f;
    float g2c3 = g2 * -0.9761644005775452f;
    o->b[0] = st_fma(g, -1.0226759910583496f, g2);
    o->b[1] = st_fma(g, 2.998300075531006f, g2c1);
    o->b[2] = st_fma(g, -2.9296529293060303f, g2c2);
    o->b[3] = st_fma(g, 0.9540290832519531f, g2c3);
    o->a[0] = st_fma(g, -0.9876635074615479f, -0.03529201075434685f) + g2;
    o->a[1] = st_fma(g, 2.963855028152466f, 0.03416439890861511f) + g2c1;
    o->a[2] = st_fma(g, -2.964664936065674f, 0.035290781408548355f) + g2c2;
    o->a[3] = st_fma(g, 0.9884738922119141f, -0.034165628254413605f) + g2c3;
}

static void presence_set(struct sos *c, float p)
{
    shelves_set(c, p, 0.5315085053443909f, -0.3654121160507202f, SOL_PRES_K, SOL_PRES_G);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct sol_state *s = state;
    (void)buffer; (void)env;
    sol_init(s, k);
    gain_set(&s->gain3, s->gain);
    tone_brit_f(&s->tone, &TS, s->bass, s->middle, s->treble);
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
    float gain = st_taper(st_pow2_5, io->knobs[SL_GAIN]);
    float bass = st_taper(st_pow3, io->knobs[SL_BASS]);
    float presence = st_taper(st_pow2, io->knobs[SL_PRESENCE]);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            sol_glide(s, io->knobs, bass);
        return;
    }
    if (s->presence != presence) {
        s->presence = presence == 0.0f ? 0.01f : presence;
        presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PF[0], &s->pf[0], x, n);
    sos_block(&PF[1], &s->pf[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            gain_set(&s->gain3, s->gain);
        x[i] = tone3_run_std(&s->gain3, x[i]);
    }
    sos_block_div(&LOWPASS, &s->lowpass, x, n);
    stage_pow_block(&s->stage, x, n, 1.5f, 0.0024414062f);
    for (int i = 0; i < n; i++) {
        if (sol_glide_post(s, io->knobs, bass)) {
            s->bass = bass;
            tone_brit_f(&s->tone, &TS, s->bass, s->middle, s->treble);
        }
        x[i] = tone3_run_b1b0(&s->tone, x[i]) * s->volume;
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 5; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 1.0124400854110718f;
}

const struct wvfx_desc sol_ld_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x59),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Sol 100 LD",
    .category = "Sol",
    .params = params,
    .state_size = sizeof(struct sol_state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
