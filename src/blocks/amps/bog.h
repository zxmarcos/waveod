/*
 * The pieces the original's Bog amps share (docs/stock_port.md): the SV's envelope, the presence section, the SV OD's
 * and XT's tone stack. The generic ones are in amp.h. Each model's file strings them in its order with its tables.
 */
#pragma once
#include "amp.h"

#define BOG_PARAM(name, def, knob) { name, 0.0f, 100.0f, 1.0f, def, knob, 0x15, 0, 0, 0, 0 }

/* the SV's envelope: |x| through a fixed 2nd-order low pass (the Calif Star's) */
static inline float bog_env_run(struct env_state *e, float x)
{
    return env_run(e, x, 9.920032653099042e-07f, -1.9980080127716064f, 0.9980090260505676f);
}

/* the presence section from Presence p: a shelf (amp.h), e = 2^(1.0796267 p), K = 2.1507726, the middle terms
   -0.291 */
static inline void bog_presence_set(struct sos *c, float p)
{
    shelf_set(c, p, 1.0796266794204712f, 2.1507725715637207f, -0.29103848338127136f);
}

/* the SV OD's and XT's tone stack: polynomials in Bass b, Middle m and Treble t, the same shape in both, the
   constants per model (named after the terms they scale; t2s is t^2) */
struct bog_ts {
    float ab, am, m3, n1m, t1, n0mb, n2mb, n3m, nc, t2;     /* numerator */
    float q, f3, f4, f5, f6, f7, f8;                        /* the t^2 m, t m and m b t^2 terms */
    float e00, e0m, e0t, h0, ht, h2, d0, dt;                /* a0 */
    float k0, km, kt, i0, i2, it, j0, jt;                   /* a1 */
    float mm0, mmm, mmt, l0, lt, l2, g0, gt;                /* a2 */
    float p0, pm, pt, n0c, nt, n2, o0, ot;                  /* a3 */
};

static inline void bog_tone_set(struct tone3 *o, const struct bog_ts *c, float b, float m, float t)
{
    float mb = m * b, tb = t * b, tb3 = tb * 3.0f, t2s = t * t, tm = t * m;
    float t1 = t * c->t1, t2 = t * c->t2, m3 = m * c->m3;
    float a = st_fma(m, c->am, b * c->ab);
    float q = (t2s * m) * c->q;
    float f4 = st_fma(t2s, c->f4, q), f3 = st_fma(t2s, c->f3, q);
    float f7 = tm * c->f7, f6 = tm * c->f6;
    float f8 = (mb * c->f8) * t2s, f5 = (mb * c->f5) * t2s;
    o->b[0] = (st_fma(mb, c->n0mb, a) + t1) + tb;
    o->b[1] = st_fma(-st_fma(m, c->n1m, c->nc), b, -(t2 + (m3 + tb3)));
    o->b[2] = st_fma(mb, c->n2mb, tb3) - (a + t1);
    o->b[3] = st_fma(st_fma(m, c->n3m, c->nc), b, (t2 + m3) - tb);
    float e0 = st_fma(t, c->e0t, st_fma(m, c->e0m, c->e00));
    float h = st_fma(st_fma(t, c->dt, c->d0), m, st_fma(t2s, c->h2, st_fma(t, c->ht, c->h0)));
    o->a[0] = st_fma(h, b, (e0 + f7) - (f3 + f8));
    float k = st_fma(t, c->kt, st_fma(m, c->km, f4 + c->k0));
    float i = st_fma(st_fma(t, c->jt, c->j0), m, st_fma(t, c->it, st_fma(t2s, c->i2, c->i0)));
    o->a[1] = st_fma(i, b, (k - f6) + f5);
    float mm = st_fma(t, c->mmt, st_fma(m, c->mmm, f3 + c->mm0));
    float l = st_fma(st_fma(t, c->gt, c->g0), m, st_fma(t2s, c->l2, st_fma(t, c->lt, c->l0)));
    o->a[2] = st_fma(l, b, mm - (f7 + f5));
    float p = st_fma(t, c->pt, st_fma(m, c->pm, c->p0));
    float n = st_fma(st_fma(t, c->ot, c->o0), m, st_fma(t, c->nt, st_fma(t2s, c->n2, c->n0c)));
    o->a[3] = st_fma(n, b, ((p + f6) - f4) + f8);
}

/* the XT Blue's and Red's */
static const struct bog_ts BOG_XT_TS = {
    .ab = 0.02127886936068535f, .am = 0.0009600669727660716f, .m3 = 0.000807765987701714f,
    .n1m = 2.6473889350891113f, .t1 = 0.001038764021359384f, .n0mb = 0.9322378039360046f,
    .n2mb = 2.4980649948120117f, .n3m = -0.7829136848449707f, .nc = 0.018200650811195374f,
    .t2 = 0.0010226670419797301f,
    .q = 0.00039042250136844814f, .f3 = 0.0005193817778490484f, .f4 = 0.0005113335209898651f,
    .f5 = 1.1363639831542969f, .f6 = 0.0003016162954736501f, .f7 = 0.00037615711335092783f,
    .f8 = 0.37878790497779846f,
    .e00 = 0.0011389959836378694f, .e0m = 0.0010170170571655035f, .e0t = 0.0005694982246495783f,
    .h0 = 1.120048999786377f, .ht = 0.5547305941581726f, .h2 = -0.5078083872795105f, .d0 = 0.9879186153411865f,
    .dt = 0.3645572066307068f,
    .k0 = -0.0009208808769471943f, .km = -0.0008538099937140942f, .kt = -0.0004604404966812581f,
    .i0 = -3.116534948348999f, .i2 = 1.507807970046997f, .it = -1.5532560348510742f, .j0 = -2.8029189109802246f,
    .jt = -1.0221329927444458f,
    .mm0 = -0.0011358900228515267f, .mmm = -0.0010168609442189336f, .mmt = -0.0005679451278410852f,
    .l0 = 2.8799569606781006f, .lt = 1.445268988609314f, .l2 = -1.492192029953003f, .g0 = 2.6423840522766113f,
    .gt = 0.9505943059921265f,
    .p0 = 0.0009239873033948243f, .pm = 0.0008539652917534113f, .pt = 0.00046199370990507305f,
    .n0c = -0.8834589123725891f, .nt = -0.4467441141605377f, .n2 = 0.4921916127204895f, .o0 = -0.8273835778236389f,
    .ot = -0.29301849007606506f,
};

/* the XT's pre-filters, peak and low pass */
static const struct sos BOG_XT_PRE[2] = {
    { 18.12818717956543f, -35.05553436279297f, 16.947229385375977f, 1.0f, -1.766971230506897f, 0.7805444002151489f },
    { 35.05553436279297f, 18.1639461517334f, 0.006391800008714199f, 1.0f, -0.10578390210866928f,
      8.300000445160549e-06f },
};
static const struct sos BOG_XT_PEAK = { 1.0426000356674194f, -1.9869999885559082f, 0.9574000239372253f,
                                        1.0757999420166016f, -1.9869999885559082f, 0.9241999983787537f };
static const struct sos BOG_XT_LOWPASS = { 0.007289975881576538f, 0.014579951763153076f, 0.007289975881576538f,
                                           1.1203248500823975f, -1.9708400964736938f, 0.8796751499176025f };

/* the XT's presence section from Presence p (shelves_set) */
static inline void bog_xt_presence_set(struct sos *c, float p)
{
    shelves_set(c, p, 0.830482006072998f, -0.49828922748565674f, 0.06420212984085083f, 0.018522391095757484f);
}
