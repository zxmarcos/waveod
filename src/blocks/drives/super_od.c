/*
 * Super OD (DRIVE › OD): the original's Super OD (QME-200 V1.2.2, library entry 3.4, id 0x03000006) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision throughout.
 *
 * Per sample: a fixed 2nd-order pre-filter, the Butter OD's gain section and clipper (drive.h; Gain, pow1_5) with the
 * FIR summed onto the pre-filter's output, a 3rd-order tone section (Tone, pow2), x 0.8 Volume, four fixed
 * 2nd-order sections. Volume glides 0.005 per block (also while bypassed); Gain and Tone are read as they are, only
 * while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

static const struct sos PRE = {
    0.9474372863769531f, -1.6538771390914917f, 0.7064399123191833f, 1.0f, -1.7313443422317505f, 0.7357801198959351f,
};
static const struct sos POST[4] = {
    { 0.6423919200897217f, -1.1779584884643555f, 0.535801112651825f, 1.0f, -1.9003119468688965f, 0.9014434218406677f },
    { 0.7553991079330444f, -1.4789042472839355f, 0.7241715788841248f, 1.0f, -1.9143656492233276f,
      0.9152731895446777f },
    { 1.4789042472839355f, -1.3620502948760986f, 0.2232545018196106f, 1.0f, -0.7326539158821106f,
      0.06249520182609558f },
    { 1.4789042472839355f, -0.023221800103783607f, 0.00020929999300278723f, 1.0f, -0.13560639321804047f,
      0.013966900296509266f },
};

struct state {
    float drive, tone, level;   /* Gain and Tone as read (tapers), Volume glided (0-1) */
    struct fos gain;
    struct sos3 tc;
    struct sos_state pre, post[4];
    struct fos_state gs;
    struct sos3_state ts;
    struct stage st;
};

static void tone_set(struct sos3 *c, float t)
{
    float t2 = t * t, u = t * 0.06227283924818039f, v = t * 0.04417027160525322f;
    *c = (struct sos3){ st_fma(t2, 0.059255748987197876f, -0.0026366470847278833f - u),
                        st_fma(t2, -0.04718736931681633f, v + -0.0003886219928972423f),
                        st_fma(t2, -0.059255748987197876f, u + 0.0021298700012266636f),
                        st_fma(t2, 0.04718736931681633f, -0.00011815469770226628f - v),
                        st_fma(t, -0.9703720808029175f, -0.07412392646074295f) + t2,
                        st_fma(t2, -2.677823066711426f, st_fma(t, 2.654228925704956f, 0.10075409710407257f)),
                        st_fma(t2, 2.37978196144104f, st_fma(t, -2.409409999847412f, -0.005807721987366676f)),
                        st_fma(t2, -0.7019593119621277f, st_fma(t, 0.7255529761314392f, -0.021836010739207268f)) };
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->drive = st_taper(st_pow1_5, k[0]);
    s->tone = st_taper(st_pow2, k[1]);
    s->level = k[2] * 0.01f;
    bod_gain_set(&s->gain, s->drive);
    tone_set(&s->tc, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    s->level = st_glide_by(s->level, io->knobs[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float d = st_taper(st_pow1_5, io->knobs[0]), t = st_taper(st_pow2, io->knobs[1]);
    if (s->drive != d) {
        s->drive = d;
        bod_gain_set(&s->gain, d);
    }
    if (s->tone != t) {
        s->tone = t;
        tone_set(&s->tc, t);
    }
    float level = s->level * 0.800000011920929f;
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
        y[i] = level * sos3_run_div_b1(&s->tc, &s->ts, y[i]);
    for (int k = 0; k < 4; k++)
        sos_block(&POST[k], &s->post[k], y, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = y[i];
}

const struct wvfx_desc super_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x104),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Super OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
