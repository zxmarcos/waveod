/*
 * Doctor CL (AMP › Doctor): the original's Doctor CL amp (QME-200 V1.2.2, library entry 4.19, id 0x0700001B) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. The shared pieces are in doctor.h; this model: Gain x 0.9, the stage
 * +-1.275 (k 0.000734) on 4 points per sample + FIR, the Tone Cut summed from b1, x 2.82 out.
 */
#include "doctor.h"

static const struct wvfx_param params[] = DOCTOR_PARAMS(35.0f);

struct state {
    struct doctor_state d;
    struct stage st;
};

static void knobs(struct doctor_knobs *o, const float *k)
{
    doctor_knobs(o, k);
    o->gain = o->gain * 0.8999999761581421f;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    struct doctor_knobs kn;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    knobs(&kn, k);
    doctor_init(&s->d, &kn);
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
    struct doctor_knobs kn;
    knobs(&kn, io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    struct doctor_knobs *a = &s->d.at;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            doctor_glide(&s->d, &kn);
        return;
    }
    for (int i = 0; i < n; i++) {
        a->gain = st_glide(a->gain, kn.gain);
        a->bass = st_glide(a->bass, kn.bass);
        a->middle = st_glide(a->middle, kn.middle);
        a->treble = st_glide(a->treble, kn.treble);
        if (a->bass != kn.bass || a->middle != kn.middle || a->treble != kn.treble)
            doctor_tone_set(&s->d.tone, a->bass, a->middle, a->treble);
        x[i] = doctor_pre(&s->d, p[2 * i]);
    }
    stage_pow_block(&s->st, x, n, 1.274999976158142f, 0.0007344726473093033f);
    for (int i = 0; i < n; i++) {
        a->cut = st_glide(a->cut, kn.cut);
        a->volume = st_glide(a->volume, kn.volume);
        if (a->cut != kn.cut)
            doctor_cut_set(&s->d, a->cut);
        float v = fos_run_b1_div(&s->d.cut_c, s->d.cut_a0, &s->d.cut, x[i]);
        p[2 * i] = p[2 * i + 1] = doctor_post(&s->d, v, 2.819999933242798f);
    }
}

const struct wvfx_desc doctor_cl_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x1B),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Doctor CL",
    .category = "Doctor",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
