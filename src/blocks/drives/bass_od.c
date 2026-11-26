/*
 * Bass OD (DRIVE › Bass Drive): the original's Bass OD (QME-200 V1.2.2, library entry 3.22, id 0x0300003F) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample: a fixed 1st-order pre-filter (the clean signal), a 1st-order gain section (Gain, A25), x 1.3, a
 * six-range clipper on 4 points per sample with two curves blended by m(Gain), as in the Warm OD, with the
 * anti-aliasing FIR, a 2nd-order tone section (Tone, B25), Mode's post filters (0 none, 1 one section, 2 two
 * sections x 0.8, other values silence the wet path), Blend between 4 x the clean signal and the result, x 2.016
 * Volume (A25). Volume and Blend glide 0.005 per block (also while bypassed); Gain and Tone are read as they are,
 * only while the block is on.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Mode", 0.0f, 2.0f, 1.0f, 0.0f, 3, 0x15, 0, 0, 0, 0 },
    { "Blend", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0, 0, 0, 0 },
};

static const struct fos PF1 = { 0.9998865723609924f, -0.9998865723609924f, -0.9997733235359192f };
static const struct sos POST1 = {
    0.9750000238418579f, -1.8956999778747559f, 0.9301000237464905f, 1.0f, -1.8956999778747559f, 0.9052000045776367f,
};
static const struct sos POST2 = {
    0.9976000189781189f, -1.982300043106079f, 0.9861999750137329f, 1.0f, -1.982300043106079f, 0.9839000105857849f,
};
static const struct sos POST3 = {
    1.1476999521255493f, -1.5254000425338745f, 0.5178999900817871f, 1.0f, -1.5254000425338745f, 0.6656000018119812f,
};

struct state {
    float gain, tone, volume, blend;    /* Gain and Tone as read (tapers), Volume and Blend glided */
    struct fos pf2;
    struct sos tc;
    struct fos_state s1, s2;
    struct sos_state ts, p1, p2, p3;
    struct stage st;
};

static void gain_set(struct fos *c, float g)
{
    float t = g * 207.75430297851562f;
    *c = (struct fos){ t + 1.976444959640503f, -1.9293349981307983f - t, -0.9528902173042297f };
}

static void tone_set(struct sos *c, float t)
{
    float t2 = t * t, u = t * 0.10183299332857132f;
    *c = (struct sos){ st_fma(t2, 0.10183299332857132f, -0.004531166050583124f - u), -0.004276182036846876f,
                       st_fma(t2, -0.10183299332857132f, u + 0.00025498445029370487f),
                       st_fma(t, -0.9490835070610046f, -0.09541255235671997f) + t2,
                       st_fma(t - t2, 1.7963340282440186f, 0.03793766722083092f),
                       st_fma(t2, 0.7963340282440186f, st_fma(t, -0.8472505211830139f, 0.048922520130872726f)) };
}

/* the 0-0.4 range's linear terms: x c, (x c1 + c0) */
struct r4 { float a1, a0, b1, b0, ka, kb; };
static const struct r4 R4 = {
    -0.635200023651123f, 0.2685999870300293f, -2.3592000007629395f, 0.5737000107765198f, 0.9699000120162964f,
    0.9577000141143799f,
};
/* the original's current point: the x 1.3 folded into the constants, the terms on the gain section's output */
static const struct r4 R4_CUR = {
    -0.825760006904602f, 0.2685999870300293f, -3.066959857940674f, 0.5737000107765198f, 1.2608699798583984f,
    1.2450100183486938f,
};

/* the clipper's two curves at x (a = |x|), blended by m; w and r give the 0-0.4 range's linear terms (w = x, R4 for
   the earlier points) */
static float clip(float x, float m, float w, const struct r4 *r)
{
    float a = st_abs(x), A, B;
    if (x < -10.0f) {
        float u = a / (a + 20.0f);
        A = st_fma(st_fma(a, 1.7872500848170603e-06f, -0.0011924001155421138f), a,
                   st_fma(u, -0.46048998832702637f, -1.2265000343322754f));
        B = st_fma(st_fma(a, 1.7434000483262935e-06f, -0.0009528500377200544f), a,
                   st_fma(u, -0.442220002412796f, -0.9315900206565857f));
        A = st_fma(u * u, 0.14868000149726868f, A);
        B = st_fma(u * u, 0.13553999364376068f, B);
    } else if (x < -0.800000011920929f) {
        float u = a / st_fma(a, 3.0f, 1.0f);
        A = st_fma(st_fma(a, 0.0016497999895364046f, -0.04193800315260887f), a, 5.394700050354004f);
        B = st_fma(st_fma(a, 0.0006838099798187613f, -0.02052300050854683f), a, 1.1116000413894653f);
        A = st_fma(u, -42.19350051879883f, A);
        B = st_fma(u, -10.947699546813965f, B);
        A = st_fma(u * u, 68.2229995727539f, A);
        B = st_fma(u * u, 14.276800155639648f, B);
    } else if (x < 0.0f) {
        float x2 = x * x;
        A = st_fma(st_fma(x2, -0.0869000032544136f, 0.9825000166893005f), x, x2 * -0.07460000365972519f);
        B = st_fma(st_fma(x2, -0.5812000036239624f, 0.930400013923645f), x, x2 * -0.37209999561309814f);
    } else if (x < 0.4000000059604645f) {
        float x2 = x * x;
        A = st_fma(st_fma(w, r->a1, r->a0), x2, w * r->ka);
        B = st_fma(st_fma(w, r->b1, r->b0), x2, w * r->kb);
    } else if (x < 8.0f) {
        float u = a / (a + 0.10000000149011612f);
        A = st_fma(st_fma(a, -0.0008061000262387097f, 0.01687600091099739f), a,
                   st_fma(u, 8.019200325012207f, -3.5657999515533447f));
        B = st_fma(st_fma(a, -0.0006769300671294332f, 0.014449000358581543f), a,
                   st_fma(u, 1.7467000484466553f, -0.7101799845695496f));
        A = st_fma(u * u, -3.8538999557495117f, A);
        B = st_fma(u * u, -0.5740699768066406f, B);
    } else {
        float u = a / (a + 2.0f);
        A = st_fma(st_fma(a, -1.2957250419276534e-06f, 0.0007117500063031912f), a,
                   st_fma(u, -1.4947999715805054f, 1.1888999938964844f));
        B = st_fma(st_fma(a, -1.2990501545573352e-06f, 0.0006011500372551382f), a,
                   st_fma(u, -1.5038000345230103f, 1.044100046157837f));
        A = st_fma(u * u, 1.0643999576568604f, A);
        B = st_fma(u * u, 1.0667999982833862f, B);
    }
    return st_fma(A - B, m, B);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[0]);
    s->tone = st_taper(st_b25, k[1]);
    s->volume = st_taper(st_a25, k[2]);
    s->blend = k[4] * 0.01f;
    gain_set(&s->pf2, s->gain);
    tone_set(&s->tc, s->tone);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    int mode = (int)io->knobs[3];
    s->volume = st_glide_by(s->volume, st_taper(st_a25, io->knobs[2]), 0.005f);
    s->blend = st_glide_by(s->blend, io->knobs[4] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float g = st_taper(st_a25, io->knobs[0]), t = st_taper(st_b25, io->knobs[1]);
    float level = s->volume * 2.0160000324249268f, wet = 0.0f;
    /* stage by stage over the block (Gain and Tone, not glided, before the first sample: m is the block's): the
       same samples as sample by sample */
    if (s->gain != g) {
        s->gain = g;
        gain_set(&s->pf2, g);
    }
    if (s->tone != t) {
        s->tone = t;
        tone_set(&s->tc, t);
    }
    float r = 1.0f / st_fma(s->gain, 10.0f, 1.0f);
    float m = st_fma(st_fma(r, -0.002068999921903014f, 0.7153800129890442f), r,
                     st_fma(st_fma(s->gain, 0.15086999535560608f, -0.501259982585907f), s->gain,
                            0.28668999671936035f));
    float *p = io->io, c[WVFX_MAX_FRAMES], y[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        c[i] = fos_run(&PF1, &s->s1, p[2 * i]);
        y[i] = fos_run(&s->pf2, &s->s2, c[i]);
    }
    /* the points of 1.3 g2: the 3 older ones through the clipper with w = the point (R4), the current one with
       w = g2 (R4_CUR; no point is the last one's again); then the FIR four samples at a time */
    const float *rr = &s->st.pts[s->st.at];
    for (int j = 0; j < 40; j++)
        buf[39 - j] = rr[j];
    float prev = s->st.prev;
    for (int i = 0; i < n; i++) {
        float x = y[i] * 1.2999999523162842f, d = x - prev, *o = buf + 40 + 4 * i;
        float q1 = st_fma(d, 0.6666667f, prev), q2 = st_fma(d, 0.33333334f, prev);
        o[0] = clip(prev, m, prev, &R4);
        o[1] = clip(q2, m, q2, &R4);
        o[2] = clip(q1, m, q1, &R4);
        o[3] = clip(x, m, y[i], &R4_CUR);
        prev = x;
    }
    s->st.prev = prev;
    stage_block_fir(&s->st, buf, y, n);
    for (int i = 0; i < n; i++)
        y[i] = sos_run_div_b1(&s->tc, &s->ts, y[i]);
    if (mode == 2) {
        sos_block(&POST2, &s->p2, y, n);
        sos_block(&POST3, &s->p3, y, n);
        for (int i = 0; i < n; i++)
            y[i] = y[i] * 0.800000011920929f;
    } else if (mode == 1) {
        sos_block(&POST1, &s->p1, y, n);
    } else if (mode != 0) {
        for (int i = 0; i < n; i++)
            y[i] = wet;
    }
    for (int i = 0; i < n; i++) {
        float c4 = c[i] * 4.0f;
        p[2 * i] = p[2 * i + 1] = level * st_fma(y[i] - c4, s->blend, c4);
    }
}

const struct wvfx_desc bass_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x116),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bass OD",
    .category = "Bass Drive",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
