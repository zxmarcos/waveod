/*
 * D-Chorus (MOD › Chorus): the original's D-Chorus (QME-200 V1.2.2, library entry 8.1, id 0x04000002) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input; a two-voice chorus with the voices
 * crossed between the outputs. Single precision except the right output's level.
 *
 * The path: a fixed 3rd-order input filter (y1); the dry part 1.42 y1 + 0.214 x a 1st-order low pass of it; a
 * 3rd-order low pass into two 430-sample delay lines read 165 + 263 d m samples back, m a triangle LFO ramping 0..1
 * by the Mode's step per sample (the two in opposite phase; linear interpolation), each read through the same low
 * pass. The left voice: dry + 1.23 (HP12(read L) + 1.2 HP14(read R)) less the right voice's last feedback, through a
 * 2nd-order section (out L), whose output through a second section feeds the right voice: dry + 1.23 HP12(read R)
 * less it, through the first section (out R x 1.3, in double), and through the second into the left's feedback.
 * Mode 0-3 picks the step and the depth d (0.5, 0.7333, 0.9, 1); d glides 0.00022676 of the way each sample.
 */
#include "chorus.h"

static const struct wvfx_param params[] = {
    { "Mode", 0.0f, 3.0f, 1.0f, 0.0f, 0, 0x7f, 0x00, 0, 0, 0 },
};

static const struct lp3 IN = {
    -2.0732457637786865f, 1.9044160842895508f, 2.0732076168060303f, -1.904454231262207f, -0.9702293872833252f,
    -0.42138150334358215f, 0.39171960949897766f,
};
static const struct fos DRY_LP = { 0.007521783001720905f, 0.007521783001720905f, -0.9849564433097839f };
static const struct lp3 LP = {
    0.34466680884361267f, 1.0340003967285156f, 1.0340003967285156f, 0.34466680884361267f, 0.9776689410209656f,
    0.554726243019104f, 0.2249392569065094f,
};
static const struct fos HP12 = { 0.9867755174636841f, -0.9867755174636841f, -0.9735509753227234f };
static const struct fos HP14 = { 0.9841058850288391f, -0.9841058850288391f, -0.9682117700576782f };
static const struct sos S17 = {
    -0.20074692368507385f, -0.07802966237068176f, 0.12271725386381149f, 1.0f, 0.015007911249995232f,
    -0.8589485883712769f,
};
static const struct sos S19 = {
    -1.2439178228378296f, 2.3821678161621094f, -1.1382499933242798f, 1.0f, -1.4057284593582153f, 0.4211772084236145f,
};

/* each Mode's LFO step per sample and depth */
static const float MODE_STEP[4] = { 1.7939175450010225e-05f, 1.7939175450010225e-05f, 2.3723199774394743e-05f,
                                    2.3723199774394743e-05f };
static const float MODE_DEPTH[4] = { 0.5f, 0.7333333492279053f, 0.8999999761581421f, 1.0f };

struct state {
    float step, target, depth;
    float m1, m2, sign1, sign2;         /* the two ramps and their directions */
    struct lp3_state in, lp, lpl, lpr;
    struct fos_state dry, hp12l, hp12r, hp14;
    struct sos_state s17l, s17r, s19l, s19r;
    MOD_LINE(430) l, r;
};

static int mode_of(float k)
{
    for (int i = 0; i < 4; i++)
        if (k == (float)i)
            return i;
    return -1;
}

/* m moves by sign x step; at 1 it turns down, below 0 up */
static void ramp(float *m, float *sign, float step)
{
    *m = st_fma(*sign, step, *m);
    if (*m >= 1.0f) {
        *m = 1.0f;
        *sign = -1.0f;
    } else if (*m < 0.0f) {
        *m = 0.0f;
        *sign = 1.0f;
    }
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    int m = mode_of(k[0]);
    if (m >= 0) {
        s->depth = MODE_DEPTH[m];
        s->step = MODE_STEP[m];
    }
    s->sign1 = 1.0f;
    s->sign2 = -1.0f;
    s->m2 = 1.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    int m = mode_of(io->knobs[0]);
    if (m >= 0) {
        s->step = MODE_STEP[m];
        s->target = MODE_DEPTH[m];
    }
    if (!io->enabled || io->frames == 0)
        return;
    float dd = (s->target - s->depth) * 0.0002267600066261366f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float y1 = lp3_run(&IN, &s->in, p[0]);
        float y3 = fos_run(&DRY_LP, &s->dry, y1), w = lp3_run(&LP, &s->lp, y1);
        s->depth += dd;
        ramp(&s->m1, &s->sign1, s->step);
        ramp(&s->m2, &s->sign2, s->step);
        float width = s->depth * 263.0f;
        float rl = lp3_run(&LP, &s->lpl, mod_line_run(&s->l, w, st_fma(width, s->m1, 165.0f)));
        float rr = lp3_run(&LP, &s->lpr, mod_line_run(&s->r, w, st_fma(width, s->m2, 165.0f)));
        float h12l = fos_run(&HP12, &s->hp12l, rl), h12r = fos_run(&HP12, &s->hp12r, rr);
        float h14 = fos_run(&HP14, &s->hp14, rr);
        float dry = y3 * 0.21400000154972076f + y1 * 1.4199999570846558f;
        float l = sos_run(&S17, &s->s17l, st_fma(st_fma(h14, 1.2000000476837158f, h12l), 1.2300000190734863f,
                                                 dry - s->s19r.y1));
        float fl = sos_run(&S19, &s->s19l, l);
        float r = sos_run(&S17, &s->s17r, st_fma(h12r, 1.2300000190734863f, dry) - fl);
        sos_run(&S19, &s->s19r, r);
        p[0] = l;
        p[1] = (float)((double)r * 1.3);
    }
}

const struct wvfx_desc mod_d_chorus_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x101),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "D-Chorus",
    .category = "Chorus",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
