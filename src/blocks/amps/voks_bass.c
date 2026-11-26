/*
 * Voks Bass (AMP › Voks): the original's Voks Bass amp (QME-200 V1.2.2, library entry 4.57, id 0x07000075) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. Single precision throughout. No clipping: a tone stack between fixed filters.
 *
 * Per sample:
 *   pre-filter    two fixed 2nd-order sections
 *   tone stack    3rd order, polynomials in Bass (A25v3 x 0.9) and Treble (B25)
 *   volume        x Volume (B25) x 9.758699
 * Volume, Bass and Treble glide 0.0001 per sample; the tone coefficients stop one glide step short of the knobs.
 * Init computes the Bass's linear terms from the taper's value before the x 0.9 (with constants x 0.9).
 */
#include "amp.h"

enum { K_VOLUME, K_BASS, K_TREBLE };

#define PARAM(name, knob) { name, 0.0f, 100.0f, 1.0f, 50.0f, knob, 0x15, 0, 0, 0, 0 }
static const struct wvfx_param params[] = {
    PARAM("Volume", K_VOLUME), PARAM("Bass", K_BASS), PARAM("Treble", K_TREBLE),
};

static const struct sos FIT[2] = {
    { 0.7310824990272522f, -1.3342231512069702f, 0.6036581993103027f, 1.0f, -1.8308792114257812f,
      0.8317154049873352f },
    { 1.3342231512069702f, 0.282382994890213f, 0.03215700015425682f, 1.0f, 0.20005470514297485f,
      0.030973099172115326f },
};

/* the Bass's linear factors in n0..n3, d0..d3: Process's for the glided Bass, Init's for the taper before x 0.9 */
static const float LIN_PROCESS[8] = { -0.1387753039598465f, 0.35452160239219666f, -0.29731449484825134f,
                                      0.0815681591629982f, -1.3655329942703247f, 3.5837979316711426f,
                                      -3.074651002883911f, 0.8563905954360962f };
static const float LIN_INIT[8] = { -0.12489777058362961f, 0.31906944513320923f, -0.26758304238319397f,
                                   0.07341133803129196f, -1.2289797067642212f, 3.2254180908203125f,
                                   -2.767185926437378f, 0.7707515358924866f };

struct state {
    float volume, bass, treble;         /* glided */
    struct tone3 tone;
    struct sos_state fit[2];
};

/* the tone stack from Bass b (squared as is, its linear terms lin x c) and Treble t */
static void tone_set(struct tone3 *o, float lin, const float *c, float b, float t)
{
    float b2 = b * b, tb = t * b, tb2 = t * b2;
    float t1 = t * 0.001034429995343089f, t2 = t * 0.001029112027026713f;
    float q1 = tb2 * 0.9911131262779236f, q3 = tb2 * 2.973339080810547f;
    float n0 = st_fma(b2, 0.13883790373802185f, st_fma(lin, c[0], -0.0001355619024252519f));
    float n1 = st_fma(b2, -0.35456109046936035f, st_fma(lin, c[1], 8.918024832382798e-05f));
    float n2 = st_fma(b2, 0.29725179076194763f, st_fma(lin, c[2], 0.0001355619024252519f));
    float n3 = st_fma(b2, -0.0815286710858345f, st_fma(lin, c[3], -8.918024832382798e-05f));
    o->b[0] = (n0 - (t1 + tb)) + q1;
    o->b[1] = st_fma(tb, 3.00205397605896f, n1 + t2) - q3;
    o->b[2] = st_fma(tb, -3.0040969848632812f, n2 + t1) + q3;
    o->b[3] = st_fma(tb, 1.0020430088043213f, n3) - (t2 + q1);
    o->a[0] = st_fma(b2, 1.354693055152893f, st_fma(lin, c[4], -0.001404555980116129f));
    o->a[1] = st_fma(b2, -3.5526421070098877f, st_fma(lin, c[5], 0.0008825223194435239f));
    o->a[2] = st_fma(b2, 3.045849084854126f, st_fma(lin, c[6], 0.001402145018801093f));
    o->a[3] = st_fma(b2, -0.8478994965553284f, st_fma(lin, c[7], -0.0008849338046275079f));
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    (void)buffer; (void)env;
    struct state *s = state;
    float raw = st_taper(st_a25v3, k[K_BASS]);
    *s = (struct state){ 0 };
    s->volume = st_taper(st_b25, k[K_VOLUME]);
    s->bass = raw * 0.9f;
    s->treble = st_taper(st_b25, k[K_TREBLE]);
    tone_set(&s->tone, raw, LIN_INIT, s->bass, s->treble);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float volume = st_taper(st_b25, io->knobs[K_VOLUME]);
    float bass = st_taper(st_a25v3, io->knobs[K_BASS]) * 0.9f;
    float treble = st_taper(st_b25, io->knobs[K_TREBLE]);
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->volume = st_glide(s->volume, volume);
        s->bass = st_glide(s->bass, bass);
        s->treble = st_glide(s->treble, treble);
        if (!io->enabled)
            continue;
        if (s->treble != treble || s->bass != bass)
            tone_set(&s->tone, s->bass, LIN_PROCESS, s->bass, s->treble);
        float x = sos_run(&FIT[0], &s->fit[0], p[0]);
        x = sos_run(&FIT[1], &s->fit[1], x);
        float y = s->volume * 9.758699417114258f * tone3_run_std(&s->tone, x);
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc voks_bass_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x75),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Voks Bass",
    .category = "Voks",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
