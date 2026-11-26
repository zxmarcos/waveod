/*
 * Full OD (DRIVE › OD): the original's Full OD (QME-200 V1.2.2, library entry 3.6, id 0x0300000A) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs. Single
 * precision except the gain section's coefficients.
 *
 * Per sample: a 3rd-order gain section (Gain, A25; x 1.5 when Mode is 0; coefficients in double), x 1.5, a rational
 * clipper on 4 points per sample (its own polynomials each side, 0 at 0) with the anti-aliasing FIR, a fixed
 * 2nd-order section, a 1st-order tone section (Tone, B25; Mode picks its curve), x 0.49 Volume (A25). Gain and Volume
 * glide 0.005 per block (also while bypassed) and the gain section follows Gain's glide; Tone and Mode are read as
 * they are, only while the block is on.
 *
 * Quirks kept: the glide and a Tone change use the Mode the block last saw; a Mode change then recomputes both.
 */
#include "drive.h"

static const struct wvfx_param params[] = {
    { "Gain", 0.0f, 100.0f, 1.0f, 40.0f, 0, 0x15, 0, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 60.0f, 1, 0x15, 0, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0, 0, 0, 0 },
    { "Mode", 0.0f, 1.0f, 1.0f, 1.0f, 3, 0x15, 0, 0, 0, 0 },
};

static const struct sos TO1 = {
    1.9806691408157349f, -1.4827721118927002f, -0.4949321150779724f, 1.0f, -1.4827721118927002f, 0.4857370853424072f,
};

struct state {
    float gain, tone, mode, volume;     /* Gain and Volume glided (tapers), Tone as read (taper), Mode */
    struct sos3 pf;
    struct fos to2;                     /* the tone section, a0 in to2_a0 */
    float to2_a0;
    struct sos3_state pfs;
    struct sos_state t1;
    struct fos_state t2;
    struct stage st;
};

/* the gain section from the Gain taper g, in double, x 1.5 when the Mode m is 0 */
static void gain_set(struct sos3 *c, float g, float m)
{
    double G = (double)(m == 0.0f ? g * 1.5f : g);
    *c = (struct sos3){ (float)st_fmad(G, 22.27666, 0.4513879), (float)st_fmad(G, -24.09508, -0.4770171),
                        (float)st_fmad(G, -18.63983, -0.3859249), (float)st_fmad(G, 20.45825, 0.411554),
                        (float)(G + 0.06953577), (float)st_fmad(G, -2.856958, -0.0955892),
                        (float)st_fmad(G, 2.714217, -0.002664279), (float)st_fmad(G, -0.8572597, 0.02874886) };
}

/* the tone section from the Tone taper t, its curve by the Mode m */
static void tone_set(struct state *s, float t, float m)
{
    float c, a1, a0;
    if (m == 0.0f) {
        c = (t * 1.0003999471664429f) / (t + 0.37400001287460327f);
        a1 = -3.27474308013916f - c;
        a0 = c + 3.325256824493408f;
    } else {
        c = (t * 0.800000011920929f) / (t + 0.10000000149011612f);
        a1 = -1.2947430610656738f - c;
        a0 = c + 1.3452569246292114f;
    }
    s->to2 = (struct fos){ c + 0.02412312477827072f, 0.02412312477827072f - c, a1 };
    s->to2_a0 = a0;
}

/* the clipper: (n0 x^2 + (n1 + n2 x^2) |x|) / (d1 + d2 |x| + (d3 +- |x|) x^2), each side its own; 0 at 0 */
static inline float clip(float x)
{
    float a = st_abs(x), x2 = x * x, n, d;
    if (x < 0.0f) {
        n = st_fma(st_fma(x2, 0.6366999745368958f, 0.20029999315738678f), a, x2 * 0.7160999774932861f);
        d = st_fma(-1.8229999542236328f - a, x2, st_fma(a, 0.2004999965429306f, -0.3310999870300293f));
    } else if (x > 0.0f) {
        n = st_fma(st_fma(x2, 1.5920000076293945f, 1.843000054359436f), a, x2 * -0.8945000171661377f);
        d = st_fma(a + 0.43849998712539673f, x2, st_fma(a, 0.7975000143051147f, 2.005000114440918f));
    } else {
        return 0.0f;
    }
    return n / d;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_a25, k[0]);
    s->tone = st_taper(st_b25, k[1]);
    s->volume = st_taper(st_a25, k[2]);
    s->mode = k[3];
    gain_set(&s->pf, s->gain, s->mode);
    tone_set(s, s->tone, s->mode);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    float gt = st_taper(st_a25, io->knobs[0]), t = st_taper(st_b25, io->knobs[1]), m = io->knobs[3];
    s->gain = st_glide_by(s->gain, gt, 0.005f);
    s->volume = st_glide_by(s->volume, st_taper(st_a25, io->knobs[2]), 0.005f);
    if (io->frames == 0 || !io->enabled)
        return;
    float level = s->volume * 0.49000000953674316f;
    /* stage by stage over the block (every coefficient is the block's: set before the first sample as the original
       sets it at it, again at every sample to the same values): the same samples as sample by sample */
    if (s->gain != gt)
        gain_set(&s->pf, s->gain, s->mode);
    if (s->tone != t) {
        s->tone = t;
        tone_set(s, t, s->mode);
    }
    if (s->mode != m) {
        s->mode = m;
        gain_set(&s->pf, s->gain, m);
        tone_set(s, s->tone, m);
    }
    float *p = io->io, x[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    for (int i = 0; i < n; i++)
        x[i] = sos3_run_div(&s->pf, &s->pfs, p[2 * i]) * 1.5f;
    STAGE_BLOCK_POINTS(&s->st, x, n, buf, clip(v));
    stage_block_fir(&s->st, buf, x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = level * fos_run_div(&s->to2, s->to2_a0, &s->t2, sos_run_b1(&TO1, &s->t1, x[i]));
}

const struct wvfx_desc full_od_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DRV, 0x106),
    .menu = WVFX_DRV,
    .nparams = sizeof params / sizeof params[0],
    .name = "Full OD",
    .category = "OD",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
