/*
 * Bog SV OD (AMP › Bog): the original's Bog SV OD amp (QME-200 V1.2.2, library entry 4.16, id 0x0700003D) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout.
 *
 * Per sample (amp.h, bog.h have the pieces):
 *   pre-filter    x 1.1, two fixed 2nd-order sections
 *   gain          a 2nd-order section from Gain (pow3_5), a fixed low pass, a fixed 2nd-order section, x 0.25
 *   stage         the envelope-driven clip 0.15 - 0.15 e^(-k |x|), k = 1.5 + 1.7 env, x max(1 / (1 + 3 k), 0.15), on
 *                 4 points per sample, the FIR
 *   tone stack    3rd order, polynomials in Bass, Treble (pow3) and Middle (linear), x Volume x 45
 *   presence      a 2nd-order section from Presence (pow2)
 *   power amp     three fixed 2nd-order sections, x 2.2652
 * Gain, Volume, Bass, Middle and Treble glide 0.0001 per sample, Presence takes effect at once; the coefficients
 * stop one glide step short of the knobs.
 */
#include "bog.h"

enum { K_GAIN, K_PRESENCE, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE };

static const struct wvfx_param params[] = {
    BOG_PARAM("Gain", 30.0f, K_GAIN), BOG_PARAM("Presence", 50.0f, K_PRESENCE), BOG_PARAM("Volume", 50.0f, K_VOLUME),
    BOG_PARAM("Bass", 50.0f, K_BASS), BOG_PARAM("Middle", 50.0f, K_MIDDLE), BOG_PARAM("Treble", 50.0f, K_TREBLE),
};

static const struct sos PRE[2] = {
    { 7.67387056350708f, -8.255764961242676f, 0.7065004110336304f, 1.0f, -1.130622148513794f, 0.1543916016817093f },
    { 8.255764961242676f, 4.354709625244141f, 9.800000043469481e-06f, 1.0f, 0.4710494875907898f,
      1.2999999853491317e-05f },
};

static const struct sos LOWPASS = { 0.0449824333190918f, 0.0899648666381836f, 0.0449824333190918f,
                                    1.2931621074676514f, -1.8200702667236328f, 0.7068378925323486f };

static const struct sos PRE3 = { 28.660383224487305f, -50.77084732055664f, 22.1122989654541f, 1.0f,
                                 -1.7366905212402344f, 0.7443767189979553f };

static const struct sos POWER[3] = {
    { 0.6662471294403076f, -1.2943296432495117f, 0.628348171710968f, 1.0f, -1.9863110780715942f,
      0.9865562915802002f },
    { 1.2943296432495117f, -0.013454199768602848f, 3.8800000766059384e-05f, 1.0f, -1.19711172580719f,
      0.2672421932220459f },
    { 1.1741849184036255f, -1.2943296432495117f, 0.1447090059518814f, 1.0f, -0.028996000066399574f,
      0.0007891000132076442f },
};

struct state {
    float gain, volume, bass, middle, treble;   /* glided */
    float presence;                             /* the knob the presence section was computed for */
    struct sos gain_c, pres_c;
    struct tone3 tone;
    struct sos_state pre[2], gs, lowpass, pre3, pres, power[3];
    struct env_state env;
    struct stage st;
};

/* the gain section from the glided Gain g */
static void model_gain_set(struct sos *c, float g)
{
    float g2 = g * g;
    c->b0 = g * -0.10000000149011612f;
    c->b1 = g * 0.19023600220680237f;
    c->b2 = g * -0.09023599326610565f;
    c->a0 = st_fma(g, -1.0280989408493042f, -0.10280989855527878f) + g2;
    c->a1 = st_fma(g2, -1.902359962463379f, st_fma(g, 1.9018570184707642f, 0.19018569588661194f));
    c->a2 = st_fma(g2, 0.9023599028587341f, st_fma(g, -0.874764084815979f, -0.08747640997171402f));
}

/* the tone stack's constants (bog_tone_set) */
static const struct bog_ts TS = {
    .ab = 0.021948210895061493f, .am = 0.0009793691569939256f, .m3 = 0.0008165742037817836f,
    .n1m = 2.693495988845825f, .t1 = 0.001038764021359384f, .n0mb = 0.9510713219642639f,
    .n2mb = 2.5337769985198975f, .n3m = -0.7913529872894287f, .nc = 0.01865573041141033f,
    .t2 = 0.0010226670419797301f,
    .q = 0.00017178589769173414f, .f3 = 0.00022852800611872226f, .f4 = 0.0002249866956844926f, .f5 = 0.5f,
    .f6 = 0.00012851299834437668f, .f7 = 0.0001635230000829324f, .f8 = 0.16666670143604279f,
    .e00 = 0.0011458309600129724f, .e0m = 0.0010080150095745921f, .e0t = 0.00025208271108567715f,
    .h0 = 1.1261659860610962f, .ht = 0.24542680382728577f, .h2 = -0.22343569993972778f, .d0 = 0.9790787100791931f,
    .dt = 0.1584548056125641f,
    .k0 = -0.0009139410103671253f, .km = -0.0008394226897507906f, .kt = -0.0002010669995797798f,
    .i0 = -3.122641086578369f, .i2 = 0.663435697555542f, .it = -0.6847347021102905f, .j0 = -2.7714219093322754f,
    .jt = -0.4417881965637207f,
    .mm0 = -0.0011425119591876864f, .mmm = -0.0010079320054501295f, .mmt = -0.0002513526997063309f,
    .l0 = 2.873837947845459f, .lt = 0.634573221206665f, .l2 = -0.6565642952919006f, .g0 = 2.6057701110839844f,
    .gt = 0.4082117974758148f,
    .p0 = 0.0009172591962851584f, .pm = 0.0008395056938752532f, .pt = 0.00020179699640721083f,
    .n0c = -0.8773555159568787f, .nt = -0.1952652931213379f, .n2 = 0.21656429767608643f, .o0 = -0.8134260177612305f,
    .ot = -0.12487850338220596f,
};

/* the presence section from Presence p: a shelf (amp.h), e = 2^(0.6643856 p), K = 2.0840607, the middle terms
   -0.569 */
static void presence_set(struct sos *c, float p)
{
    shelf_set(c, p, 0.6643856167793274f, 2.0840606689453125f, -0.5690549612045288f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow3_5, k[K_GAIN]);
    s->presence = st_taper(st_pow2, k[K_PRESENCE]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow3, k[K_BASS]);
    s->middle = k[K_MIDDLE] * 0.01f;
    s->treble = st_taper(st_pow3, k[K_TREBLE]);
    model_gain_set(&s->gain_c, s->gain);
    bog_tone_set(&s->tone, &TS, s->bass, s->middle, s->treble);
    presence_set(&s->pres_c, s->presence);
    return 0;
}

/*
 * The chain of the header stage by stage over the block: every stage sees the samples it would see sample by
 * sample, in the same order, with the same coefficients (each glide advances once per sample in the stage that uses
 * it; the knobs that do not glide can only change before the block's first sample), and keeps its state in registers
 * over the block.
 */
static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gain = st_taper(st_pow3_5, io->knobs[K_GAIN]);
    float presence = st_taper(st_pow2, io->knobs[K_PRESENCE]);
    float volume = io->knobs[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow3, io->knobs[K_BASS]);
    float middle = io->knobs[K_MIDDLE] * 0.01f;
    float treble = st_taper(st_pow3, io->knobs[K_TREBLE]);
    float *p = io->io, x[WVFX_MAX_FRAMES], k[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->bass = st_glide(s->bass, bass);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    if (s->presence != presence) {
        s->presence = presence;
        presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i] * 1.1f;
    sos_block(&PRE[0], &s->pre[0], x, n);
    sos_block(&PRE[1], &s->pre[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            model_gain_set(&s->gain_c, s->gain);
        x[i] = sos_run_div(&s->gain_c, &s->gs, x[i]);
    }
    sos_block_div(&LOWPASS, &s->lowpass, x, n);
    sos_block(&PRE3, &s->pre3, x, n);
    for (int i = 0; i < n; i++) {
        x[i] = x[i] * 0.25f;
        k[i] = st_fma(bog_env_run(&s->env, x[i]), 1.7000000476837158f, 1.5f);
    }
    stage_env_block(&s->st, x, k, n, 0.15000000596046448f, 0.15000000596046448f);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->treble != treble || s->bass != bass || s->middle != middle)
            bog_tone_set(&s->tone, &TS, s->bass, s->middle, s->treble);
        x[i] = s->volume * 45.0f * tone3_run_b1b0(&s->tone, x[i]);
    }
    sos_block_div(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 3; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 2.265199899673462f;
}

const struct wvfx_desc bog_svod_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x3D),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Bog SV OD",
    .category = "Bog",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
