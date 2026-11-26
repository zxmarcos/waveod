/*
 * Butter OD (DRIVE › OD): the original's Butter OD (QME-200 V1.2.2, library entry 3.2, id 0x03000002) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: a fixed 2nd-order pre-filter, a 1st-order gain section (Gain, pow1_5), x 3, an asymmetric soft clipper
 * on 4 points per sample (0.005 x + 0.995 A (1 - e^(-x)) above zero, 0.005 x + 0.995 A (1 - e^(2x)) below; the levels
 * A follow Gain), the anti-aliasing FIR summed onto the pre-filter's output (the clean part), a fixed 1st-order tone
 * section, a 1st-order level section (Volume). Volume glides 0.005 per block (also while bypassed) and its section
 * follows the glide; Gain is read as it is, only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
};

static const struct sos PRE = {
    0.9525279998779297f, -1.5326803922653198f, 0.5801537036895752f, 1.0f, -1.5475690364837646f, 0.5724462270736694f,
};
static const struct fos TONE = { -0.059255748987197876f, -0.059255748987197876f, -0.8814885020256042f };

struct state {
    float drive, level;         /* Gain as read (taper), Volume glided (0-1) */
    struct fos gain, out;       /* the gain and level sections */
    struct sos_state pre;
    struct fos_state gs, tone, os;
    struct stage st;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->drive = st_taper(st_pow1_5, k[0]);
    s->level = k[1] * 0.01f;
    bod_gain_set(&s->gain, s->drive);
    s->out = (struct fos){ k[1] * 0.0067974780686199665f, k[1] * -0.0067974780686199665f, -0.9984586238861084f };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float target = io->knobs[1] * 0.01f;
    s->level = st_glide_by(s->level, target, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float d = st_taper(st_pow1_5, io->knobs[0]);
    if (s->drive != d) {
        s->drive = d;
        bod_gain_set(&s->gain, d);
    }
    if (s->level != target)
        s->out = (struct fos){ s->level * 0.6797478199005127f, s->level * -0.6797478199005127f, -0.9984586238861084f };
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE, &s->pre, x, n);
    for (int i = 0; i < n; i++)
        y[i] = fos_run(&s->gain, &s->gs, x[i]);
    bod_stage_block(&s->st, y, n, d, x);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = fos_run(&s->out, &s->os, fos_run_b1(&TONE, &s->tone, y[i]));
}

const struct wvfx_desc butter_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x102),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Butter OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
