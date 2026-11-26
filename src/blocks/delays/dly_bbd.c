/*
 * BBD (DELAY): the original's BBD delay (QME-200 V1.2.2, library entry 9.4, id 0x0B00001D) in C, its output bit for
 * bit (tools/check_stock.py; docs/stock_port.md). A bucket-brigade style stereo delay: per channel the input plus
 * 0.75 Feedback x the channel's last output goes through an anti-alias low pass (7th order), x w into a 4 s line
 * read Time back (between two samples), a soft saturation (y = x - 0.0060241 x^2 - 0.03125 x^3) and two reconstruction
 * low passes (5th and 2nd order): the channel's output. Single precision.
 *
 * The right line is read Time x Time R% back; its feedback is |Feedback| + e^(-4 R^2) (1 - |Feedback|) (R = Time R%;
 * just |Feedback| at R 1), at most 0.5 while R < 0.6, where the left one takes Feedback as it is. Spread crosses the
 * outputs: L = ((1 + Spread) l + (1 - Spread) r) / 2, R the same mirrored. Per channel: below Mix 0.5,
 * (x + 2 Mix wet) 2 Level; from 0.5, (wet + 2 (1 - Mix) x) 2 Level (bypassed with Trail on: (wet + x) 2 Level).
 * Feedback, Time R% (at least 0.01), Spread, Mix and Level glide 0.0001 per sample, the times 0.0002267 of the way
 * (with Sync on they jump); w fades from 0 to 1 by 0.0001 per sample after the Init or a clear. Bypassed with Trail on
 * the lines run on their feedback alone; with Trail at 0 everything is cleared once and the signal passes. The Init
 * reads the tempo: here the first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 1, 0x19, 0x16, 0, 0, 0 },
    { "Feedback", -100.0f, 100.0f, 1.0f, 20.0f, 2, 0x1d, 0x00, 0, 0, 0 },
    { "Time R%", 1.0f, 100.0f, 0.10000000149011612f, 61.79999923706055f, 3, 0x1e, 0x00, 0, 0, 0 },
    { "Spread", -100.0f, 100.0f, 1.0f, 100.0f, 4, 0x1d, 0x00, 0, 0, 0 },
    { "Level", 0.0f, 100.0f, 1.0f, 50.0f, 5, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 7, 0x00, 0x00, 0, 0, 0 },
};

/* the anti-alias filter (8 taps, 7 poles) and the two reconstruction filters */
static const float NUM_AA[8] = {
    2.8081625714548863e-05f, 0.0032426018733531237f, 0.01229358371347189f, 0.017813386395573616f,
    0.011916614137589931f, 0.0035167457535862923f, 0.00034109599073417485f, 6.656459390796954e-06f,
};
static const float DEN_AA[7] = {
    0.2700248658657074f, -2.3521013259887695f, -0.4762425422668457f, 1.8582518100738525f, 0.30789822340011597f,
    -0.4814563989639282f, -0.07721597701311111f,
};
static const float NUM_R1[6] = {
    0.0005151601508259773f, 0.030418215319514275f, 0.0675298199057579f, 0.047705404460430145f,
    0.010333969257771969f, 1.1068735148000997e-05f,
};
static const float DEN_R1[5] = {
    -0.6330031156539917f, -1.1645663976669312f, 0.7652950882911682f, 0.3377612829208374f, -0.14897207915782928f,
};
static const float NUM_R2[3] = { 0.014402961358428001f, 0.12374170124530792f, 0.013226140290498734f };
static const float DEN_R2[2] = { -1.7567249536514282f, 0.9080592393875122f };

struct channel {
    struct dly_line line;
    float xa[8], ya[8];                 /* the anti-alias filter's last inputs and outputs ([k] = k samples ago) */
    float x1[6], y1[6], x2[3], y2[3];   /* the reconstruction filters' */
    float out;                          /* the channel's last output (the wet) */
};

struct state {
    float mix, feedback, ratio, spread, level, wet, len0, len1;
    float time0, sync0, ratio0;         /* the Init's Time, Sync and Time R%, for the first process call */
    int started, cleared;
    struct channel l, r;
};

/* the anti-alias filter: in now, xa/ya the past */
static float antialias(struct channel *c, float in)
{
    float *x = c->xa, *y = c->ya;
    float n = NUM_AA[0] * in;
    for (int k = 1; k < 8; k++)
        n = st_fma(x[k], NUM_AA[k], n);
    float d = y[1] * DEN_AA[0];
    for (int k = 2; k < 8; k++)
        d = st_fma(y[k], DEN_AA[k - 1], d);
    float o = n - d;
    for (int k = 7; k > 1; k--) {
        x[k] = x[k - 1];
        y[k] = y[k - 1];
    }
    x[1] = x[0] = in;
    y[1] = y[0] = o;
    return o;
}

static float recon1(struct channel *c, float in)
{
    float *x = c->x1, *y = c->y1;
    float n = st_fma(in, NUM_R1[0], NUM_R1[1] * x[1]);
    for (int k = 2; k < 6; k++)
        n = st_fma(NUM_R1[k], x[k], n);
    float d = DEN_R1[0] * y[1];
    for (int k = 2; k < 6; k++)
        d = st_fma(DEN_R1[k - 1], y[k], d);
    float o = n - d;
    for (int k = 5; k > 1; k--) {
        x[k] = x[k - 1];
        y[k] = y[k - 1];
    }
    x[1] = x[0] = in;
    y[1] = y[0] = o;
    return o;
}

static float recon2(struct channel *c, float in)
{
    float *x = c->x2, *y = c->y2;
    float n = st_fma(NUM_R2[2], x[2], st_fma(NUM_R2[1], x[1], NUM_R2[0] * in));
    float d = st_fma(DEN_R2[1], y[2], DEN_R2[0] * y[1]);
    float o = n - d;
    x[2] = x[1];
    x[1] = x[0] = in;
    y[2] = y[1];
    y[1] = y[0] = o;
    return o;
}

/* one channel: its line and filters, then its mix (x the channel's input, trailing: bypassed with Trail on) */
static float channel_run(struct state *s, struct channel *c, float x, float len, float fb, int trailing, int enabled)
{
    float in = antialias(c, st_fma(fb * 0.75f, c->out, trailing ? -0.0f : x));
    dly_write(&c->line, s->wet * in);
    float a = dly_read(&c->line, (int)len, len - __builtin_truncf(len));             /* VRINTZ */
    float a2 = a * a;
    a = st_fma(a * -0.03125f, a2, st_fma(a2, -0.006024099886417389f, a));
    float wet = recon2(c, recon1(c, a));
    c->out = wet;
    float y;
    if (s->mix < 0.5f)
        y = st_fma(wet + wet, s->mix, x);
    else if (trailing && !enabled)
        y = wet + x;
    else
        y = st_fma(x + x, 1.0f - s->mix, wet);
    return y * (s->level + s->level);
}

static void clear(struct state *s)
{
    float *bl = s->l.line.buf, *br = s->r.line.buf;
    s->l = (struct channel){ { bl, 0 } };
    s->r = (struct channel){ { br, 0 } };
    __builtin_memset(bl, 0, DLY_BUFFER);
    s->wet = 0.0f;
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    s->mix = k[0] * 0.01f;
    s->feedback = k[2] * 0.01f;
    s->spread = k[4] * 0.01f;
    s->level = k[5] * 0.01f;
    s->ratio = s->ratio0 = k[3] * 0.01f;
    s->time0 = k[1];
    s->sync0 = k[6];
    s->l.line.buf = buffer;
    s->r.line.buf = (float *)buffer + DLY_LINE;
    clear(s);
    return 0;
}

/*
 * One channel's line over a block (the same operations on the same values as channel_run sample by sample), when
 * every read is of a sample written before the block (whole delay > the block): the reads first, through the
 * saturation and the reconstruction filters (the channel's outputs), then per sample the anti-alias filter on the
 * input plus the feedback of the output before, written x w. len[i], fb[i], w[i]: the sample's.
 */
static void channel_block(struct channel *c, const float *x, const float *len, const float *fb, const float *w,
                          int trailing, float *wet, int n)
{
    struct dly_line *l = &c->line;
    for (int i = 0; i < n; i++) {
        int wi = l->w + 1 + i;                  /* the write position after sample i's write */
        while (wi >= DLY_LINE)
            wi -= DLY_LINE;
        struct dly_line at = { l->buf, wi };
        float a = dly_read(&at, (int)len[i], len[i] - __builtin_truncf(len[i]));          /* VRINTZ */
        float a2 = a * a;
        wet[i] = st_fma(a * -0.03125f, a2, st_fma(a2, -0.006024099886417389f, a));
    }
    {   /* recon1 then recon2 over the block, their pasts in registers */
        float *x1 = c->x1, *y1 = c->y1, *x2 = c->x2, *y2 = c->y2;
        float u1 = x1[1], u2 = x1[2], u3 = x1[3], u4 = x1[4], u5 = x1[5];
        float v1 = y1[1], v2 = y1[2], v3 = y1[3], v4 = y1[4], v5 = y1[5];
        float p1 = x2[1], p2 = x2[2], q1 = y2[1], q2 = y2[2];
        for (int i = 0; i < n; i++) {
            float in = wet[i];
            float nn = st_fma(in, NUM_R1[0], NUM_R1[1] * u1);
            nn = st_fma(NUM_R1[2], u2, nn);
            nn = st_fma(NUM_R1[3], u3, nn);
            nn = st_fma(NUM_R1[4], u4, nn);
            nn = st_fma(NUM_R1[5], u5, nn);
            float d = DEN_R1[0] * v1;
            d = st_fma(DEN_R1[1], v2, d);
            d = st_fma(DEN_R1[2], v3, d);
            d = st_fma(DEN_R1[3], v4, d);
            d = st_fma(DEN_R1[4], v5, d);
            float o = nn - d;
            u5 = u4; u4 = u3; u3 = u2; u2 = u1; u1 = in;
            v5 = v4; v4 = v3; v3 = v2; v2 = v1; v1 = o;
            float n2 = st_fma(NUM_R2[2], p2, st_fma(NUM_R2[1], p1, NUM_R2[0] * o));
            float d2 = st_fma(DEN_R2[1], q2, DEN_R2[0] * q1);
            float o2 = n2 - d2;
            p2 = p1; p1 = o;
            q2 = q1; q1 = o2;
            wet[i] = o2;
        }
        x1[1] = x1[0] = u1; x1[2] = u2; x1[3] = u3; x1[4] = u4; x1[5] = u5;
        y1[1] = y1[0] = v1; y1[2] = v2; y1[3] = v3; y1[4] = v4; y1[5] = v5;
        x2[1] = x2[0] = p1; x2[2] = p2;
        y2[1] = y2[0] = q1; y2[2] = q2;
    }
    /* the anti-alias filter (antialias()) with its past in registers */
    float out = c->out, *xa = c->xa, *ya = c->ya;
    float a1 = xa[1], a2 = xa[2], a3 = xa[3], a4 = xa[4], a5 = xa[5], a6 = xa[6], a7 = xa[7];
    float b1 = ya[1], b2 = ya[2], b3 = ya[3], b4 = ya[4], b5 = ya[5], b6 = ya[6], b7 = ya[7];
    for (int i = 0; i < n; i++) {
        float in = st_fma(fb[i] * 0.75f, out, trailing ? -0.0f : x[i]);
        float nn = NUM_AA[0] * in;
        nn = st_fma(a1, NUM_AA[1], nn);
        nn = st_fma(a2, NUM_AA[2], nn);
        nn = st_fma(a3, NUM_AA[3], nn);
        nn = st_fma(a4, NUM_AA[4], nn);
        nn = st_fma(a5, NUM_AA[5], nn);
        nn = st_fma(a6, NUM_AA[6], nn);
        nn = st_fma(a7, NUM_AA[7], nn);
        float d = b1 * DEN_AA[0];
        d = st_fma(b2, DEN_AA[1], d);
        d = st_fma(b3, DEN_AA[2], d);
        d = st_fma(b4, DEN_AA[3], d);
        d = st_fma(b5, DEN_AA[4], d);
        d = st_fma(b6, DEN_AA[5], d);
        d = st_fma(b7, DEN_AA[6], d);
        float o = nn - d;
        a7 = a6; a6 = a5; a5 = a4; a4 = a3; a3 = a2; a2 = a1; a1 = in;
        b7 = b6; b6 = b5; b5 = b4; b4 = b3; b3 = b2; b2 = b1; b1 = o;
        dly_write(l, w[i] * o);
        out = wet[i];
    }
    xa[1] = xa[0] = a1; xa[2] = a2; xa[3] = a3; xa[4] = a4; xa[5] = a5; xa[6] = a6; xa[7] = a7;
    ya[1] = ya[0] = b1; ya[2] = b2; ya[3] = b3; ya[4] = b4; ya[5] = b5; ya[6] = b6; ya[7] = b7;
    c->out = out;
}

/* process() over a block, stage by stage; 0 when a read could be of a sample of this block (the caller then runs
   the sample loop) */
static int block(struct state *s, const struct wvfx_io *io, float mix, float fbk, float ratio, float spread,
                 float level, float sync, float len0, float len1, float d0, float d1, int trailing)
{
    const int n = io->frames <= WVFX_MAX_FRAMES ? io->frames : WVFX_MAX_FRAMES;
    float L0[WVFX_MAX_FRAMES], L1[WVFX_MAX_FRAMES], F0[WVFX_MAX_FRAMES], F1[WVFX_MAX_FRAMES];
    float W[WVFX_MAX_FRAMES], M[WVFX_MAX_FRAMES], V[WVFX_MAX_FRAMES], S[WVFX_MAX_FRAMES];
    float xl[WVFX_MAX_FRAMES], xr[WVFX_MAX_FRAMES], wl[WVFX_MAX_FRAMES], wr[WVFX_MAX_FRAMES];
    /* the glides, on copies: committed only if the block runs here */
    float feedback = s->feedback, rt = s->ratio, sp = s->spread, mx = s->mix, lv = s->level, wt = s->wet;
    float l0 = s->len0, l1 = s->len1;
    for (int i = 0; i < n; i++) {
        feedback = st_glide(feedback, fbk);
        rt = st_glide(rt, ratio);
        sp = st_glide(sp, spread);
        mx = st_glide(mx, mix);
        lv = st_glide(lv, level);
        if (sync == 1.0f) {
            l0 = len0;
            l1 = len1;
        } else {
            l1 = l1 + d1;
            l0 = l0 + d0;
        }
        if ((int)l0 <= n || (int)l1 <= n)
            return 0;
        float f = st_abs(feedback);
        if (rt != 1.0f)
            f = st_fma(st_expf((rt * rt) * -4.0f), 1.0f - f, f);
        F0[i] = feedback;
        F1[i] = 0.6f > rt ? __builtin_fminf(f, 0.5f) : f;                                 /* VMINNM, VSELGT */
        L0[i] = l0;
        L1[i] = l1;
        W[i] = wt;
        wt = st_glide(wt, 1.0f);
        M[i] = mx;
        V[i] = lv;
        S[i] = sp;
    }
    s->feedback = feedback;
    s->ratio = rt;
    s->spread = sp;
    s->mix = mx;
    s->level = lv;
    s->wet = wt;
    s->len0 = l0;
    s->len1 = l1;
    if (io->enabled)
        s->cleared = 0;
    float *p = io->io;
    for (int i = 0; i < n; i++) {
        xl[i] = p[2 * i];
        xr[i] = p[2 * i + 1];
    }
    channel_block(&s->l, xl, L0, F0, W, trailing, wl, n);
    channel_block(&s->r, xr, L1, F1, W, trailing, wr, n);
    for (int i = 0; i < n; i++) {
        float y[2];
        const float *x2[2] = { xl, xr }, *w2[2] = { wl, wr };
        for (int c = 0; c < 2; c++) {
            float x = x2[c][i], wet = w2[c][i], yy;
            if (M[i] < 0.5f)
                yy = st_fma(wet + wet, M[i], x);
            else if (trailing && !io->enabled)
                yy = wet + x;
            else
                yy = st_fma(x + x, 1.0f - M[i], wet);
            y[c] = yy * (V[i] + V[i]);
        }
        float a = 1.0f - S[i], b = S[i] + 1.0f;
        p[2 * i] = st_fma(b, y[0], a * y[1]) * 0.5f;
        p[2 * i + 1] = st_fma(b, y[1], a * y[0]) * 0.5f;
    }
    return 1;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len0 = dly_time_free(s->time0, s->sync0, io->beat_ms);
        s->len1 = s->len0 * s->ratio0;
        s->started = 1;
    }
    float mix = k[0] * 0.01f, fb = k[2] * 0.01f, ratio = k[3] * 0.01f, spread = k[4] * 0.01f;
    float level = k[5] * 0.01f, sync = k[6];
    if (ratio < 0.01f)
        ratio = 0.01f;
    float len0 = dly_time_free(k[1], sync, io->beat_ms), len1 = ratio * len0;
    int trail = (int)k[7];
    if (io->frames == 0)
        return;
    float d0 = (len0 - s->len0) * 0.00022669999452773482f;
    float d1 = (len1 - s->len1) * 0.00022669999452773482f;
    if ((io->enabled || trail == 1) &&
        block(s, io, mix, fb, ratio, spread, level, sync, len0, len1, d0, d1, !io->enabled))
        return;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->feedback = st_glide(s->feedback, fb);
        s->ratio = st_glide(s->ratio, ratio);
        s->spread = st_glide(s->spread, spread);
        s->mix = st_glide(s->mix, mix);
        s->level = st_glide(s->level, level);
        if (sync == 1.0f) {
            s->len0 = len0;
            s->len1 = len1;
        } else {
            s->len1 = s->len1 + d1;
            s->len0 = s->len0 + d0;
        }
        float xl = p[0], xr = p[1];
        int trailing;
        if (io->enabled) {
            s->cleared = 0;
            trailing = 0;
        } else if (trail == 1) {
            trailing = 1;
        } else {
            if (trail == 0 && !s->cleared)
                clear(s);
            continue;
        }
        float f = st_abs(s->feedback);
        if (s->ratio != 1.0f)
            f = st_fma(st_expf((s->ratio * s->ratio) * -4.0f), 1.0f - f, f);
        float fr = 0.6f > s->ratio ? __builtin_fminf(f, 0.5f) : f;                     /* VMINNM, VSELGT */
        float l = channel_run(s, &s->l, xl, s->len0, s->feedback, trailing, io->enabled);
        float r = channel_run(s, &s->r, xr, s->len1, fr, trailing, io->enabled);
        s->wet = st_glide(s->wet, 1.0f);
        float a = 1.0f - s->spread, b = s->spread + 1.0f;
        p[0] = st_fma(b, l, a * r) * 0.5f;
        p[1] = st_fma(b, r, a * l) * 0.5f;
    }
}

const struct wvfx_desc dly_bbd_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x104),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "BBD",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = DLY_BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
