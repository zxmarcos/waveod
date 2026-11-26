/*
 * B-Man N (AMP › B-Man): the original's B-Man N amp (QME-200 V1.2.2, library entry 4.1, id 0x07000003) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout. The shared pieces are in bman.h; this model:
 *   a 1st-order gain section from Gain, (0.9994 g, -0.9994 g) / (1, -0.9989) (divided by its a0 of 1), the
 *   presence's K 0.787 and middle terms -0.656, x 76.6.
 */
#include "bman.h"

static const struct wvfx_param params[] = BMAN_PARAMS(30.0f);

#define PRES_K 0.7871752977371216f
#define PRES_C -0.6563876271247864f

struct state {
    struct bman_state b;
    struct fos gain_c;
    struct fos_state gs;
};

static void gain_set(struct fos *c, float g)
{
    c->b0 = g * 0.9994333982467651f;
    c->b1 = g * -0.9994333982467651f;
    c->a1 = -0.9988669157028198f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    struct bman_knobs kn;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    bman_knobs(&kn, k);
    bman_init(&s->b, &kn, PRES_K, PRES_C);
    gain_set(&s->gain_c, s->b.gain);
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
            gain_set(&s->gain_c, b->gain);
        bman_tone_update(b, &kn);
        float v = fos_run_div(&s->gain_c, 1.0f, &s->gs, bman_pre(b, p[2 * i]));
        x[i] = bman_tone_env(b, v, &k[i]);
    }
    bman_post_block(b, x, k, n);
    for (int i = 0; i < n; i++) {
        b->volume = st_glide(b->volume, kn.volume);
        p[2 * i] = p[2 * i + 1] = bman_out(b, x[i], 76.60224151611328f);
    }
}

const struct wvfx_desc bman_n_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x03),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "B-Man N",
    .category = "B-Man",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
