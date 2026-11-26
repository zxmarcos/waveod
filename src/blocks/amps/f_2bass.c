/*
 * F-2Bass (AMP › Bass): the original's F-2Bass bass preamp (QME-200 V1.2.2, library entry 4.60, id 0x08000076; its
 * code calls it the Alchemy Pre) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the
 * left input, the result on both outputs. Single precision, no clipping stage.
 *
 * Per sample:
 *   pre-filters   two fixed 2nd-order sections
 *   tone stack    3rd order from Bass (A25v1), Middle and Treble (B25): its coefficients sums of the three and their
 *                 products b m, b t (the Init and the process sum them in different orders: both kept); divided by a0
 *   bright        two fixed 2nd-order sections, one pair with Bright 0, another otherwise
 *   output        x 0.005877 Volume (B25)
 * The four knobs glide 0.0001 per sample, bypassed too; the tone stack is recomputed while one of its three has not
 * reached its knob, so not at the last step (as the original). Bypassed it passes the signal.
 */
#include "amp.h"
#include "stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Bright", 0.0f, 1.0f, 1.0f, 0.0f, 1, 0x00, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Middle", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PF1 = {
    5.588955879211426f, -9.255999565124512f, 3.667083740234375f, 1.0f, -1.656741976737976f, 0.6567491888999939f,
};
static const struct sos PF2 = {
    9.255999565124512f, 2.803436517715454f, 0.08509229868650436f, 1.0f, 0.16325199604034424f, 0.0009379999828524888f,
};
/* [Bright != 0][first, second] */
static const struct sos POST[2][2] = {
    { { 3.0221645832061768f, -0.01484730001538992f, 2.7000000955013093e-06f, 1.0f, -0.4131614863872528f,
        1.0999999631167157e-06f },
      { 5.038599967956543f, 1.94712495803833f, 0.05081399902701378f, 1.0f, 0.1937119960784912f,
        4.999999987376214e-07f } },
    { { 4.365490436553955f, -7.311999797821045f, 2.9514377117156982f, 1.0f, -1.6602227687835693f,
        0.6614282727241516f },
      { 7.311999797821045f, 0.00040869999793358147f, 0.006918800063431263f, 1.0f, -5.729999975301325e-05f,
        0.001082699978724122f } },
};

struct state {
    float volume, bass, middle, treble;
    float n[4], d[4];
    struct sos_state pf1, pf2, post[2][2];
    float ex[4], ey[4];
};

/* the tone stack as the Init sums it */
static void eq_init(struct state *s)
{
    float b = s->bass, m = s->middle, t = s->treble, mb = m * b, tb = t * b, *n = s->n, *d = s->d;
    n[0] = (((t * 0.0014195339754223824f) + (b * 0.004269454162567854f)) +
            st_fma(mb, 0.1581858992576599f, m * 0.0002243566996185109f)) + tb;
    n[1] = st_fma(b, -0.0033920500427484512f, mb * -0.4381859004497528f) -
           (((m * 0.00017280649626627564f) + (t * 0.0014173459494486451f)) + (tb * 3.0f));
    n[2] = ((mb * 0.4018141031265259f) -
            (((b * 0.004269454162567854f) + (m * 0.0002243566996185109f)) + (t * 0.0014195339754223824f))) +
           (tb * 3.0f);
    n[3] = (st_fma(b, 0.0033920500427484512f, st_fma(mb, -0.12181410193443298f, m * 0.00017280649626627564f)) +
            (t * 0.0014173459494486451f)) - tb;
    d[0] = st_fma(b, 1.185675024986267f, 0.0016770459478721023f) +
           st_fma(mb, 0.1581858992576599f, m * 0.0002243566996185109f);
    d[1] = (st_fma(b, -3.184798002243042f, -0.0011594370007514954f) - (m * 0.00017280649626627564f)) +
           (mb * -0.4381859004497528f);
    d[2] = (st_fma(b, 2.8143250942230225f, -0.0016762519953772426f) - (m * 0.0002243566996185109f)) +
           (mb * 0.4018141031265259f);
    d[3] = st_fma(b, -0.8152021169662476f,
                  st_fma(mb, -0.12181410193443298f, m * 0.00017280649626627564f) + 0.001160230953246355f);
}

/* the tone stack as the process sums it */
static void eq_set(struct state *s)
{
    float b = s->bass, m = s->middle, t = s->treble, bm = b * m, bt = b * t, *n = s->n, *d = s->d;
    float tb0 = st_fma(b, 0.004269454162567854f, t * 0.0014195339754223824f);
    float mid = st_fma(bm, 0.1581858992576599f, m * 0.0002243566996185109f);
    n[0] = (tb0 + bt) + mid;
    n[1] = st_fma(b, -0.0033920500427484512f, bm * -0.4381859004497528f) -
           (st_fma(t, 0.0014173459494486451f, m * 0.00017280649626627564f) + (bt * 3.0f));
    n[2] = ((bt * 3.0f) - ((m * 0.0002243566996185109f) + tb0)) + (bm * 0.4018141031265259f);
    n[3] = (st_fma(b, 0.0033920500427484512f, st_fma(t, 0.0014173459494486451f, m * 0.00017280649626627564f)) - bt) +
           (bm * -0.12181410193443298f);
    d[0] = st_fma(b, 1.185675024986267f, 0.0016770459478721023f) + mid;
    d[1] = st_fma(b, -3.184798002243042f, -0.0011594370007514954f - (m * 0.00017280649626627564f)) +
           (bm * -0.4381859004497528f);
    d[2] = st_fma(b, 2.8143250942230225f, -0.0016762519953772426f - (m * 0.0002243566996185109f)) +
           (bm * 0.4018141031265259f);
    d[3] = st_fma(b, -0.8152021169662476f,
                  ((bm * -0.12181410193443298f) + (m * 0.00017280649626627564f)) + 0.001160230953246355f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ .volume = st_taper(st_b25, k[0]), .bass = st_taper(st_a25v1, k[2]),
                         .middle = st_taper(st_b25, k[3]), .treble = st_taper(st_b25, k[4]) };
    eq_init(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float vol = st_taper(st_b25, k[0]), bass = st_taper(st_a25v1, k[2]), mid = st_taper(st_b25, k[3]);
    float treble = st_taper(st_b25, k[4]);
    int bright = k[1] != 0.0f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->volume = st_glide(s->volume, vol);
        s->middle = st_glide(s->middle, mid);
        s->treble = st_glide(s->treble, treble);
        s->bass = st_glide(s->bass, bass);
        if (!io->enabled)
            continue;
        if (s->treble != treble || s->middle != mid || s->bass != bass)
            eq_set(s);
        float y = sos_run(&PF2, &s->pf2, sos_run(&PF1, &s->pf1, p[0]));
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[3], ex[3], st_fma(n[2], ex[2], st_fma(n[1], ex[1], n[0] * y)));
        float dd = st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1]));
        float e = (nn - dd) / d[0];
        for (int j = 3; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = y;
        ey[1] = ey[0] = e;
        float o = sos_run(&POST[bright][1], &s->post[bright][1], sos_run(&POST[bright][0], &s->post[bright][0], e));
        p[0] = p[1] = (s->volume * 0.005877375137060881f) * o;
    }
}

const struct wvfx_desc f_2bass_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x76),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "F-2Bass",
    .category = "Bass",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
