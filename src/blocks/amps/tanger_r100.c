/*
 * Tanger R100 (AMP › Tanger): the original's Tanger R100 amp (QME-200 V1.2.2, library entry 4.48, id 0x07000053) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h has the pieces):
 *   input         x Gain (B25) x 49
 *   pre-filters   three fixed 2nd-order sections
 *   stage         8x oversampled (amp.h, os8_block), the clipper 0.9842 |w| / (|w| + 0.06552) on w = 8 v (odd)
 *   tone stack    3rd order from Bass (A25_0_0_8), Middle and Treble (B25)
 *   output        x Volume (B25) x 1.2556
 * Gain, Volume and Treble glide 0.0001 per sample, Bass and Middle take effect at once; the tone coefficients stop
 * one glide step short of Treble.
 */
#include "amp.h"

enum { K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOB("Gain", K_GAIN), KNOB("Volume", K_VOLUME), KNOB("Bass", K_BASS), KNOB("Middle", K_MIDDLE),
    KNOB("Treble", K_TREBLE),
};

static const struct sos PF[3] = {
    { 0.38927119970321655f, -0.7767999768257141f, 0.38753071427345276f, 1.0f, -1.9818987846374512f,
      0.9820443987846375f },
    { 0.7767999768257141f, -0.5849732756614685f, 0.033059198409318924f, 1.0f, -0.4771570861339569f,
      0.018636299297213554f },
    { 0.7767999768257141f, 0.40375879406929016f, 0.02768459916114807f, 1.0f, -0.9290854930877686f,
      0.12703880667686462f },
};

struct state {
    float gain, volume, treble;     /* glided */
    float bass, middle;             /* the knobs the tone stack was set for */
    struct sos_state pf[3];
    struct os8 nl;
    struct tone3 tone;
};

/*
 * The tone stack from Bass b, Middle m and Treble t (bt = b t, bm = b m, mt = m t, m2 = m^2; the products by t and
 * m fused into the constants first):
 *   n0 = ((((0.000922 + 0.0521 t) + 0.0404 m) + 0.0184 b) + bt - 0.0397 m2) - 0.05 mt + 0.794 bm
 *   n1 = ((((-0.000812 - 0.152 t) - 0.114 m) - 0.0162 b) + 0.15 mt + 0.114 m2 - 2.276 bm) - 3 bt
 *   n2 = ((-0.000922 + 0.148 t + 0.108 m) - 0.108 m2) - (0.0184 b + 0.15 mt) + 2.170 bm + 3 bt
 *   n3 = ((0.000812 - 0.0480 t - 0.0338 m) - bt) + 0.0162 b + 0.05 mt + 0.0344 m2 - 0.688 bm
 *   di = (ki + mi m + bi b) + m2 term + bm term
 */
static void tone_set(struct tone3 *o, float b, float m, float t)
{
    float m2 = m * m, bm = b * m, bt = b * t, mt = m * t, bt3 = bt * 3.0f;
    float bk0 = b * 0.018434559926390648f, bk1 = b * 0.01623976044356823f;
    float mt05 = mt * 0.05000000074505806f, mt15 = mt * 0.15000000596046448f;
    float a0 = st_fma(m, 0.04042724147439003f, st_fma(t, 0.05207505077123642f, 0.0009217279148288071f));
    float a1 = st_fma(m, -0.11442279815673828f, st_fma(t, -0.152047798037529f, -0.0008119880803860724f));
    float a2 = st_fma(m, 0.107777900993824f, st_fma(t, 0.14792490005493164f, -0.0009217279148288071f));
    float a3 = st_fma(m, -0.033782340586185455f, st_fma(t, -0.04795219004154205f, 0.0008119880803860724f));
    float M0 = m2 * -0.039713021367788315f, M1 = m2 * 0.11381559818983078f;
    float M2 = m2 * -0.10849209874868393f, M3 = m2 * 0.03438954055309296f;
    float B0 = bm * 0.7942603826522827f, B1 = bm * -2.2763121128082275f;
    float B2 = bm * 2.169842004776001f, B3 = bm * -0.6877908706665039f;
    o->b[0] = ((((a0 + bk0) + bt) + M0) - mt05) + B0;
    o->b[1] = ((((a1 - bk1) + mt15) + M1) + B1) - bt3;
    o->b[2] = (((a2 + M2) - (bk0 + mt15)) + B2) + bt3;
    o->b[3] = ((((a3 - bt) + bk1) + mt05) + M3) + B3;
    o->a[0] = (st_fma(b, 1.0994189977645874f, st_fma(m, -0.013621999882161617f, 0.05721405893564224f)) + M0) + B0;
    o->a[1] = (st_fma(b, -3.0972249507904053f, st_fma(m, 0.03962644934654236f, -0.15673880279064178f)) + M1) + B1;
    o->a[2] = (st_fma(b, 2.900580883026123f, st_fma(m, -0.038172878324985504f, 0.1427903026342392f)) + M2) + B2;
    o->a[3] = (st_fma(b, -0.9027755260467529f, st_fma(m, 0.012168419547379017f, -0.04325680062174797f)) + M3) + B3;
}

static float clip(float v)
{
    float w = v * 8.0f, a = st_abs(w);
    float r = (a * 0.9842000007629395f) / (a + 0.06552000343799591f);
    return clip_sign(r, w);                     /* w < 0 ? -r : r */
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_b25, k[K_GAIN]);
    s->volume = st_taper(st_b25, k[K_VOLUME]);
    s->bass = st_taper(st_a25_0_0_8, k[K_BASS]);
    s->middle = st_taper(st_b25, k[K_MIDDLE]);
    s->treble = st_taper(st_b25, k[K_TREBLE]);
    tone_set(&s->tone, s->bass, s->middle, s->treble);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_b25, io->knobs[K_GAIN]), volume = st_taper(st_b25, io->knobs[K_VOLUME]);
    float bass = st_taper(st_a25_0_0_8, io->knobs[K_BASS]), middle = st_taper(st_b25, io->knobs[K_MIDDLE]);
    float treble = st_taper(st_b25, io->knobs[K_TREBLE]);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    /* stage by stage over the block (each glide once per sample where it is used): the same samples */
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        x[i] = s->gain * 49.0f * p[2 * i];
    }
    for (int j = 0; j < 3; j++)
        sos_block(&PF[j], &s->pf[j], x, n);
    os8_block(&s->nl, x, n, clip);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->treble = st_glide(s->treble, treble);
        if (s->bass != bass || s->middle != middle || s->treble != treble) {
            s->bass = bass;
            s->middle = middle;
            tone_set(&s->tone, s->bass, s->middle, s->treble);
        }
        p[2 * i] = p[2 * i + 1] = s->volume * 1.2555841207504272f * tone3_run_std(&s->tone, x[i]);
    }
}

const struct wvfx_desc tanger_r100_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x53),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Tanger R100",
    .category = "Tanger",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
