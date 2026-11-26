/*
 * Master Dist (DRIVE › Distortion): the original's Master Dist (QME-200 V1.2.2, library entry 3.17, id 0x0300002E) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision except the Contour section (coefficients, state and arithmetic in double).
 *
 * Per sample: a 3rd-order gain section (Gain), a fixed 2nd-order section (no x1 term), x (4.3 Gain + 0.5), the soft
 * clipper 1 - e^(-2 x) / e^(2 x) - 1 on 4 points per sample with the anti-aliasing FIR, a 3rd-order Bass/Treble
 * section (Bass pow2, Treble pow5), a fixed 2nd-order section, the 3rd-order Contour section (pow0_1) in double, a
 * 2nd-order level section with no x1 term (Volume), five fixed 2nd-order sections, x 13. All five knobs glide 0.0001
 * per sample, only while the block is on; the sections follow (a step short, the level section to the last step).
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Contour", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
};

/* the sections with no x1 term: b0 x + b2 x2 (from b2), y1 a1 + y2 a2 */
static const struct sos PF1 = { -20.971500396728516f, 0.0f, 20.971500396728516f, 1.0f, -1.4078069925308228f,
                                0.44367000460624695f };
static const struct sos PF2 = {
    1.0003050565719604f, -1.4078069925308228f, 0.4433651864528656f, 1.0f, -1.4078069925308228f, 0.44367000460624695f,
};
static const struct sos POST[5] = {
    { 0.530843198299408f, -1.000686526298523f, 0.47172459959983826f, 1.0f, -1.962479591369629f, 0.96302729845047f },
    { 0.6905918717384338f, -1.36329984664917f, 0.6730185151100159f, 1.0f, -1.8893146514892578f, 0.892074704170227f },
    { 1.36329984664917f, 0.09685300290584564f, 0.5984007120132446f, 1.0f, -0.017908500507473946f,
      0.33747440576553345f },
    { 1.36329984664917f, -0.8712630271911621f, 0.2919211983680725f, 1.0f, 0.18135100603103638f,
      0.024113699793815613f },
    { 1.36329984664917f, 1.2152643203735352f, 0.41504350304603577f, 1.0f, 1.1082313060760498f, 0.467800498008728f },
};
#define LEVEL_A1 -1.7953039407730103f
#define LEVEL_A2 0.7955135703086853f

/* the Contour section, in double */
struct sos3d { double b0, b1, b2, b3, a0, a1, a2, a3; };
struct sos3d_state { double x1, x2, x3, y1, y2, y3; };

static inline double sos3d_run_div(const struct sos3d *c, struct sos3d_state *s, double x)
{
    double n = c->b0 * x, d = c->a1 * s->y1;
    n = st_fmad(c->b1, s->x1, n);
    d = st_fmad(c->a2, s->y2, d);
    n = st_fmad(c->b2, s->x2, n);
    d = st_fmad(c->a3, s->y3, d);
    n = st_fmad(c->b3, s->x3, n);
    double y = (n - d) / c->a0;
    s->x3 = s->x2; s->x2 = s->x1; s->x1 = x;
    s->y3 = s->y2; s->y2 = s->y1; s->y1 = y;
    return y;
}

struct state {
    float gain, volume, bass, contour, treble;  /* glided: Gain and Volume 0-1, the rest tapers */
    struct sos3 gc, eq;
    struct sos3d cc;
    struct sos lc;
    struct sos3_state gs, es;
    struct sos3d_state cs;
    struct sos_state p1, p2, ls, post[5];
    struct stage st;
};

/* the sections with no x1 term */
static inline float nox1_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b0, x, c->b2 * s->x2);
    float d = st_fma(s->y2, c->a2, s->y1 * c->a1);
    return sos_out(s, x, n - d);
}

/* the gain section from g (0-1); the Init's from the raw knob G, its own factors */
static void gain_set(struct sos3 *c, float g)
{
    *c = (struct sos3){ st_fma(g, 26.54142951965332f, 1.1325030326843262f),
                        st_fma(g, -28.028310775756836f, -0.5533064007759094f),
                        st_fma(g, -23.567659378051758f, -1.1325030326843262f),
                        st_fma(g, 25.054550170898438f, 0.5533064007759094f), g + 1.1337870359420776f,
                        st_fma(g, -2.486304998397827f, -0.5513656735420227f),
                        st_fma(g, 1.9737670421600342f, -1.1324729919433594f),
                        st_fma(g, -0.4874629080295563f, 0.5526790022850037f) };
}

static void eq_set(struct sos3 *c, float b, float t)
{
    float tb = t * b;
    *c = (struct sos3){ (st_fma(b, 0.29057571291923523f, 0.0010791720123961568f) + t * 0.005707991775125265f) + tb,
                        st_fma(b, -0.6612561941146851f, -0.001041676034219563f)
                            - (t * 0.005629876162856817f + tb * 3.0f),
                        (st_fma(b, 0.45765960216522217f, -0.0010791720123961568f) - t * 0.005707991775125265f)
                            + tb * 3.0f,
                        (st_fma(b, -0.08697906881570816f, 0.001041676034219563f) - tb) + t * 0.005629876162856817f,
                        st_fma(b, 3.236711025238037f, 0.01781889982521534f),
                        st_fma(b, -9.347811698913574f, -0.0168424304574728f),
                        st_fma(b, 8.993877410888672f, -0.01780718006193638f),
                        st_fma(b, -2.8827760219573975f, 0.01685415953397751f) };
}

static void contour_set(struct sos3d *c, float k)
{
    double d = (double)k, d2 = d * d, k2 = (double)(k * k);
    *c = (struct sos3d){ st_fmad(d, -1.142089233388, -0.0012961712247574) + k2,
                         st_fmad(d, 3.1301322388431, 0.000957468883106) + d2 * -2.7067964581012,
                         st_fmad(d, -2.8347465139878, 0.0012945673731648) + d2 * 2.4135929162024,
                         st_fmad(d, 0.84670350853268, -0.00095907273469867) + d2 * -0.70679645810121,
                         st_fmad(d, -1.1904678178013, -0.002543757618991) + k2,
                         st_fmad(d, 3.1785108232564, 0.0040827314166832) + d2 * -2.7067964581012,
                         st_fmad(d, -2.7863679295745, -0.0014346819799775) + d2 * 2.4135929162024,
                         st_fmad(d, 0.79832492411938, -0.00010749952090003) + d2 * -0.70679645810121 };
}

/* 1 - e^(-2 x) above zero, e^(2 x) - 1 below: e^(-2 |x|) either way (amp.h, exp_negabs), the side by x's sign bit
   (at +0 both give 0) */
static inline float clip(float x)
{
    float e = exp_negabs(x, 2.0f);
    return pick_sign(x, 1.0f - e, e + -1.0f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    float G = k[0], V = k[1];
    s->gain = G * 0.01f;
    s->volume = V * 0.01f;
    s->bass = st_taper(st_pow2, k[2]);
    s->contour = st_taper(st_pow0_1, k[3]);
    s->treble = st_taper(st_pow5, k[4]);
    s->gc = (struct sos3){ st_fma(G, 0.265414297580719f, 1.1325030326843262f),
                           st_fma(G, -0.2802830934524536f, -0.5533064007759094f),
                           st_fma(G, -0.2356765866279602f, -1.1325030326843262f),
                           st_fma(G, 0.2505455017089844f, 0.5533064007759094f), G * 0.01f + 1.1337870359420776f,
                           st_fma(G, -0.024863049387931824f, -0.5513656735420227f),
                           st_fma(G, 0.019737670198082924f, -1.1324729919433594f),
                           st_fma(G, -0.004874628968536854f, 0.5526790022850037f) };
    eq_set(&s->eq, s->bass, s->treble);
    contour_set(&s->cc, s->contour);
    s->lc = (struct sos){ V * -0.0010178049560636282f, 0.0f, V * 0.0010178049560636282f, 1.0f, LEVEL_A1, LEVEL_A2 };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    if (io->frames == 0 || !io->enabled)
        return;
    float gt = io->knobs[0] * 0.01f, vt = io->knobs[1] * 0.01f, bt = st_taper(st_pow2, io->knobs[2]);
    float ct = st_taper(st_pow0_1, io->knobs[3]), tt = st_taper(st_pow5, io->knobs[4]);
    /* stage by stage over the block: the Gain's glide before the stage, the others after it (each once per sample,
       its sections following it); the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        float g = st_glide(s->gain, gt);
        if (g != gt)
            gain_set(&s->gc, g);
        float v = nox1_run(&PF1, &s->p1, sos3_run_div(&s->gc, &s->gs, p[2 * i]));
        s->gain = g;
        x[i] = v * st_fma(g, 4.300000190734863f, 0.5f);
    }
    STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip(v));
    stage_block_fir(&s->st, buf, x, n);
    for (int i = 0; i < n; i++) {
        s->bass = st_glide(s->bass, bt);
        s->contour = st_glide(s->contour, ct);
        float t = st_glide(s->treble, tt);
        if (s->bass != bt || t != tt)
            eq_set(&s->eq, s->bass, t);
        if (s->contour != ct)
            contour_set(&s->cc, s->contour);
        if (s->volume != vt) {
            s->volume = st_glide(s->volume, vt);
            s->lc = (struct sos){ s->volume * -0.1017804965376854f, 0.0f, s->volume * 0.1017804965376854f, 1.0f,
                                  LEVEL_A1, LEVEL_A2 };
        }
        s->treble = t;
        float v = sos_run(&PF2, &s->p2, sos3_run_div_b1(&s->eq, &s->es, x[i]));
        v = (float)sos3d_run_div(&s->cc, &s->cs, (double)v);
        x[i] = nox1_run(&s->lc, &s->ls, v);
    }
    for (int k = 0; k < 5; k++)
        sos_block(&POST[k], &s->post[k], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 13.0f;
}

const struct wvfx_desc master_dist_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x111),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Master Dist",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
