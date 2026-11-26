/*
 * Doctor OD (AMP › Doctor): the original's Doctor OD amp (QME-200 V1.2.2, library entry 4.20, id 0x07000049) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. The shared pieces are in doctor.h; this model: the 8x oversampled stage
 * (amp.h, os8_block) with the clip +-1 (k 1 / 40.96) on 8 v, the Tone Cut summed from b0, x 1.032 out.
 */
#include "doctor.h"

static const struct wvfx_param params[] = DOCTOR_PARAMS(30.0f);

struct state {
    struct doctor_state d;
    struct os8 nl;
};

static float clip(float v)
{
    return clip_pow(v * 8.0f, 1.0f, 0.0244140625f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    struct doctor_knobs kn;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    doctor_knobs(&kn, k);
    doctor_init(&s->d, &kn);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    struct doctor_knobs kn;
    doctor_knobs(&kn, io->knobs);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    struct doctor_knobs *a = &s->d.at;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++)
            doctor_glide(&s->d, &kn);
        return;
    }
    /* stage by stage over the block, each knob's glide (doctor_glide's) once per sample where it is used and the
       coefficients following it (doctor_update's): the same samples as sample by sample */
    for (int i = 0; i < n; i++) {
        a->gain = st_glide(a->gain, kn.gain);
        a->bass = st_glide(a->bass, kn.bass);
        a->middle = st_glide(a->middle, kn.middle);
        a->treble = st_glide(a->treble, kn.treble);
        if (a->bass != kn.bass || a->middle != kn.middle || a->treble != kn.treble)
            doctor_tone_set(&s->d.tone, a->bass, a->middle, a->treble);
        x[i] = doctor_pre(&s->d, p[2 * i]);
    }
    os8_block(&s->nl, x, n, clip);
    for (int i = 0; i < n; i++) {
        a->cut = st_glide(a->cut, kn.cut);
        a->volume = st_glide(a->volume, kn.volume);
        if (a->cut != kn.cut)
            doctor_cut_set(&s->d, a->cut);
        float v = fos_run_div(&s->d.cut_c, s->d.cut_a0, &s->d.cut, x[i]);
        p[2 * i] = p[2 * i + 1] = doctor_post(&s->d, v, 1.0321120023727417f);
    }
}

const struct wvfx_desc doctor_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x49),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Doctor OD",
    .category = "Doctor",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
