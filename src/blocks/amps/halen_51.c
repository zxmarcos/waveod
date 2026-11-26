/*
 * Halen 51 (AMP › Halen): the original's Halen 51 amp (QME-200 V1.2.2, library entry 4.49, id 0x0700005A) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both outputs.
 * Single precision throughout.
 *
 * Per sample (amp.h has the pieces):
 *   pre-filters   two fixed 2nd-order sections
 *   gain          a 2nd-order section from Gain (pow2_5; divided by its a0), a low pass (also)
 *   stage         the asymmetric power clipper +-0.5 (k 1 / 204.8 below zero, 1 / 146.3 above) on 4 points per
 *                 sample, the FIR
 *   tone stack    3rd order from Bass, Middle (pow2_5) and Treble (linear), with m^2, t^2 and m^2 t^2 terms
 *   presence      two shelves from Presence (pow4; up to the taper's point 26 it counts as 0.01), on 3 x Volume x
 *                 the tone stack
 *   power amp     four fixed 2nd-order sections, x 0.6196
 * Gain, Volume, Middle and Treble glide 0.0001 per sample, Bass and Presence take effect at once; the coefficients
 * stop one glide step short of the knobs. Presence is the knob word 6 (the original leaves word 5 unused).
 */
#include "amp.h"

enum { K_GAIN, K_VOLUME, K_BASS, K_MIDDLE, K_TREBLE, K_PRESENCE = 6 };

#define KNOB(name, k) { name, 0.0f, 100.0f, 1.0f, 50.0f, k, 0x15, 0, 0, 0, 0 }

static const struct wvfx_param params[] = {
    KNOB("Gain", K_GAIN), KNOB("Volume", K_VOLUME), KNOB("Bass", K_BASS), KNOB("Middle", K_MIDDLE),
    KNOB("Treble", K_TREBLE), KNOB("Presence", K_PRESENCE),
};

static const struct sos PF[2] = {
    { 13.271702766418457f, -25.718732833862305f, 12.448835372924805f, 1.0f, -1.9052027463912964f,
      0.9086843729019165f },
    { 25.718732833862305f, 17.032678604125977f, 4.2849555015563965f, 1.0f, -0.6983562111854553f,
      0.04672899842262268f },
};
static const struct sos LOWPASS = { 0.008551985025405884f, 0.017103970050811768f, 0.008551985025405884f,
                                    1.1302413940429688f, -1.9657920598983765f, 0.8697585463523865f };
static const struct sos POWER[4] = {
    { 0.6875929832458496f, -1.313806414604187f, 0.6280083060264587f, 1.0f, -1.9427403211593628f,
      0.9442933201789856f },
    { 1.313806414604187f, 0.18840719759464264f, 0.3976357877254486f, 1.0f, 0.44458478689193726f,
      0.47705620527267456f },
    { 1.313806414604187f, -0.0710282027721405f, 1.0338363647460938f, 1.0f, -0.08692699670791626f,
      0.8083435297012329f },
    { 0.9007272124290466f, 1.313806414604187f, 0.6369361281394958f, 1.0f, 1.4017421007156372f,
      0.7002798914909363f },
};

struct state {
    float gain, volume, middle, treble;     /* glided */
    float bass, presence;                   /* the knobs the tone stack and the presence were set for */
    struct sos gain_c, pres_c;
    struct sos_state pf[2], gs, lowpass, pres, power[4];
    struct stage st;
    struct tone3 tone;
};

/* the gain section from the glided Gain g: (0.976 g^2 - 0.983 g, 1.931 (g - g^2), 0.954 g^2 - 0.947 g) /
   (g^2 - 0.984 g - 0.0235, -1.930 g^2 + 1.930 g - 0.00095, 0.931 g^2 - 0.947 g + 0.0225) */
static void gain_set(struct sos *c, float g)
{
    float g2 = g * g;
    c->b0 = st_fma(g2, 0.9761977791786194f, g * -0.9834938049316406f);
    c->b1 = (g - g2) * 1.930508017539978f;
    c->b2 = st_fma(g2, 0.9543099999427795f, g * -0.9470140933990479f);
    c->a0 = st_fma(g, -0.9841318726539612f, -0.023460540920495987f) + g2;
    c->a1 = st_fma(g2, -1.9299739599227905f, st_fma(g, 1.9303300380706787f, -0.0009487656061537564f));
    c->a2 = st_fma(g2, 0.9310414791107178f, st_fma(g, -0.9465538263320923f, 0.022511780261993408f));
}

/*
 * The tone stack from Bass b, Middle m and Treble t (m2 = m^2, t2 = t^2): ni = (b terms) + t terms + (b, t
 * polynomial) m + (b, t polynomial) m2; di = (b polynomial) +- the m^2 b t^2 and m b t^2 terms, then polynomials
 * scaled by m, t, t2 and m2. Each fused from its constant in the original's order.
 */
#define F st_fma
static void tone_set(struct tone3 *o, float b, float m, float t)
{
    float m2 = m * m, t2 = t * t, tb = t * b, mb = m * b, m2b = m2 * b;
    float TB1 = tb * 0.7403758764266968f, TB3 = tb * 2.221127986907959f;
    float bk0 = b * 0.011833390220999718f, bk1 = b * 0.010508069768548012f;
    float n = F(t, 0.012469490058720112f, (bk0 + 0.0001911233994178474f) + TB1);
    n = F(F(t, 0.0009027631022036076f, b + 0.0162927508354187f), m, n);
    o->b[0] = F(F(t, -0.012860720045864582f, F(b, -0.5154638886451721f, -0.016151199117302895f)), m2, n);
    n = F(t, -0.03637984022498131f, -0.00016971799777820706f - (bk1 + TB3));
    n = F(F(t, 0.0383031889796257f, F(b, 1.4801119565963745f, 0.04637686163187027f)), m2, n);
    o->b[1] = F(F(t, -0.002429310930892825f, F(b, -2.871417999267578f, -0.046497538685798645f)), m, n);
    n = F(t, 0.035362351685762405f, (-0.0001911233994178474f - bk0) + TB3);
    n = F(F(t, 0.002150333020836115f, F(b, 2.7428359985351562f, 0.04415855184197426f)), m, n);
    o->b[2] = F(F(t, -0.038024209439754486f, F(b, -1.4138330221176147f, -0.044300101697444916f)), m2, n);
    n = F(t, -0.01145200990140438f, (bk1 + 0.00016971799777820706f) - TB1);
    n = F(F(t, 0.012581749819219112f, F(b, 0.4491845965385437f, 0.014074450358748436f)), m2, n);
    o->b[3] = F(F(t, -0.0006237850175239146f, F(b, -0.8714181184768677f, -0.013953760266304016f)), m, n);
    float F6 = (m2b * 0.10149919986724854f) * t2, F8 = (mb * 0.19690850377082825f) * t2;
    float F3 = (m2b * 0.3044975996017456f) * t2, F4 = (mb * 0.5907254219055176f) * t2;
    float d = (F(b, 0.8119447827339172f, 0.013666120357811451f) + F6) - F8;
    d = F(F(b, 1.0404020547866821f, 0.017919890582561493f), m, d);
    d = F(F(F(b, 0.17515060305595398f, 0.0031079279724508524f), m, F(b, 0.2012445032596588f, 0.003388399025425315f)),
          t, d);
    d = F(F(m, -0.003438778920099139f, F(b, -0.18712350726127625f, -0.0031501529738307f)), t2, d);
    o->a[0] = F(F(F(b, -0.09028381109237671f, -0.006299885921180248f), t,
                  F(t2, 0.006395489908754826f, F(b, -0.5362898707389832f, -0.030687719583511353f))), m2, d);
    d = (F(b, -2.2912399768829346f, -0.03747110068798065f) - F3) + F4;
    d = F(F(b, -2.985793113708496f, -0.05082378908991814f), m, d);
    d = F(F(m, 0.010181030258536339f, F(b, 0.557311475276947f, 0.009127738885581493f)), t2, d);
    d = F(F(F(b, -0.4949299097061157f, -0.008643620647490025f), m, F(b, -0.5711178779602051f, -0.009340442717075348f)),
          t, d);
    o->a[1] = F(F(F(b, 0.2551184892654419f, 0.017819780856370926f), t,
                  F(t2, -0.0191167201846838f, F(b, 1.5390690565109253f, 0.08752840012311935f))), m2, d);
    d = (F(b, 2.14955997467041f, 0.03416663035750389f) + F3) - F4;
    d = F(F(b, 2.85050892829895f, 0.04797609895467758f), m, d);
    d = F(F(F(b, 0.46440809965133667f, 0.007984988391399384f), m, F(b, 0.5391312837600708f, 0.008569782599806786f)),
          t, d);
    d = F(F(m, -0.010045729577541351f, F(b, -0.5532522797584534f, -0.008807807229459286f)), t2, d);
    o->a[2] = F(F(F(b, -0.23938560485839844f, -0.016750989481806755f), t,
                  F(t2, 0.019046980887651443f, F(b, -1.4693349599838257f, -0.08304011076688766f))), m2, d);
    d = (F(b, -0.6702619791030884f, -0.010359839536249638f) - F6) + F8;
    d = F(F(b, -0.905117928981781f, -0.015072209760546684f), m, d);
    d = F(F(m, 0.0033034749794751406f, F(b, 0.18306440114974976f, 0.0028302210848778486f)), t2, d);
    d = F(F(F(b, -0.14462879300117493f, -0.0024492950178682804f), m, F(b, -0.1692579984664917f, -0.0026172949001193047f)),
          t, d);
    o->a[3] = F(F(F(b, 0.07455091178417206f, 0.0052311010658741f), t,
                  F(t2, -0.006325745023787022f, F(b, 0.4665555953979492f, 0.026199430227279663f))), m2, d);
}
#undef F

static float presence_knob(float k)
{
    return (int)(k * 2.55f) < 27 ? 0.01f : st_taper(st_pow4, k);
}

static void presence_set(struct sos *c, float p)
{
    shelves_set_x(c, p * 0.830482006072998f, p * -0.16609640419483185f, 0.03563403710722923f, 0.07126807421445847f);
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->gain = st_taper(st_pow2_5, k[K_GAIN]);
    s->volume = k[K_VOLUME] * 0.01f;
    s->bass = st_taper(st_pow2_5, k[K_BASS]);
    s->middle = st_taper(st_pow2_5, k[K_MIDDLE]);
    s->treble = k[K_TREBLE] * 0.01f;
    s->presence = presence_knob(k[K_PRESENCE]);
    gain_set(&s->gain_c, s->gain);
    tone_set(&s->tone, s->bass, s->middle, s->treble);
    presence_set(&s->pres_c, s->presence);
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float gain = st_taper(st_pow2_5, k[K_GAIN]), volume = k[K_VOLUME] * 0.01f;
    float bass = st_taper(st_pow2_5, k[K_BASS]), middle = st_taper(st_pow2_5, k[K_MIDDLE]);
    float treble = k[K_TREBLE] * 0.01f, ptaper = st_taper(st_pow4, k[K_PRESENCE]);
    float *p = io->io, x[WVFX_MAX_FRAMES];
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    if (!io->enabled || n <= 0) {
        for (int i = 0; i < n; i++) {
            s->gain = st_glide(s->gain, gain);
            s->volume = st_glide(s->volume, volume);
            s->middle = st_glide(s->middle, middle);
            s->treble = st_glide(s->treble, treble);
        }
        return;
    }
    /* stage by stage over the block (each glide once per sample where it is used; Presence, not glided, before the
       first sample): the same samples as sample by sample */
    if (s->presence != ptaper) {
        s->presence = presence_knob(k[K_PRESENCE]);
        presence_set(&s->pres_c, s->presence);
    }
    for (int i = 0; i < n; i++)
        x[i] = p[2 * i];
    sos_block(&PF[0], &s->pf[0], x, n);
    sos_block(&PF[1], &s->pf[1], x, n);
    for (int i = 0; i < n; i++) {
        s->gain = st_glide(s->gain, gain);
        if (s->gain != gain)
            gain_set(&s->gain_c, s->gain);
        x[i] = sos_run_div(&s->gain_c, &s->gs, x[i]);
    }
    sos_block_div(&LOWPASS, &s->lowpass, x, n);
    stage_pow_asym_block(&s->st, x, n, 0.5f, 0.0048828125f, 0.0068359375f);
    for (int i = 0; i < n; i++) {
        s->volume = st_glide(s->volume, volume);
        s->middle = st_glide(s->middle, middle);
        s->treble = st_glide(s->treble, treble);
        if (s->bass != bass || s->middle != middle || s->treble != treble) {
            s->bass = bass;
            tone_set(&s->tone, s->bass, s->middle, s->treble);
        }
        x[i] = s->volume * 3.0f * tone3_run_b1b0(&s->tone, x[i]);
    }
    sos_block(&s->pres_c, &s->pres, x, n);
    for (int j = 0; j < 4; j++)
        sos_block(&POWER[j], &s->power[j], x, n);
    for (int i = 0; i < n; i++)
        p[2 * i] = p[2 * i + 1] = x[i] * 0.6195999979972839f;
}

const struct wvfx_desc halen_51_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_AMP, 0x5A),
    .menu = WVFX_AMP,
    .nparams = sizeof params / sizeof params[0],
    .name = "Halen 51",
    .category = "Halen",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
