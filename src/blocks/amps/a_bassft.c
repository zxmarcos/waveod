/*
 * A BassFT (AMP › Bass): the original's A BassFT bass amp (QME-200 V1.2.2, library entry 4.59, id 0x08000075; its
 * code calls it the Ampage Flip) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the
 * left input, the result on both outputs. Single precision.
 *
 * Per sample:
 *   pre-filter    a fixed 2nd-order section
 *   tone stack    4th order from Bass (held in 0.06 .. 0.99) and Treble, its coefficients polynomials in both (the
 *                 original's fused order); divided by its a0
 *   gain          a 2nd-order section from Volume (held in 0.01 .. 0.99; v^2 terms); divided by its a0
 *   stage         half of that through the amps' 4-points stage with a power clipper 40 - 40 (1 - |v| / 2560)^1024
 *                 (odd, held at +-40) and the 40-tap FIR; x 0.1173
 * Volume, Bass and Treble glide 0.0001 per sample, bypassed too; the gain section and the tone stack are recomputed
 * while they have not reached the knobs, so not at the last step (as the original). The Init sets them from the
 * knobs as they are (Bass and Volume not held). Bypassed it passes the signal.
 */
#include "amp.h"

static const struct wvfx_param params[] = {
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
};

static const struct sos PF = {
    15.342845916748047f, 14.394908905029297f, 1.956452488899231f, 1.0f, 0.7974165081977844f, 0.028524400666356087f,
};

struct state {
    float volume, bass, treble;
    struct sos gain;
    float n[5], d[5];
    struct sos_state pf, gs;
    float ex[5], ey[5];
    struct stage st;
};

/* the tone stack from the glided Bass and Treble */
static void eq_set(struct state *s)
{
    float b = s->bass, t = s->treble, *n = s->n, *d = s->d;
    n[0] = st_fma(st_fma((b * b), -0.9899979829788208f, (b + 0.0010907480027526617f)), t, st_fma(st_fma(b,
        -0.6813899874687195f, st_fma((b * b), 0.6744701862335205f, -0.000735472422093153f)), (t * t),
        st_fma((b * b), -0.0059949918650090694f, st_fma(b, 0.006055563222616911f, 6.604810096177971e-06f))));
    n[1] = st_fma(st_fma(t, -3.802622079849243f, st_fma((t * t), 2.6448590755462646f, -0.010868280194699764f)), b,
        st_fma(st_fma(t, 3.784213066101074f, ((t * t) * -2.6317689418792725f)), (b * b), ((st_fma((t * t),
        0.0014544540317729115f,
        ((b * b) * 0.010878429748117924f)) + 8.526806141162524e-07f) - (t * 0.0020490780007094145f))));
    n[2] = st_fma(st_fma((b * b), -5.4203691482543945f, st_fma(b, 5.4188551902771f, -0.00013062759535387158f)), t,
        st_fma(st_fma(b, -3.8477370738983154f, st_fma((b * b), 3.84848690032959f, 1.6490410416736268e-05f)),
        (t * t), st_fma((b * b), 0.0010601950343698263f, st_fma(b, -0.001170643954537809f,
        -1.2344939932518173e-05f))));
    n[3] = st_fma(st_fma(t, -3.4296820163726807f, st_fma((t * t), 2.4864559173583984f, 0.010869369842112064f)), b,
        st_fma(st_fma(t, 3.4480910301208496f, ((t * t) * -2.4995460510253906f)), (b * b),
        (((t * 0.0020490780007094145f) + -8.286900765597238e-07f) - st_fma((t * t), 0.0014544540317729115f,
        ((b * b) * 0.010878429748117924f)))));
    n[4] = st_fma(st_fma((b * b), -0.8219370245933533f, st_fma(b, 0.8134486079216003f, -0.0009601201163604856f)),
        t, st_fma(st_fma(b, -0.6021884083747864f, st_fma((b * b), 0.60835862159729f, 0.0007189820171333849f)),
        (t * t), st_fma((b * b), 0.004934796132147312f, st_fma(b, -0.004883829038590193f,
        5.764125035057077e-06f))));
    d[0] = st_fma(st_fma((b * b), -8.431119918823242f, st_fma(b, 8.512934684753418f, 0.009588428772985935f)), t,
        st_fma(st_fma(b, -8.074666023254395f, st_fma((b * b), 7.997063159942627f, -0.009094790555536747f)),
        (t * t), st_fma((b * b), -0.348559707403183f, st_fma(b, 0.3519422113895416f, 0.00039443280547857285f))));
    d[1] = st_fma(st_fma(t, -32.12485122680664f, st_fma((t * t), 31.30238914489746f, -1.214020013809204f)), b,
        st_fma(st_fma((t * t), -31.147729873657227f, st_fma(t, 31.970230102539062f, 1.208724021911621f)), (b * b),
        ((-0.0005801385268568993f - (t * 0.01712954044342041f)) + ((t * t) * 0.01718411035835743f))));
    d[2] = st_fma(st_fma((b * b), -45.421058654785156f, -0.001938172965310514f), t, st_fma(st_fma((t * t),
        -45.4658317565918f, st_fma(t, 45.4121208190918f, 1.5614399909973145f)), b, st_fma(st_fma((b * b),
        45.46638107299805f, 0.001005470985546708f), (t * t), st_fma((b * b), -1.5627820491790771f,
        -0.00017723609926179051f))));
    d[3] = st_fma(st_fma(t, -28.500690460205078f, st_fma((t * t), 29.323150634765625f, -0.8869634866714478f)), b,
        st_fma(st_fma((t * t), -29.47780990600586f, st_fma(t, 28.655309677124023f, 0.8922603726387024f)), (b * b),
        (((t * 0.01712954044342041f) + 0.0005816012853756547f) - ((t * t) * 0.01718411035835743f))));
    d[4] = st_fma(st_fma((b * b), -6.7733612060546875f, -0.007650255225598812f), t, st_fma(st_fma((t * t),
        -7.085044860839844f, st_fma(t, 6.7004899978637695f, 0.18761080503463745f)), b, st_fma(st_fma((b * b),
        7.162097930908203f, 0.008089319802820683f), (t * t), st_fma((b * b), -0.18965110182762146f,
        -0.0002157339040422812f))));
}

/* the gain section from Volume v; the Init's from the raw knob (k = 100 v, the x 0.01 folded into the constants) */
static void gain_set(struct state *s, float v)
{
    float v2 = v * v, b1 = (v - v2) + (v - v2);
    s->gain = (struct sos){ st_fma(v, -1.0022679567337036f, v2), b1, st_fma(v, -0.997732400894165f, v2),
                            st_fma(v, -0.9994331002235413f, -0.002835752908140421f) + v2, b1 + -2.570945071056485e-06f,
                            st_fma(v, -1.0005669593811035f, 0.002833181992173195f) + v2 };
}

static void gain_init(struct state *s, float k)
{
    float v = k * 0.01f, v2 = v * v, b1 = (v - v2) + (v - v2);
    s->gain = (struct sos){ st_fma(k, -0.010022679343819618f, v2), b1, st_fma(k, -0.009977323934435844f, v2),
                            st_fma(k, -0.009994330815970898f, -0.002835752908140421f) + v2,
                            b1 + -2.570945071056485e-06f, st_fma(k, -0.010005669668316841f, 0.002833181992173195f) + v2 };
}

/* 40 - 40 (1 - |v| / 2560)^1024 above zero, the mirror below, held at +-40 */
static float clamp(float x, float lo, float hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ .volume = k[0] * 0.01f, .bass = k[1] * 0.01f, .treble = k[2] * 0.01f };
    eq_set(s);
    gain_init(s, k[0]);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float vol = clamp(k[0] * 0.01f, 0.01f, 0.9900000095367432f), bass = clamp(k[1] * 0.01f, 0.05999999865889549f,
                                                                            0.9900000095367432f);
    float treble = k[2] * 0.01f;
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    /* everything up to the stage per sample, the stage over the block after (the same samples) */
    for (int i = 0; i < n; i++, p += 2) {
        s->volume = st_glide(s->volume, vol);
        s->bass = st_glide(s->bass, bass);
        s->treble = st_glide(s->treble, treble);
        if (!io->enabled)
            continue;
        if (s->treble != treble || s->bass != bass)
            eq_set(s);
        if (s->volume != vol)
            gain_set(s, s->volume);
        float y = sos_run_div(&PF, &s->pf, p[0]);
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[4], ex[4], st_fma(n[3], ex[3], st_fma(n[2], ex[2], st_fma(n[1], ex[1], n[0] * y))));
        float dd = st_fma(d[4], ey[4], st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1])));
        float e = (nn - dd) / d[0];
        for (int j = 4; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = y;
        ey[1] = ey[0] = e;
        float g = sos_run_div(&s->gain, &s->gs, e);
        x[i] = g * 0.5f;
    }
    if (!io->enabled)
        return;
    stage_pow_block(&s->st, x, n, 40.0f, 0.0003906250058207661f);    /* +-40, 40 - 40 (1 - |v| / 2560)^1024 */
    p = io->io;
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.11726399511098862f;
}

const struct wvfx_desc a_bassft_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x75),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "A BassFT",
    .category = "Bass",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
