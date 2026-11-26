/*
 * B-Man Bri (AMP › B-Man): the original's B-Man Bri amp (QME-200 V1.2.2, library entry 4.2, id 0x07000024) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. The shared pieces are in bman.h; this model:
 *   a 2nd-order gain section from Gain (amp.h, gain2_set; divided by its a0), the presence's K 0.385 and middle
 *   terms 1.774, x 49.1.
 */
#include "bman.h"

static const struct wvfx_param params[] = BMAN_PARAMS(35.0f);

#define PRES_K 0.38513273000717163f
#define PRES_C 1.773593783378601f

/* b0 = g^2 - 1.113 g, b1 = 2 (g - g^2), b2 = g^2 - 0.887 g, a0 = -0.114 - 0.999 g + g^2, a1 = b1 - 0.000129,
   a2 = 0.114 - 1.001 g + g^2 */
static const float GAIN_K[7] = {
    -1.1133790016174316f, -0.8866212964057922f, -0.9994331002235413f, -0.11400990188121796f,
    -0.00012854729720856994f, -1.0005669593811035f, 0.11388129740953445f,
};

struct state {
    struct bman_state b;
    struct sos gain_c;
    struct sos_state gs;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    struct bman_knobs kn;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    bman_knobs(&kn, k);
    bman_init(&s->b, &kn, PRES_K, PRES_C);
    gain2_set(&s->gain_c, s->b.gain, s->b.gain, GAIN_K);
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
    struct state *s = state;
    struct bman_state *b = &s->b;
    struct bman_knobs kn;
    bman_knobs(&kn, io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            bman_glide(b, &kn);
        return;
    }
    bman_presence_update(b, &kn, PRES_K, PRES_C);
    for (int i = 0; i < n; i++) {
        bman_glide_pre(b, &kn);
        if (b->gain != kn.gain)
            gain2_set(&s->gain_c, b->gain, b->gain, GAIN_K);
        bman_tone_update(b, &kn);
        float v = sos_run_div(&s->gain_c, &s->gs, bman_pre(b, p[2 * i]));
        x[i] = bman_tone_env(b, v, &k[i]);
    }
    bman_post_block(b, x, k, n);
    for (int i = 0; i < n; i++) {
        b->volume = st_glide(b->volume, kn.volume);
        p[2 * i] = p[2 * i + 1] = bman_out(b, x[i], 49.100799560546875f);
    }
}

const struct wvfx_desc bman_bri_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x24),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "B-Man Bri",
    .category = "B-Man",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
