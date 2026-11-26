/*
 * BB-Boost (DYN): the original's BB-Boost (QME-200 V1.2.2, library entry 0.6, id 0x0000000B) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A drive-style boost, mono (the left input; both outputs the same).
 * Single precision.
 *
 * x through a 2nd-order section, then a gain section set by Gain (g = pow2(Gain), divided by its a0); the amps'
 * 4-points stage (amp.h) with an exponential clipper L (1 - e^(-2 |v|)) (odd; L = 0.5 - 0.2 g) and the 40-tap FIR
 * summed onto 0.4 x the first section's output; two 2nd-order sections, a 4th-order tone section by Bass and Treble
 * (polynomials in 1 - Bass and 1 - Treble, the original's fused order; divided by its a0), a 2nd-order low pass
 * (divided by its a0), x 0.4 Volume. Volume glides 0.005 per block (bypassed too); Treble 0.0001 per sample while
 * Bass or Treble differs from the knob (Bass jumps), the tone section recomputed then. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PF = {
    0.9754291772842407f, -1.7918051481246948f, 0.8163824081420898f, 1.0f, -1.8327220678329468f, 0.8336324095726013f,
};
static const struct sos POST1 = {
    0.050625190138816833f, 0.004103711806237698f, -0.04652148112654686f, 1.0f, -1.740054965019226f,
    0.7490835785865784f,
};
static const float POST2_B0 = -2.8582019805908203f, POST2_B2 = 2.8582019805908203f;
static const float POST2_A1 = -0.7475324273109436f, POST2_A2 = -0.22488810122013092f;
static const struct sos LOWPASS = {
    0.46263501048088074f, 0.9252700209617615f, 0.9754291772842407f, 1.7052360773086548f, -0.14946000277996063f,
    0.2947639226913452f,
};

struct state {
    float drive, volume, bass, treble;
    struct sos gain;                    /* divided by a0 */
    float n[5], d[5];                   /* the tone section, d[0] divides */
    struct sos_state pf, gs, p1, lp;
    float p2x1, p2x2, p2y1, p2y2;
    float ex[5], ey[5];
    struct stage st;
};

static void gain_set(struct state *s, float g)
{
    float n1 = st_fma(g, -1.902359962463379f, 0.002682891907170415f);
    s->gain = (struct sos){ st_fma(g, 45.99103927612305f, 0.6822215914726257f), n1,
                            st_fma(g, -44.088680267333984f, -0.6414914727210999f), g + 0.23231109976768494f, n1,
                            st_fma(g, 0.9023599028587341f, -0.1915811002254486f) };
}

/* the tone section from Bass and Treble (x 0.01) */
static void eq_set(struct state *s)
{
    float u = 1.0f - s->bass, v = 1.0f - s->treble, u2 = u * u, v2 = v * v, *n = s->n, *d = s->d;
    n[0] = st_fma(st_fma(u2, -0.23922720551490784f, st_fma(u, 0.23949329555034637f, 0.0014517470262944698f)), v,
        st_fma(st_fma(u2, -0.5684093236923218f, st_fma(u, 0.5681431889533997f, 0.003930797800421715f)), v2,
        st_fma(u, -0.9999281764030457f, (u2 + -0.006703047081828117f))));
    n[1] = st_fma(st_fma(v2, 2.1961960792541504f, st_fma(v, 0.9165046811103821f, -3.695560932159424f)), u2,
        st_fma(st_fma(v, -0.9165046811103821f, 3.69559907913208f), u,
        (0.011667280457913876f - ((((v2 * u) * 2.1961960792541504f) + (v * 0.0030136900022625923f))
        + (v2 * 0.007279445882886648f)))));
    n[2] = st_fma((-0.0005821504746563733f - st_fma(u, -3.1786649227142334f, (u2 * 3.178133010864258f))), v2,
        st_fma(st_fma(u2, -1.315214991569519f, st_fma(u, 1.314682960510254f, 0.00011019570229109377f)), v,
        st_fma(u2, 5.1156439781188965f, st_fma(u, -5.115746974945068f, 0.0015726130222901702f))));
    n[3] = st_fma(st_fma(v, -0.837824821472168f, 3.1437480449676514f), u, st_fma(st_fma(v2, 2.0413150787353516f,
        st_fma(v, 0.837824821472168f, -3.143781900405884f)), u2,
        ((((v2 * 0.007279445882886648f) + (v * 0.0030136900022625923f)) + -0.011672699823975563f)
        - ((v2 * u) * 2.0413150787353516f))));
    n[4] = st_fma(-st_fma(v, 0.19988730549812317f, st_fma(v2, 0.49096840620040894f, -0.723698616027832f)), u2,
        -st_fma(st_fma(v, -0.20015330612659454f, st_fma(v2, -0.4907023012638092f, 0.723662793636322f)), u,
        st_fma(v2, 0.0033486480824649334f, st_fma(v, 0.0015619429759681225f, -0.005125014111399651f))));
    d[0] = st_fma(st_fma(u2, -1.3760459423065186f, st_fma(u, 1.376312017440796f, 0.009047278203070164f)), v,
        st_fma(st_fma(u, -0.5686753988265991f, st_fma(u2, 0.5684093236923218f, -0.003664732910692692f)), v2,
        st_fma(u2, -0.19236339628696442f, st_fma(u, 0.19243529438972473f, 0.0012486979831010103f))));
    d[1] = st_fma(st_fma(v2, -2.1961960792541504f, st_fma(v, 5.308897018432617f, 0.5828596949577332f)), u2,
        st_fma(st_fma(v, -5.308897018432617f, -0.5828213095664978f), u,
        ((((v2 * 0.007279445882886648f) + ((v2 * u) * 2.1961960792541504f)) + -0.0014125630259513855f)
        - (v * 0.01757257990539074f))));
    d[2] = st_fma(st_fma(u2, -7.671481132507324f, -0.0005219749873504043f), v, st_fma(st_fma(v2,
        -3.1776010990142822f, st_fma(v, 7.6709489822387695f, 0.6221929788589478f)), u,
        st_fma(((u2 * 3.178133010864258f) + 5.0020240450976416e-05f), v2, st_fma(u2, -0.6222956776618958f,
        -0.0009979483438655734f))));
    d[3] = st_fma(st_fma(v2, -2.0413150787353516f, st_fma(v, 4.920454025268555f, 0.26464250683784485f)), u2,
        st_fma(st_fma(v, -4.920454025268555f, -0.26467588543891907f), u,
        (((((v2 * u) * 2.0413150787353516f) + 0.0014130290364846587f) + (v * 0.01757257990539074f))
        - (v2 * 0.007279445882886648f))));
    d[4] = st_fma(st_fma(u2, -1.181823968887329f, -0.00852530263364315f), v, st_fma(st_fma(v2,
        -0.49123451113700867f, st_fma(v, 1.1820900440216064f, 0.03287883847951889f)), u, st_fma(st_fma(u2,
        0.49096840620040894f, 0.0036147129721939564f), v2, st_fma(u2, -0.032842978835105896f,
        -0.000250284094363451f))));
}

/* l - l e^(-2 |v|) (the original's e^(-+2 v), amp.h exp_negabs), l the side's (lp, ln: the block's Drive), by v's
   sign bit (at +0 e = 1 and both sides give 0) */
static inline float clip(float v, float lp, float ln)
{
    float l = pick_sign(v, lp, ln);
    return st_fma(-exp_negabs(v, 2.0f), l, l);                                       /* VFMS */
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->volume = k[1] * 0.01f;
    s->drive = st_taper(st_pow2, k[0]);
    s->bass = k[2] * 0.01f;
    s->treble = k[3] * 0.01f;
    gain_set(s, s->drive);
    eq_set(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float drive = st_taper(st_pow2, k[0]), vol = k[1] * 0.01f, bass = k[2] * 0.01f, treble = k[3] * 0.01f;
    float *p = io->io;
    s->volume = st_glide_by(s->volume, vol, 0.004999999888241291f);           /* once per block */
    const int nf = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || nf <= 0)
        return;
    /* stage by stage over the block (Drive, not glided, before the first sample; Treble's glide once per sample,
       the EQ following it after the stage): the same samples as sample by sample */
    if (s->drive != drive) {
        s->drive = drive;
        gain_set(s, drive);
    }
    float yv[WVFX_MAX_FRAMES], gv[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    for (int i = 0; i < nf; i++)
        yv[i] = p[2 * i];
    sos_block(&PF, &s->pf, yv, nf);
    for (int i = 0; i < nf; i++)
        gv[i] = yv[i];
    sos_block_div(&s->gain, &s->gs, gv, nf);
    float lp = st_fma(s->drive, -0.20000000298023224f, 0.5f), ln = st_fma(-s->drive, -0.20000000298023224f, -0.5f);
    STAGE_BLOCK_POINTS(&s->st, gv, nf, buf, clip(v, lp, ln));
    for (int i = 0; i < nf; i++)
        yv[i] = yv[i] * 0.4000000059604645f;
    stage_block_fir_acc(&s->st, buf, gv, nf, AMP_FIR_ORDER2, yv);
    sos_block(&POST1, &s->p1, gv, nf);
    for (int i = 0; i < nf; i++, p += 2) {
        if (s->bass != bass || s->treble != treble) {
            s->treble = st_glide(s->treble, treble);
            s->bass = bass;
            eq_set(s);
        }
        float u = gv[i];
        float w = st_fma(POST2_B0, u, POST2_B2 * s->p2x2) - st_fma(s->p2y2, POST2_A2, s->p2y1 * POST2_A1);
        s->p2x2 = s->p2x1;
        s->p2x1 = u;
        s->p2y2 = s->p2y1;
        s->p2y1 = w;
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[4], ex[4], st_fma(n[3], ex[3], st_fma(n[2], ex[2], st_fma(n[1], ex[1], n[0] * w))));
        float dd = st_fma(d[4], ey[4], st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1])));
        float e = (nn - dd) / d[0];
        for (int j = 4; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = w;
        ey[1] = ey[0] = e;
        p[0] = p[1] = (sos_run_div(&LOWPASS, &s->lp, e) * 0.4000000059604645f) * s->volume;
    }
}

const struct wvfx_desc dyn_bb_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x106),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "BB-Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
