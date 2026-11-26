/*
 * AC Sim (DYN): the original's AC Sim (QME-200 V1.2.2, library entry 0.10, id 0x01000001) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). An acoustic simulator for an electric guitar, mono (the left input;
 * both outputs the same). Single precision.
 *
 * x (faded, below) through a fixed 2nd-order section, then three paths summed, x Volume (the Vol_0 taper):
 *   top    a 3rd-order section (one for Mode 2, another for the rest), x (G1 + G0 Top) (Top taper; 0.1 + 1.077 Top in
 *          Mode 3, 0.292 + 0.885 Top otherwise), a 2nd-order band pass
 *   mid    a 2nd-order band pass on x, one per Mode, x (1.315 - 1.052 Top), a 2nd-order band pass
 *   body   a 3rd-order low pass on x and from it three branches, each a 2nd-order band pass (its own by Mode) then a
 *          1st-order low pass (its own by Mode), summed, x (0.9999 Body + 0.0001) (Body taper), a 2nd-order band
 *          pass
 * All the sections are fixed (the original's sections 3 to 31 by their numbers; 17 has a1 = 0 as in the original);
 * Mode (0-3) picks among them. Body, Top and Volume glide 0.0001 per sample. A change of Mode fades the input out
 * (0.0005 per sample), switches at silence and fades back in; the input is scaled by the level of the sample before.
 * The sections Mode does not use keep their state. Bypassed it passes the signal.
 */
#include "../amps/amp.h"
#include "../amps/stock_tapers.h"

static const struct wvfx_param params[] = {
    { "Body", 0.0f, 100.0f, 1.0f, 50.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Top", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Volume", 0.0f, 100.0f, 1.0f, 50.0f, 2, 0x15, 0x00, 0, 0, 0 },
    { "Mode", 0.0f, 3.0f, 1.0f, 2.0f, 3, 0x45, 0x00, 0, 0, 0 },
};

/* the input */
static const struct sos S3 = { -5.3614707f, 10.722941f, -5.3614707f, 1.0f, -0.78983414f, 0.35475406f };
/* top: Mode 2, the others */
static const struct lp3 S4 = { 0.4971324f, -1.4913971f, 1.4913971f, -0.4971324f, -1.565557f, 1.0829331f, -0.32856914f };
static const struct lp3 S5 = { 0.39147347f, -1.1744204f, 1.1744204f, -0.39147347f, -1.2166047f, 0.69060504f,
                               -0.22457798f };
/* mid: Mode 0, 1, 2, 3 */
static const struct sos S6_9[4] = {
    { -0.047901355f, 0.0f, 0.047901355f, 1.0f, -1.8520434f, 0.89022255f },
    { -0.0581702f, 0.0f, 0.0581702f, 1.0f, -1.8703477f, 0.88960403f },
    { -0.05730538f, 0.0f, 0.05730538f, 1.0f, -1.8165054f, 0.890972f },
    { -0.032622658f, 0.0f, 0.032622658f, 1.0f, -1.769885f, 0.8921565f },
};
/* body: the low pass; branch 1 (a band pass, then a low pass by Mode 0/1, 2, 3) */
static const struct lp3 S10 = { 9.37884e-05f, 0.0002813652f, 0.0002813652f, 9.37884e-05f, -2.716057f, 2.4495852f,
                                -0.73277795f };
static const struct sos S11 = { -0.023020858f, 0.0f, 0.023020858f, 1.0f, -1.9739076f, 0.9792342f };
static const struct fos S12 = { 0.034130227f, 0.034130227f, -0.9218988f };
static const struct fos S14 = { 0.02843339f, 0.02843339f, -0.9331765f };
static const struct fos S15 = { 0.010793624f, 0.010793624f, -0.96830064f };
/* branch 2: a band pass by Mode 0, 1/3, 2; a low pass by Mode 0/1, 2, 3 */
static const struct sos S16 = { -0.010902693f, 0.0f, 0.010902693f, 1.0f, -1.9870361f, 0.9883105f };
static const struct sos S17 = { -0.010904815f, 0.0f, 0.010904815f, 1.0f, 0.0f, 0.9882919f };
static const struct sos S18 = { -0.010889746f, 0.0f, 0.010889746f, 1.0f, -1.9874531f, 0.98831344f };
static const struct fos S19 = { 0.034130227f, 0.034130227f, -0.9218988f };
static const struct fos S21 = { 0.04114696f, 0.04114696f, -0.9079486f };
static const struct fos S22 = { 0.01575923f, 0.01575923f, -0.9584189f };
/* branch 3: a band pass by Mode 1, the others; a low pass by Mode 3, the others */
static const struct sos S23 = { -0.00516975f, 0.0f, 0.00516975f, 1.0f, -1.9944755f, 0.9946126f };
static const struct sos S24 = { -0.0051912516f, 0.0f, 0.0051912516f, 1.0f, -1.994361f, 0.9946076f };
static const struct fos S25 = { 0.06773515f, 0.06773515f, -0.85495687f };
static const struct fos S28 = { 0.005101f, 0.005101f, -0.979596f };
/* the band passes after the top, mid and body gains */
static const struct sos S29 = { -3.0892448f, 0.0f, 3.0892448f, 1.0f, -0.16788094f, -0.05377571f };
static const struct sos S30 = { -0.97598517f, 0.0f, 0.97598517f, 1.0f, -0.73971856f, -0.1485078f };
static const struct sos S31 = { -6.9733152f, 0.0f, 6.9733152f, 1.0f, -0.8351454f, -0.1622192f };

struct state {
    float body, top, volume, mode, fade;
    struct sos_state s3, s6_9[4], s11, s16, s17, s18, s23, s24, s29, s30, s31;
    struct lp3_state s4, s5, s10;
    struct fos_state s12, s14, s15, s19, s21, s22, s25, s28;
};

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer;
    (void)env;
    *s = (struct state){ 0 };
    s->body = st_taper(st_body, k[0]);
    s->top = st_taper(st_top, k[1]);
    s->volume = st_taper(st_vol_0, k[2]);
    s->mode = k[3];
    s->fade = 1.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float body = st_taper(st_body, k[0]), top = st_taper(st_top, k[1]), vol = st_taper(st_vol_0, k[2]);
    float *p = io->io;
    for (int i = 0; i < io->frames && io->enabled; i++, p += 2) {
        float fade = s->fade;
        s->body = st_glide(s->body, body);
        s->top = st_glide(s->top, top);
        s->volume = st_glide(s->volume, vol);
        if (s->mode == k[3]) {
            s->fade = s->fade + 0.0005000000237487257f;
            if (s->fade >= 1.0f)
                s->fade = 1.0f;
        } else {
            s->fade = s->fade + -0.0005000000237487257f;
            if (s->fade < 0.0f) {
                s->fade = 0.0f;
                s->mode = k[3];
            }
        }
        float m = s->mode, x = fade * p[0];
        /* top */
        float y = sos_run(&S3, &s->s3, x);
        float a = m == 2.0f ? lp3_run(&S4, &s->s4, y) : lp3_run(&S5, &s->s5, y);
        /* mid */
        int mi = m == 0.0f ? 0 : m == 1.0f ? 1 : m == 2.0f ? 2 : 3;
        float b = sos_run(&S6_9[mi], &s->s6_9[mi], x);
        /* body */
        float c = lp3_run(&S10, &s->s10, x);
        float d = sos_run(&S11, &s->s11, c);
        float e = m == 0.0f || m == 1.0f ? fos_run(&S12, &s->s12, d)
                : m == 2.0f ? fos_run(&S14, &s->s14, d) : fos_run(&S15, &s->s15, d);
        float f = m == 0.0f ? sos_run(&S16, &s->s16, c)
                : m == 1.0f || m == 3.0f ? sos_run(&S17, &s->s17, c) : sos_run(&S18, &s->s18, c);
        float g = m == 0.0f || m == 1.0f ? fos_run(&S19, &s->s19, f)
                : m == 2.0f ? fos_run(&S21, &s->s21, f) : fos_run(&S22, &s->s22, f);
        float h = m == 1.0f ? sos_run(&S23, &s->s23, c) : sos_run(&S24, &s->s24, c);
        float j, g0, g1;
        if (m == 3.0f) {
            j = fos_run(&S28, &s->s28, h);
            g0 = 1.0769239664077759f;
            g1 = 0.10000000149011612f;
        } else {
            j = fos_run(&S25, &s->s25, h);
            g0 = 0.8846200108528137f;
            g1 = 0.29230400919914246f;
        }
        float ot = sos_run(&S29, &s->s29, st_fma(s->top, g0, g1) * a);
        float om = sos_run(&S30, &s->s30, st_fma(s->top, -1.052262306213379f, 1.3153278827667236f) * b);
        float ob = sos_run(&S31, &s->s31, st_fma(s->body, 0.9998999834060669f, 9.999000030802563e-05f) * ((g + e) + j));
        p[0] = p[1] = s->volume * ((om + ot) + ob);
    }
}

const struct wvfx_desc dyn_ac_sim_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DYN, 0x10A),
    .menu = WVFX_DYN,
    .nparams = sizeof params / sizeof params[0],
    .name = "AC Sim",
    .category = "Acoustic",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
