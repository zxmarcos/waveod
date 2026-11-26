/*
 * Dist Plus (DRIVE › Distortion): the original's Dist Plus (QME-200 V1.2.2, library entry 3.18, id 0x03000029) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample: a 1st-order gain section whose coefficients come from a 64-step table (Gain), a fixed high pass, a hard
 * clip at +-2 on 4 points per sample with the anti-aliasing FIR, a fixed band pass, the high pass again, a piecewise
 * polynomial clipper on 4 points per sample (odd, flat at 0.78275 beyond 3) with a second FIR, four fixed 2nd-order
 * sections, x 1.5 Volume.
 *
 * Quirks kept: the original's Init ignores the knobs, so Volume starts at 0 and glides 0.0001 per sample, only while
 * the block is on. The second FIR keeps its points halved and weighs the 4 new ones unhalved with halved taps, each on
 * its own; the 4 oldest go in on their own too, the rest in pairs.
 */
#include "../amps/amp.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
};

/* the gain section per Gain step: a1, then (b0, b1) */
static const float PF1_A[64] = {
    -0.996694028377533f, -0.996694028377533f, -0.9965789914131165f, -0.9964619874954224f, -0.9963449835777283f,
    -0.9962270259857178f, -0.9961090087890625f, -0.995989978313446f, -0.9958699941635132f, -0.9957489967346191f,
    -0.9956269860267639f, -0.9955040216445923f, -0.9953789710998535f, -0.9952530264854431f, -0.9951249957084656f,
    -0.9949949979782104f, -0.994862973690033f, -0.9947280287742615f, -0.9945889711380005f, -0.9944480061531067f,
    -0.994301974773407f, -0.9941520094871521f, -0.9939979910850525f, -0.9938380122184753f, -0.9936720132827759f,
    -0.9934989809989929f, -0.9933189749717712f, -0.9931309819221497f, -0.9929350018501282f, -0.9927279949188232f,
    -0.992510974407196f, -0.992281973361969f, -0.9920399785041809f, -0.9917839765548706f, -0.9915120005607605f,
    -0.9912220239639282f, -0.9909129738807678f, -0.9905819892883301f, -0.9902259707450867f, -0.989844024181366f,
    -0.9894300103187561f, -0.9889820218086243f, -0.9884949922561646f, -0.9879639744758606f, -0.9873809814453125f,
    -0.9867389798164368f, -0.9860280156135559f, -0.9852370023727417f, -0.9843509793281555f, -0.9833530187606812f,
    -0.9822180271148682f, -0.9809169769287109f, -0.9794110059738159f, -0.9776470065116882f, -0.9755529761314392f,
    -0.9730250239372253f, -0.9699140191078186f, -0.9659929871559143f, -0.9608960151672363f, -0.9540020227432251f,
    -0.9441570043563843f, -0.9289489984512329f, -0.9023600220680237f, -0.9023600220680237f,
};
static const float PF1_B[128] = {
    7.8520731925964355f, -7.848766803741455f, 7.8520731925964355f, -7.848766803741455f, 8.091651916503906f,
    -8.088231086730957f, 8.332794189453125f, -8.329256057739258f, 8.575409889221191f, -8.571755409240723f,
    8.819470405578613f, -8.815696716308594f, 9.064994812011719f, -9.061102867126465f, 9.312065124511719f,
    -9.30805492401123f, 9.560821533203125f, -9.55669116973877f, 9.811463356018066f, -9.807211875915527f,
    10.064248085021973f, -10.059874534606934f, 10.319496154785156f, -10.3149995803833f, 10.57758617401123f,
    -10.572964668273926f, 10.838955879211426f, -10.834209442138672f, 11.104104042053223f, -11.099228858947754f,
    11.373587608337402f, -11.368582725524902f, 11.64802360534668f, -11.6428861618042f, 11.92809009552002f,
    -11.92281723022461f, 12.214526176452637f, -12.209115982055664f, 12.508137702941895f, -12.502586364746094f,
    12.809799194335938f, -12.80410099029541f, 13.120452880859375f, -13.114605903625488f, 13.441123962402344f,
    -13.435121536254883f, 13.772917747497559f, -13.766756057739258f, 14.117034912109375f, -14.11070728302002f,
    14.474777221679688f, -14.468276023864746f, 14.847559928894043f, -14.840879440307617f, 15.236929893493652f,
    -15.230060577392578f, 15.644577980041504f, -15.637513160705566f, 16.072362899780273f, -16.06509017944336f,
    16.52233123779297f, -16.514841079711914f, 16.99675178527832f, -16.989032745361328f, 17.49814224243164f,
    -17.490182876586914f, 18.029321670532227f, -18.021106719970703f, 18.59344482421875f, -18.5849552154541f,
    19.19407081604004f, -19.185293197631836f, 19.835233688354492f, -19.82614517211914f, 20.52152442932129f,
    -20.51210594177246f, 21.25820541381836f, -21.248432159423828f, 22.051332473754883f, -22.041175842285156f,
    22.907926559448242f, -22.897356033325195f, 23.836170196533203f, -23.825153350830078f, 24.84567642211914f,
    -24.834171295166016f, 25.94780921936035f, -25.935773849487305f, 27.15612030029297f, -27.14349937438965f,
    28.48689079284668f, -28.473630905151367f, 29.959884643554688f, -29.945911407470703f, 31.599315643310547f,
    -31.584552764892578f, 33.43519973754883f, -33.419551849365234f, 35.50519943237305f, -35.48855209350586f,
    37.85723114013672f, -37.83945083618164f, 40.55322265625f, -40.534141540527344f, 43.67463302612305f,
    -43.65404510498047f, 47.33083724975586f, -47.3084831237793f, 51.672183990478516f, -51.64773941040039f,
    56.91120910644531f, -56.88423538208008f, 63.358463287353516f, -63.32837677001953f, 71.48640441894531f,
    -71.452392578125f, 82.05072021484375f, -82.0116195678711f, 96.34004211425781f, -96.2940444946289f,
    116.74620819091797f, -116.69036102294922f, 148.2664794921875f, -148.1954345703125f, 203.3787078857422f,
    -203.28106689453125f, 203.3787078857422f, -203.28106689453125f,
};

/* the 2nd-order sections the original writes out (y = their sum in the order of the terms below) */
struct hp_state { float x1, x2, y1, y2; };

/* the high pass: (x - 2 x1) + x2, then + y2 a2, + y1 a1 (fused) */
static inline float hp_run(struct hp_state *s, float x)
{
    float y = (x - (s->x1 + s->x1)) + s->x2;
    y = st_fma(s->y2, -0.9919727444648743f, y);
    y = st_fma(s->y1, 1.9919403791427612f, y);
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

/* the band pass: y1 a1, + y2 a2, + (x - x2) b (fused) */
static inline float bp_run(struct hp_state *s, float x)
{
    float y = s->y1 * 0.8417716026306152f;
    y = st_fma(s->y2, 0.15776781737804413f, y);
    y = st_fma(x - s->x2, 0.47732532024383545f, y);
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

/* an output section, its terms summed in `order` (0 x, 1 x1, 2 x2, 3 y2, 4 y1; c in the same numbering) */
static inline float out_run(struct hp_state *s, float x, const float *c, const uint8_t *order)
{
    float v[5] = { x, s->x1, s->x2, s->y2, s->y1 };
    float y = v[order[0]] * c[order[0]];
    for (int i = 1; i < 5; i++)
        y = st_fma(v[order[i]], c[order[i]], y);
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

static const float OUT_C[4][5] = {
    { 0.8153809905052185f, -1.5839999914169312f, 0.7692999839782715f, -0.9641919732093811f, 1.963386058807373f },
    { 0.8056480288505554f, -1.5839999914169312f, 0.7879109978675842f, -0.9706839919090271f, 1.9586379528045654f },
    { 0.9153050184249878f, -1.5839999914169312f, 0.7394000291824341f, -0.7596189975738525f, 1.6813199520111084f },
    { 1.5839999914169312f, 0.03805999830365181f, 0.030579999089241028f, -0.0003800000122282654f, 0.004207999911159277f },
};
static const uint8_t OUT_ORDER_A[5] = { 1, 2, 0, 3, 4 };
static const uint8_t OUT_ORDER[5] = { 0, 1, 2, 3, 4 };

/* x held at +-2 (VMAXNM, VMINNM: no flags; the same as the compares for every x but NaN) */
static inline float clip2(float x)
{
    return __builtin_fminf(__builtin_fmaxf(x, -2.0f), 2.0f);
}

/* the second clipper, odd: 0.8163 a, then quadratic and linear pieces in a = |x|, flat beyond 3 */
static inline float clip_poly(float x)
{
    float a = st_abs(x), sg = x < 0.0f ? -1.0f : 1.0f;
    if (a <= 0.5785999894142151f)
        return a * 0.8162999749183655f * sg;
    if (a <= 1.5149999856948853f) {
        float x2 = x * x;
        float u = st_fma(x2, 0.3671000003814697f, 2.0524001121520996f);
        return st_fma(u, a, st_fma(x2, -1.4693000316619873f, -0.29440000653266907f)) * sg;
    }
    if (a <= 3.0f)
        return st_fma(x * x, -0.01640000008046627f, st_fma(a, 0.1168999969959259f, 0.5796499848365784f)) * sg;
    return sg * 0.7827500104904175f;
}

struct state {
    float volume;               /* glided, from 0 */
    struct fos_state pf1;
    struct hp_state hp1, bp, hp2, out[4];
    struct stage st1, st2;      /* st2's points halved */
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env; (void)k;
    *s = (struct state){ 0 };
    return 0;
}

/*
 * The second stage over a block in place: the clipper on 4 points per sample, kept halved in the ring; its FIR as the
 * original sums it: the 4 new points unhalved (oldest first, their own taps), the 4 oldest of the window (halved, taps
 * 0..3), then the pairs 19 down to 4. Every point first (the previous point is the last one's again: the clipper is
 * fixed), then the FIR four samples at a time.
 */
static void poly_stage_block(struct stage *s, float *y, int n)
{
    static const float NEW[4] = { 0.000750000006519258f, 4.999999873689376e-05f, -0.0003499999875202775f,
                                  -0.0005499999970197678f };
    float buf[STAGE_BLOCK_PTS], full[4 * WVFX_MAX_FRAMES];
    const float *r = &s->pts[s->at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = r[j];
    float prev = s->prev, c = clip_poly(prev);
    for (int i = 0; i < n; i++) {
        float x = y[i], d = x - prev, *f = full + 4 * i, *o = buf + 40 + 4 * i;
        f[0] = c;                                       /* in time order: prev, 1/3, 2/3, x */
        f[1] = clip_poly(st_fma(d, 0.33333334f, prev));
        f[2] = clip_poly(st_fma(d, 0.6666667f, prev));
        f[3] = c = clip_poly(x);
        for (int j = 0; j < 4; j++)
            o[j] = f[j] * 0.5f;
        prev = x;
    }
    s->prev = prev;
    enum { W = 4 };
    int i = 0;
    for (; i <= n; i += W) {
        int w_n = n - i < W ? n - i : W;
        if (w_n <= 0)
            break;
        float a[W];
        for (int w = 0; w < W; w++) {
            const float *f = full + 4 * (i + (w < w_n ? w : 0));
            a[w] = f[0] * NEW[0];
            a[w] = st_fma(f[1], NEW[1], a[w]);
            a[w] = st_fma(f[2], NEW[2], a[w]);
            a[w] = st_fma(f[3], NEW[3], a[w]);
        }
#pragma clang loop unroll(full)
        for (int k = 0; k < 4; k++)
            for (int w = 0; w < W; w++) {
                const float *e = buf + 43 + 4 * (i + (w < w_n ? w : 0));
                a[w] = st_fma(e[k - 39], AMP_FIR[k], a[w]);
            }
#pragma clang loop unroll(full)
        for (int k = 19; k >= 4; k--)
            for (int w = 0; w < W; w++) {
                const float *e = buf + 43 + 4 * (i + (w < w_n ? w : 0));
                a[w] = st_fma(e[-k] + e[k - 39], AMP_FIR[k], a[w]);
            }
        for (int w = 0; w < w_n; w++)
            y[i + w] = a[w];
    }
    const float *last = buf + 4 * n + 39;
    for (int j = 0; j < 40; j++)
        s->pts[j] = s->pts[j + 40] = last[-j];
    s->at = 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    if (!io->enabled || io->frames == 0)
        return;
    int j = (int)(io->knobs[0] * 2.55f) >> 2;
    j = j < 0 ? 0 : j > 63 ? 63 : j;
    const struct fos pf1 = { PF1_B[2 * j], PF1_B[2 * j + 1], PF1_A[j] };
    float target = io->knobs[1] * 0.01f;
    /* stage by stage over the block (Volume's glide once per sample): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = hp_run(&s->hp1, fos_run(&pf1, &s->pf1, p[2 * i]));
    STAGE_BLOCK_POINTS(&s->st1, x, n, buf, clip2(v));
    stage_block_fir(&s->st1, buf, x, n);
    for (int i = 0; i < n; i++)
        x[i] = hp_run(&s->hp2, bp_run(&s->bp, x[i]));
    poly_stage_block(&s->st2, x, n);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, target);
        float v = out_run(&s->out[0], x[i], OUT_C[0], OUT_ORDER_A);
        for (int k = 1; k < 4; k++)
            v = out_run(&s->out[k], v, OUT_C[k], OUT_ORDER);
        p[2 * i] = p[2 * i + 1] = (s->volume * 1.5f) * v;
    }
}

const struct wvfx_desc dist_plus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x112),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Dist Plus",
    .category = "Distortion",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
