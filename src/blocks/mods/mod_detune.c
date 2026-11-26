/*
 * Detune (MOD › Pitch): the original's Detune (QME-200 V1.2.2, library entry 8.21, id 0x01000029) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Each input shifted by a few cents on its own: the left
 * output gets the left one, the right output the right one, both over the dry left input. Single precision with
 * fixed-point phases.
 *
 * The shifter: a 4096-sample line read by two taps half a period apart; each tap's phase (20 bits: 12 whole samples
 * and 8 fraction bits) moves by 256 STEP[Detune] per sample and wraps at 2^20; each reads between two samples and is
 * weighted by the window at its phase (interpolated in 1/128 steps), the sum x 3 / 65536 through a fixed 1st-order
 * low pass. Out: Dry x the left input + 0.7 Wet x the shifted signal. Wet and Dry glide 0.005 per process call, also
 * while bypassed.
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Detune", -50.0f, 50.0f, 1.0f, -25.0f, 0, 0x58, 0x00, 0, 0, 0 },
    { "Wet", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Dry", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
};

/* the phase step per sample (in 1/256 sample, before x 256) per Detune value, -50 .. 50 */
static const float STEP[101] = {
    0.028468100354075432f, 0.02790669910609722f, 0.027344999834895134f, 0.02678300067782402f, 0.02622069977223873f,
    0.025658100843429565f, 0.025095099583268166f, 0.02453180029988289f, 0.023968199267983437f, 0.02340419963002205f,
    0.022840000689029694f, 0.022275399416685104f, 0.021710500121116638f, 0.021145200356841087f, 0.02057969942688942f,
    0.020013799890875816f, 0.01944749988615513f, 0.018881000578403473f, 0.018314100801944733f, 0.017746899276971817f,
    0.017179399728775024f, 0.016611499711871147f, 0.016043299809098244f, 0.015474800020456314f, 0.0149058997631073f,
    0.014336800202727318f, 0.013767199590802193f, 0.0131973996758461f, 0.012627200223505497f, 0.012056799605488777f,
    0.011485899798572063f, 0.010914799757301807f, 0.010343300178647041f, 0.009771499782800674f, 0.009199299849569798f,
    0.008626899681985378f, 0.008054099977016449f, 0.007480899803340435f, 0.006907499860972166f, 0.0063336999155581f,
    0.005759499967098236f, 0.00518509978428483f, 0.004610300064086914f, 0.004035099875181913f, 0.0034596999175846577f,
    0.0028838999569416046f, 0.0023078001104295254f, 0.0017313000280410051f, 0.0011544999433681369f,
    0.0005773999728262424f, 0.0f, -0.0005776999751105905f, -0.0011558999540284276f, -0.0017343000508844852f,
    -0.0023131000343710184f, -0.0028921999037265778f, -0.003471700008958578f, -0.004051500000059605f,
    -0.004631599877029657f, -0.005212100222706795f, -0.005792899988591671f, -0.006374000106006861f,
    -0.006955500226467848f, -0.007537300232797861f, -0.008119500242173672f, -0.008701900020241737f,
    -0.009284799918532372f, -0.009867900051176548f, -0.010451399721205235f, -0.011035200208425522f,
    -0.01161940023303032f, -0.012203900143504143f, -0.012788699939846992f, -0.013373900204896927f,
    -0.013959400355815887f, -0.014545300044119358f, -0.015131499618291855f, -0.015718000009655952f,
    -0.016304900869727135f, -0.016892099753022194f, -0.01747960038483143f, -0.0180674996227026f, -0.018655799329280853f,
    -0.019244300201535225f, -0.019833199679851532f, -0.020422499626874924f, -0.02101209945976734f,
    -0.021601999178528786f, -0.022192299365997314f, -0.02278289943933487f, -0.02337379939854145f,
    -0.023965099826455116f, -0.024556800723075867f, -0.025148799642920494f, -0.025741100311279297f,
    -0.026333700865507126f, -0.02692670002579689f, -0.02752009965479374f, -0.028113799169659615f,
    -0.028707800433039665f, -0.02930220030248165f,
};
/* the crossfade window over a tap's phase (Q15-ish integers; the reachable 258 of 513) */
static const int32_t WINDOW[258] = {
    0, 403, 806, 1208, 1611, 2013, 2415, 2817, 3218, 3619, 4019, 4419, 4817, 5216, 5613, 6009, 6405, 6800, 7193, 7586,
    7977, 8367, 8756, 9144, 9530, 9915, 10298, 10680, 11060, 11438, 11815, 12190, 12563, 12934, 13303, 13671, 14036,
    14399, 14760, 15118, 15474, 15828, 16180, 16529, 16876, 17220, 17561, 17900, 18236, 18570, 18900, 19228, 19553,
    19875, 20193, 20509, 20822, 21131, 21438, 21741, 22041, 22337, 22630, 22920, 23206, 23489, 23768, 24043, 24315,
    24584, 24848, 25109, 25366, 25619, 25868, 26114, 26355, 26593, 26826, 27055, 27281, 27502, 27719, 27931, 28140,
    28344, 28544, 28740, 28931, 29118, 29301, 29479, 29653, 29822, 29987, 30147, 30303, 30454, 30600, 30742, 30879,
    31011, 31139, 31262, 31381, 31494, 31603, 31707, 31807, 31901, 31991, 32075, 32155, 32231, 32301, 32366, 32427,
    32482, 32533, 32579, 32619, 32655, 32686, 32712, 32733, 32749, 32760, 32767, 32767, 32764, 32755, 32742, 32723,
    32700, 32671, 32638, 32600, 32556, 32508, 32455, 32397, 32334, 32266, 32194, 32116, 32034, 31946, 31854, 31757,
    31656, 31549, 31438, 31322, 31201, 31076, 30946, 30811, 30671, 30527, 30379, 30225, 30067, 29905, 29738, 29567,
    29391, 29210, 29025, 28836, 28643, 28445, 28243, 28036, 27826, 27611, 27392, 27168, 26941, 26710, 26474, 26235,
    25991, 25744, 25493, 25238, 24979, 24716, 24450, 24180, 23906, 23629, 23348, 23063, 22775, 22484, 22189, 21891,
    21590, 21285, 20977, 20666, 20352, 20034, 19714, 19391, 19064, 18735, 18403, 18069, 17731, 17391, 17048, 16703,
    16355, 16005, 15652, 15297, 14939, 14579, 14218, 13853, 13487, 13119, 12749, 12377, 12003, 11627, 11249, 10870,
    10489, 10107, 9723, 9337, 8950, 8562, 8172, 7782, 7390, 6997, 6603, 6207, 5811, 5414, 5017, 4618, 4219, 3819, 3419,
    3018, 2616, 2214, 1812, 1410, 1007, 604, 201, 0, -201,
};

#define LEN 4096
#define WRAP 0x100000

static const struct fos LP = { 0.5347844362258911f, 0.5347844362258911f, 0.06956888735294342f };

struct shifter {
    float buf[LEN];
    int w, phase[2];
    struct fos_state lp;
};

struct state {
    float wet, dry;
    struct shifter ch[2];
};

/* one tap: advance its phase by step and read it between two samples; its window weight in *gain */
static float tap(const struct shifter *s, int *phase, int w, float step, float *gain)
{
    int p = (int)(step + (float)*phase);
    if (p > WRAP)
        p -= WRAP;
    else if (p < 0)
        p += WRAP;
    *phase = p;
    int i = w - (p >> 8);
    if (i < 0)
        i += LEN;
    int older = i < 1 ? i + LEN - 1 : i - 1;
    float frac = (float)(p & 0xff);
    float v = st_fma(s->buf[older], frac, (256.0f - frac) * s->buf[i]) * 0.00390625f;
    int k = p >> 12, f = (p >> 5) & 0x7f;
    *gain = (float)(((f * WINDOW[k + 1]) >> 7) + ((WINDOW[k] * (0x80 - f)) >> 7));
    return v;
}

static float shift(struct shifter *s, float x, int range)
{
    float step = STEP[range + 50] * 256.0f;
    s->buf[s->w] = x;
    s->w = s->w + 1 >= LEN ? 0 : s->w + 1;
    float ga, gb, a = tap(s, &s->phase[0], s->w, step, &ga), b = tap(s, &s->phase[1], s->w, step, &gb);
    return fos_run(&LP, &s->lp, st_fma(b, gb, a * ga) * 4.57763671875e-05f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->wet = k[1] * 0.01f;
    s->dry = k[2] * 0.01f;
    for (int c = 0; c < 2; c++)
        s->ch[c].phase[1] = 0x80000;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    s->wet = st_glide_by(s->wet, k[1] * 0.01f, 0.005f);
    s->dry = st_glide_by(s->dry, k[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    int range = (int)k[0];
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float l = shift(&s->ch[0], p[0], range), r = shift(&s->ch[1], p[1], range);
        float dry = s->dry * p[0], wet = s->wet * 0.699999988079071f;
        p[0] = st_fma(wet, l, dry);
        p[1] = st_fma(wet, r, dry);
    }
}

const struct wvfx_desc mod_detune_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x115),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Detune",
    .category = "Pitch",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
