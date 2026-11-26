/*
 * Fat Boost (DYN): the original's Fat Boost (QME-200 V1.2.2, library entry 0.8, id 0x00000019; its code calls it the
 * FET Boost) in C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). A boost, mono (the left input;
 * both outputs the same). Single precision.
 *
 * x through a gain section set by Volume (v = the original's VolumeLine taper): 1st order with Low Cut off, 2nd order
 * with it on (divided by its a0 = v + 0.47); the amps' 4-points stage (amp.h) held at +-4 and the 40-tap FIR; a
 * 4th-order tone section by Bass (BassLine taper) and Treble (B25 taper), its coefficients polynomials (the
 * original's fused order; divided by its a0); x a level that fades the output out and back in (0.0001 per sample)
 * when Low Cut changes, the gain section switched and cleared at the silent point. Volume and Treble glide 0.0001 per
 * sample; the gain section is recomputed while Volume has not reached the knob and the tone section while Treble has
 * not or Bass changed (Bass jumps), so neither takes the last step (as the original). Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Bass", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Treble", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 20.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Low Cut", 0.0f, 1.0f, 1.0f, 0.0f, 3, 0x00, 0x00, 0, 0, 0 },
};

struct state {
    float bass, treble, volume, lowcut;
    float wet, wet_old;                 /* the fade's target and level */
    float g1n0, g1n1, g1d1;             /* the 1st-order gain section (Low Cut off); its a0 is g2.a0 */
    struct sos g2;                      /* the 2nd-order one (Low Cut on), divided by a0 */
    float n[5], d[5];                   /* the tone section, d[0] divides */
    float x1, y1;
    struct sos_state s2;
    float ex[5], ey[5];
    struct stage st;
};

static void gain_set(struct state *s, float v)
{
    s->g1n0 = v * -31.28759002685547f;
    s->g1n1 = v * 31.28759002685547f;
    s->g1d1 = st_fma(v, -0.9970802664756775f, -0.4686277210712433f);
    s->g2 = (struct sos){ v * -30.837650299072266f, v * 61.64051055908203f, v * -30.8028507232666f,
                          v + 0.4699999988079071f, st_fma(v, -1.9672490358352661f, -0.9246068000793457f),
                          st_fma(v, 0.9672517776489258f, 0.45460841059684753f) };
}

/* the tone section from the Bass and Treble tapers */
static void eq_set(struct state *s)
{
    float b = s->bass, t = s->treble, *n = s->n, *d = s->d;
    n[0] = st_fma(((t * t) * b), 0.4015323221683502f, st_fma(((t * t) * (b * b)), -0.4014213979244232f,
        st_fma((t * t), 0.0026656086556613445f, st_fma(((b * b) * t), 0.999889075756073f, st_fma(t,
        -0.006712773349136114f, (st_fma((b * b), 0.12793216109275818f, st_fma(b, -0.12796081602573395f,
        -0.0008524833247065544f)) - (t * b)))))));
    n[1] = st_fma(((t * b) - ((b * b) * t)), 3.9136204719543457f, st_fma(((t * t) * (b * b)), 1.5734000205993652f,
        ((st_fma((b * b), -0.4046037793159485f, st_fma(b, 0.4045904874801636f,
        0.0010385150089859962f)) + (t * 0.013152368366718292f))
        - (((t * t) * 0.005294834263622761f) + (((t * t) * b) * 1.5734000205993652f)))));
    n[2] = st_fma(((t * t) * b), 2.3114497661590576f, st_fma(((t * t) * (b * b)), -2.311671733856201f,
        st_fma((t * t), -3.6383109545568004e-05f, st_fma((t * b), -5.741748809814453f, st_fma(((b * b) * t),
        5.741970539093018f, st_fma(t, 0.0002731785934884101f, st_fma((b * b), 0.4570861756801605f, st_fma(b,
        -0.4570429027080536f, 0.0006254286272451282f))))))));
    n[3] = st_fma(((t * b) - ((b * b) * t)), 3.74263596534729f, st_fma(((t * t) * (b * b)), 1.50882887840271f,
        ((st_fma((b * b), -0.21184198558330536f, st_fma(b, 0.21185383200645447f,
        -0.0010387535439804196f)) + ((t * t) * 0.005294834263622761f))
        - ((t * 0.013152368366718292f) + (((t * t) * b) * 1.50882887840271f)))));
    n[4] = st_fma(((t * t) * b), 0.3692467510700226f, st_fma(((t * t) * (b * b)), -0.3691358268260956f,
        st_fma((t * t), -0.0026292256079614162f, st_fma((t * b), -0.9145078063011169f, st_fma(((b * b) * t),
        0.9143968820571899f, st_fma(t, 0.006439594551920891f, st_fma((b * b), 0.03142742067575455f, st_fma(b,
        -0.03144349902868271f, 0.00022681606060359627f))))))));
    d[0] = st_fma(((t * t) * b), -0.4013104736804962f, st_fma(((t * t) * (b * b)), 0.4014213979244232f,
        st_fma((t * t), -0.002776532666757703f, st_fma((t * b), -0.19715721905231476f, st_fma(((b * b) * t),
        0.19704629480838776f, st_fma(t, -0.001270631910301745f, st_fma((b * b), -0.726399838924408f, st_fma(b,
        0.726371169090271f, 0.004928302951157093f))))))));
    d[1] = st_fma(((t * b) - ((b * b) * t)), 0.7668204307556152f, (st_fma(((t * t) * (b * b)),
        -1.5734000205993652f, st_fma((b * b), 2.744824171066284f, st_fma(b, -2.744837522506714f, (st_fma(t,
        0.002562699606642127f,
        ((t * t) * 0.005294834263622761f)) + -0.008882745169103146f)))) + (((t * t) * b) * 1.5734000205993652f)));
    d[2] = st_fma(((t * t) * b), -2.3118934631347656f, st_fma(((t * t) * (b * b)), 2.311671733856201f,
        st_fma((t * t), 0.00025823136093094945f, st_fma((t * b), -1.1184053421020508f, st_fma(((b * b) * t),
        1.1186271905899048f, st_fma(t, -2.1435864255181514e-05f, st_fma((b * b), -3.887385129928589f, st_fma(b,
        3.8874282836914062f, -0.0009055025293491781f))))))));
    d[3] = st_fma(((t * b) - ((b * b) * t)), 0.7249783277511597f, (st_fma(((t * t) * (b * b)), -1.50882887840271f,
        (st_fma((b * b), 2.4456491470336914f, st_fma(b, -2.4456372261047363f, 0.008884438313543797f)) - st_fma(t,
        0.002562699606642127f, ((t * t) * 0.005294834263622761f)))) + (((t * t) * b) * 1.50882887840271f)));
    d[4] = st_fma(((t * t) * b), -0.3690249025821686f, st_fma(((t * t) * (b * b)), 0.3691358268260956f,
        st_fma((t * t), 0.0025183013640344143f, st_fma((t * b), -0.17623616755008698f, st_fma(((b * b) * t),
        0.17612524330615997f, st_fma(t, 0.001292067812755704f, st_fma((b * b), -0.5766884684562683f, st_fma(b,
        0.576672375202179f, -0.004021106753498316f))))))));
}

/* v held at +-4 (VMAXNM, VMINNM: no flags; the same as the compares for every v but NaN) */
static inline float hold4(float v)
{
    return __builtin_fminf(__builtin_fmaxf(v, -4.0f), 4.0f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->lowcut = k[3];
    s->bass = st_taper(st_bassline, k[0]);
    s->treble = st_taper(st_b25, k[1]);
    s->volume = st_taper(st_volumeline_fet, k[2]);
    s->wet = s->wet_old = 1.0f;
    gain_set(s, s->volume);
    eq_set(s);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float bass = st_taper(st_bassline, k[0]), treble = st_taper(st_b25, k[1]);
    float vol = st_taper(st_volumeline_fet, k[2]);
    if (s->lowcut != k[3])
        s->wet = 0.0f;                                      /* fade out, switch, fade in */
    float *p = io->io, y[WVFX_MAX_FRAMES], wv[WVFX_MAX_FRAMES], buf[STAGE_BLOCK_PTS];
    const int nf = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || nf <= 0)
        return;
    /* stage by stage over the block: the Volume's glide (its gain section) and the wet fade (a Low Cut switch
       clears the input sections at its sample) before the stage, Treble's glide (the EQ) after it, each once per
       sample: the same samples as sample by sample */
    for (int i = 0; i < nf; i++) {
        s->volume = st_glide(s->volume, vol);
        if (s->volume != vol)
            gain_set(s, s->volume);
        float w = st_glide(s->wet_old, s->wet);
        if (w == 0.0f) {
            s->wet = 1.0f;
            s->x1 = s->y1 = 0.0f;
            s->s2 = (struct sos_state){ 0 };
            s->lowcut = k[3];
        }
        s->wet_old = w;
        wv[i] = w;
        float x = p[2 * i];
        if (s->lowcut == 0.0f) {
            y[i] = st_fma(-s->g1d1, s->y1, st_fma(s->g1n1, s->x1, s->g1n0 * x)) / s->g2.a0;   /* VFMS */
            s->x1 = x;
            s->y1 = y[i];
        } else {
            y[i] = sos_run_div(&s->g2, &s->s2, x);
        }
    }
    STAGE_BLOCK_POINTS(&s->st, y, nf, buf, hold4(v));
    stage_block_fir(&s->st, buf, y, nf);
    for (int i = 0; i < nf; i++, p += 2) {
        float t = st_glide(s->treble, treble);
        s->treble = t;
        if (s->bass != bass || t != treble) {
            s->bass = bass;
            eq_set(s);
        }
        float f = y[i], w = wv[i];
        float *ex = s->ex, *ey = s->ey, *n = s->n, *d = s->d;
        float nn = st_fma(n[4], ex[4], st_fma(n[3], ex[3], st_fma(n[2], ex[2], st_fma(f, n[0], n[1] * ex[1]))));
        float dd = st_fma(d[4], ey[4], st_fma(d[3], ey[3], st_fma(d[2], ey[2], d[1] * ey[1])));
        float e = (nn - dd) / d[0];
        for (int j = 4; j > 1; j--) {
            ex[j] = ex[j - 1];
            ey[j] = ey[j - 1];
        }
        ex[1] = ex[0] = f;
        ey[1] = ey[0] = e;
        p[0] = p[1] = e * w;
    }
}

const struct wvfx_desc dyn_fat_boost_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x108),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "Fat Boost",
    .category = "Boost",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
