/*
 * Fuzz Cream (DRIVE › Fuzz): the original's Fuzz Cream (QME-200 V1.2.2, library entry 3.12, id 0x03000022) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample: a fixed 2nd-order input section (no x1 term), a 1st-order high pass whose gain comes from a table by
 * Sustain, a fixed 2nd-order section, the Warm OD's two-curve clipper on 4 points per sample with the anti-aliasing
 * FIR, the same fixed section again, the clipper and FIR again, a 2nd-order tone section from a table by Tone, x 2.34
 * Volume. The clipper's blend m comes from Sustain. Both tables are indexed by (int)knob / 4, so only their first
 * 26 steps are reachable. Volume starts at 0 and glides 0.005 per process call, also while bypassed.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Sustain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
};

/* the Sustain section's gain per Sustain step (the reachable 26 of the original's 64) */
static const float SUS[26] = {
    0.017680000513792038f, 0.017805000767111778f, 0.018191000446677208f, 0.018849000334739685f, 0.01979300007224083f,
    0.021037999540567398f, 0.022599000483751297f, 0.024491000920534134f, 0.026731999590992928f, 0.029339000582695007f,
    0.032329000532627106f, 0.035721998661756516f, 0.03953799977898598f, 0.04379599913954735f, 0.04851900041103363f,
    0.05372700095176697f, 0.05944399908185005f, 0.0656920000910759f, 0.07249700278043747f, 0.07988200336694717f,
    0.08787400275468826f, 0.096499003469944f, 0.10578399896621704f, 0.11575599759817123f, 0.12644599378108978f,
    0.13787999749183655f,
};
/* the tone section's (b0, b1, b2) per Tone step (the reachable 26 of 64) */
static const float TONE[78] = {
    0.0657379999756813f, -0.0028520000632852316f, -0.03617300093173981f, 0.07838699966669083f, -0.02849699929356575f,
    -0.023523999378085136f, 0.09103599935770035f, -0.05414099991321564f, -0.010874999687075615f, 0.10368400067090988f,
    -0.07978600263595581f, 0.0017740000039339066f, 0.1163330003619194f, -0.1054299995303154f, 0.014422000385820866f,
    0.12898199260234833f, -0.13107499480247498f, 0.027070999145507812f, 0.14163100719451904f, -0.15671899914741516f,
    0.039719998836517334f, 0.15428000688552856f, -0.18236400187015533f, 0.052368998527526855f, 0.1669279932975769f,
    -0.20800800621509552f, 0.06501699984073639f, 0.17957699298858643f, -0.2336529940366745f, 0.07766599953174591f,
    0.19222599267959595f, -0.2592970132827759f, 0.09031499922275543f, 0.20487500727176666f, -0.28494200110435486f,
    0.10296399891376495f, 0.21752400696277618f, -0.31058600544929504f, 0.11561299860477448f, 0.23017199337482452f,
    -0.336230993270874f, 0.128260999917984f, 0.24282099306583405f, -0.3618749976158142f, 0.14090999960899353f,
    0.25547000765800476f, -0.3875199854373932f, 0.15355899930000305f, 0.2681190073490143f, -0.4131639897823334f,
    0.16620799899101257f, 0.2807669937610626f, -0.43880900740623474f, 0.1788569986820221f, 0.29341599345207214f,
    -0.4644530117511749f, 0.19150499999523163f, 0.30606499314308167f, -0.4900979995727539f, 0.20415399968624115f,
    0.3187139928340912f, -0.5157420039176941f, 0.21680299937725067f, 0.3313629925251007f, -0.5413870215415955f,
    0.2294519990682602f, 0.34401100873947144f, -0.5670310258865356f, 0.24210099875926971f, 0.35666000843048096f,
    -0.5926759839057922f, 0.25474900007247925f, 0.3693090081214905f, -0.6183199882507324f, 0.26739799976348877f,
    0.3819580078125f, -0.6439650058746338f, 0.2800469994544983f,
};

/* the input section and the section around the clippers (b1 = 0, a0 = 1) */
static const struct sos PF = { -0.5436999797821045f, 0.0f, 0.5436999797821045f, 1.0f, -1.8668999671936035f,
                               0.8672000169754028f };
static const struct sos NF = { -1.867799997329712f, 0.0f, 1.867799997329712f, 1.0f, -1.7146999835968018f,
                               0.714900016784668f };
#define SUS_A1 -0.9997772574424744f
#define TONE_A1 -1.6049890518188477f
#define TONE_A2 0.6367790102958679f

struct state {
    float volume;                       /* glided, 0-1 */
    float sx, sy;                       /* the Sustain high pass's x1, y1 */
    struct sos_state pf, nf1, nf2, ts;
    struct stage st1, st2;
};

/* the no-x1 sections: from b0 x (the input one and the first around the clippers) ... */
static inline float nox1_run(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b0, x, c->b2 * s->x2);
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, n - d);
}

/* ... and from b2 x2 (the second) */
static inline float nox1_run_b2(const struct sos *c, struct sos_state *s, float x)
{
    float n = st_fma(c->b2, s->x2, c->b0 * x);
    float d = st_fma(c->a2, s->y2, c->a1 * s->y1);
    return sos_out(s, x, n - d);
}

/* a stage with clip_warm over a block in place (m is the block's: the previous point is the last one's again) */
static inline void stage_warm_block(struct stage *st, float *y, int n, float m)
{
    float buf[STAGE_BLOCK_PTS];
    STAGE_BLOCK_POINTS(st, y, n, buf, clip_warm(v, m));
    stage_block_fir(st, buf, y, n);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env; (void)k;
    *(struct state *)state = (struct state){ 0 };
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    s->volume = st_glide_by(s->volume, k[2] * 0.01f, 0.005f);
    if (!io->enabled || io->frames == 0)
        return;
    float g = SUS[(int)k[0] >> 2], t = (float)(int)k[0];
    int j = (int)k[1] >> 2;
    const struct sos tc = { TONE[3 * j], TONE[3 * j + 1], TONE[3 * j + 2], 1.0f, TONE_A1, TONE_A2 };
    float r = 1.0f / st_fma(t, 0.09999999403953552f, 1.0f);
    float m = st_fma(st_fma(r, 0.5638300180435181f, 0.09240099787712097f), r, 0.34376999735832214f);
    m = st_fma(st_fma(t, 1.8619199181557633e-05f, -0.005430200137197971f), t, m);
    float level = s->volume * 2.3399999141693115f;
    /* stage by stage over the block (every coefficient is the block's): the same samples as sample by sample */
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++) {
        float v = nox1_run(&PF, &s->pf, p[2 * i]);
        float y = st_fma(v - s->sx, g, -(SUS_A1 * s->sy));
        s->sx = v;
        s->sy = y;
        x[i] = nox1_run(&NF, &s->nf1, y);
    }
    stage_warm_block(&s->st1, x, n, m);
    for (int i = 0; i < n; i++)
        x[i] = nox1_run_b2(&NF, &s->nf2, x[i]);
    stage_warm_block(&s->st2, x, n, m);
    sos_block(&tc, &s->ts, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = level * x[i];
}

const struct wvfx_desc fuzz_cream_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x10C),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Fuzz Cream",
    .category = "Fuzz",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
