/*
 * Voks 15TB (AMP › Voks): the original's Voks 15TB amp (QME-200 V1.2.2, library entry 4.7, id 0x07000010) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 *
 * Per sample (amp.h, voks.h have the pieces):
 *   pre-filter    two fixed 2nd-order sections
 *   gain          a 2nd-order section from Gain (pow2 x 0.99), a fixed 2nd-order section
 *   tone stack    3rd order from Bass (x 0.009) and Treble (pow2), computed in double, not divided
 *   stage         x 0.01, soft clip 40 - 40 e^(-30 |x|), odd, on 4 points per sample (the current one through
 *                 e^(-0.3 |x / 0.01|)), the FIR
 *   tone cut      a 1st-order section from Tone Cut (tan(1.4247586 - 1.3453283 t))
 *   power amp     six fixed 2nd-order sections, x Volume x 0.06144
 * Gain and Volume glide 0.0001 per sample, and only while the block is on; Bass, Treble and Tone Cut glide 0.004 per
 * block, the tone and tone cut sections following them. Init reads Treble from a 101-point table at (int)knob,
 * Process from the 256-point pow2 at (int)knob: after Init, Treble glides from the one to the other. Init computes
 * the gain from the taper before x 0.99 (with the factors x 0.99) and Tone Cut from the knob (x 0.013453283).
 */
#include "voks.h"

enum { K_GAIN, K_TONECUT, K_VOLUME, K_BASS, K_TREBLE };

static const struct wvfx_param params[] = {
    VOKS_PARAM("Gain", 30.0f, K_GAIN), VOKS_PARAM("Tone Cut", 60.0f, K_TONECUT), VOKS_PARAM("Volume", 50.0f, K_VOLUME),
    VOKS_PARAM("Bass", 50.0f, K_BASS), VOKS_PARAM("Treble", 50.0f, K_TREBLE),
};

static const struct sos PRE[3] = {
    { 6.664538383483887f, -9.543780326843262f, 2.8824470043182373f, 1.0f, -1.4544854164123535f,
      0.45515939593315125f },
    { 9.543780326843262f, 3.8286190032958984f, 2.8599999495781958e-05f, 1.0f, 0.303526908159256f,
      2.6000000161729986e-06f },
    { 59.45364761352539f, -109.1803970336914f, 49.734962463378906f, 1.0f, -1.8356354236602783f,
      0.8358985781669617f },                    /* after the gain */
};

static const struct sos POWER[6] = {
    { 0.6327568888664246f, -1.2642226219177246f, 0.6314672827720642f, 1.0f, -1.9885340929031372f,
      0.9887254238128662f },
    { 0.6487795114517212f, -1.2847330570220947f, 0.6362105011940002f, 1.0f, -1.994297742843628f,
      0.9943174719810486f },
    { 0.7553973197937012f, -1.2847330570220947f, 0.5844600796699524f, 1.0f, -1.7160042524337769f,
      0.8119350075721741f },
    { 1.2847330570220947f, -0.8697986006736755f, 0.15333810448646545f, 1.0f, -0.8728402853012085f,
      0.3545733094215393f },
    { 1.2847330570220947f, -0.20710429549217224f, 0.042135000228881836f, 1.0f, 0.23551639914512634f,
      0.1239096000790596f },
    { 1.2847330570220947f, 0.9438279271125793f, 0.10666880011558533f, 1.0f, 1.1498163938522339f,
      0.36381620168685913f },
};

/* the gain section's factors (gain2_set): Process's for the glided Gain, Init's for the taper before x 0.99 */
static const float GAIN_PROCESS[7] = { -1.2267570495605469f, -0.7732425928115845f, -0.954648494720459f,
                                       -0.28239259123802185f, -0.02056756056845188f, -1.0453510284423828f,
                                       0.2618251144886017f };
static const float GAIN_INIT[7] = { -1.214489459991455f, -0.7655102014541626f, -0.9451020359992981f,
                                    -0.28239259123802185f, -0.02056756056845188f, -1.0348975658416748f,
                                    0.2618251144886017f };

struct state {
    float gain, volume;                 /* glided per sample */
    float bass, treble, tonecut;        /* glided per block */
    struct sos gain_c;
    struct tone3 tone;
    struct fos tc_c;
    struct sos_state pre[3], gs, power[6];
    struct fos_state tc;
    struct stage st;
};

/* the tone stack from Bass b and Treble t, in double; its a0 divides the others (as its reciprocal in single
   precision) and is left at 1 */
static void tone_set(struct tone3 *o, float bf, float tf)
{
    double b = bf, t = tf, b2 = b * b, tb = (float)(tf * bf), tb2 = b2 * t;
    double inv = (float)(1.0 / st_fmad(b2, 1.370547, st_fmad(b, -1.381514, -0.001421007)));
    double t1 = t * 0.00103443, t2 = t * 0.001029112, q1 = tb2 * 0.9911131, q3 = tb2 * 2.973339;
    double n0 = st_fmad(b2, 0.1403464, st_fmad(b, -0.140283, -0.0001370405) - (t1 + tb)) + q1;
    double n1 = st_fmad(st_fmad(t, 3.002054, 0.3558829), b, st_fmad(b2, -0.3559217, t2 + 8.770166e-05) - q3);
    double n2 = st_fmad(st_fmad(t, -3.004097, -0.2958068), b, st_fmad(b2, 0.2957434, t1 + 0.0001370405) + q3);
    double n3 = st_fmad(st_fmad(t, 1.002043, 0.08020683), b, st_fmad(b2, -0.08016807, -8.770166e-05) - (t2 + q1));
    o->b[0] = (float)(n0 * inv);
    o->b[1] = (float)(n1 * inv);
    o->b[2] = (float)(n2 * inv);
    o->b[3] = (float)(n3 * inv);
    o->a[0] = 1.0f;
    o->a[1] = (float)(st_fmad(b2, -3.568348, st_fmad(b, 3.599663, 0.0008659945)) * inv);
    o->a[2] = (float)(st_fmad(b2, 3.029995, st_fmad(b, -3.05867, 0.001418441)) * inv);
    o->a[3] = (float)(st_fmad(b2, -0.8321937, st_fmad(b, 0.8405258, -0.0008685599)) * inv);
}

/* the tone cut section, bilinear from w = tan(arg): (w / (w + 1)) (x + x1) - ((w - 1) / (w + 1)) y1 */
static void tonecut_set(struct fos *c, float arg)
{
    float w = st_tanf(arg);
    c->b0 = w / (w + 1.0f);
    c->b1 = c->b0;
    c->a1 = (w + -1.0f) / (w + 1.0f);
}

/*
 * The stage over a block in place (amp.h, STAGE_BLOCK_POINTS's layout): the points of 0.01 y, the current one clipped
 * from y itself (k 0.3), the 3 older ones with k 30, each by the clipper 40 - 40 e^(-k |x|) (odd; clip_env1 = clip_env
 * with a = 40, its side by the sign bit: y and 0.01 y agree, and where 0.01 y rounds to 0 both sides give 0); no point
 * is the last one's again. Then the FIR four samples at a time.
 */
static void stage_block(struct stage *s, float *y, int n)
{
    float buf[STAGE_BLOCK_PTS];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev;
    for (int i = 0; i < n; i++) {
        float x = y[i], c = x * 0.01f, d = c - prev, *o = buf + 40 + 4 * i;
        o[0] = clip_env1(prev, 30.0f, 40.0f);
        o[1] = clip_env1(st_fma(d, 0.33333334f, prev), 30.0f, 40.0f);
        o[2] = clip_env1(st_fma(d, 0.6666667f, prev), 30.0f, 40.0f);
        o[3] = clip_env1(x, 0.29999998211860657f, 40.0f);
        prev = c;
    }
    s->prev = prev;
    stage_block_fir(s, buf, y, n);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    float raw = st_taper(st_pow2, k[K_GAIN]);
    *s = (struct state){ 0 };
    s->gain = raw * 0.99f;
    s->tonecut = k[K_TONECUT] * 0.01f;
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = k[K_BASS] * 0.009f;
    s->treble = st_pow2_100[(int)k[K_TREBLE]];
    tone_set(&s->tone, s->bass, s->treble);
    gain2_set(&s->gain_c, s->gain, raw, GAIN_INIT);
    tonecut_set(&s->tc_c, st_fma(k[K_TONECUT], -0.013453283347189426f, 1.4247585535049438f));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float bass = io->knobs[K_BASS] * 0.009f, treble = st_pow2[(int)io->knobs[K_TREBLE]];
    if (s->bass != bass || s->treble != treble) {
        s->bass = st_glide_by(s->bass, bass, 0.004f);
        s->treble = st_glide_by(s->treble, treble, 0.004f);
        tone_set(&s->tone, s->bass, s->treble);
    }
    float tonecut = io->knobs[K_TONECUT] * 0.01f;
    if (s->tonecut != tonecut) {
        s->tonecut = st_glide_by(s->tonecut, tonecut, 0.004f);
        tonecut_set(&s->tc_c, st_fma(s->tonecut, -1.3453283309936523f, 1.4247585535049438f));
    }
    if (!io->enabled)
        return;
    float gain = st_taper(st_pow2, io->knobs[K_GAIN]) * 0.99f, volume = io->knobs[K_VOLUME] * 0.01f;
    /* stage by stage over the block (the glides once per sample, the gain section following Gain): the same
       samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], vol[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PRE[0], &s->pre[0], x, n);
    sos_block(&PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        s->volume = st_glide(s->volume, volume);
        vol[i] = s->volume;
        if (s->gain != gain)
            gain2_set(&s->gain_c, s->gain, s->gain, GAIN_PROCESS);
        x[i] = sos_run_div(&s->gain_c, &s->gs, x[i]);
    }
    sos_block(&PRE[2], &s->pre[2], x, n);
    for (int i = 0; i < n; i++)
        x[i] = tone3_run_std(&s->tone, x[i]);
    stage_block(&s->st, x, n);
    for (int i = 0; i < n; i++)
        x[i] = fos_run_b1(&s->tc_c, &s->tc, x[i]);
    for (int j = 0; j < 6; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = vol[i] * 0.06144000217318535f * x[i];
}

const struct wvfx_desc voks_15tb_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x10),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Voks 15TB",
    .category = "Voks",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
