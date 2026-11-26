/*
 * Rack (DELAY): the original's Rack delay (QME-200 V1.2.2, library entry 9.12, id 0x0B000014) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). A vintage digital rack delay: each input through a short
 * modulated line (257 samples, read 144 + 10 + 0.91 Mod m samples back, m a triangle ramping 9.07e-05 per sample,
 * the channels in opposite phase), plus Feedback x its line's last read (clipped to +-2), into a 4 s line read Time
 * back; the read through a 2nd-order low pass set by Tone (a bilinear design from tan), held every other sample and
 * quantized to 12 bits (4095 steps each side). Single precision.
 *
 * Out: below Mix 0.5, x + 1.6 Mix wet; from 0.5, 0.8 wet + 2 (1 - Mix) x (bypassed with Trail on: 0.8 wet + x).
 * Mix, Feedback, Tone and the time glide 0.00022676 of the way to the knobs each sample (with Sync on the time jumps;
 * the low pass is recomputed while Tone moves). Bypassed with Trail on the lines run on their feedback alone (the
 * modulation stands still); with Trail at 0 they are cleared once (the modulation restarted) and the signal passes.
 * The Init sets the low pass from the raw knob with its scaling folded otherwise (kept) and reads the tempo: here
 * the first process call does.
 */
#include "dly.h"

static const struct wvfx_param params[] = {
    { "Mix", 0.0f, 100.0f, 1.0f, 20.0f, 0, 0x15, 0x00, 0, 0, 0 },
    { "Feedback", 0.0f, 100.0f, 1.0f, 50.0f, 1, 0x15, 0x00, 0, 0, 0 },
    { "Time", 20.0f, 4000.0f, 1.0f, 500.0f, 2, 0x19, 0x15, 0, 0, 0 },
    { "Mod", 0.0f, 100.0f, 1.0f, 50.0f, 3, 0x15, 0x00, 0, 0, 0 },
    { "Tone", 0.0f, 100.0f, 1.0f, 50.0f, 4, 0x15, 0x00, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 5, 0x00, 0x21, 0, 0, 0 },
    { "Trail", 0.0f, 1.0f, 1.0f, 0.0f, 6, 0x00, 0x00, 0, 0, 0 },
};

#define MOD 257
#define BUFFER (DLY_BUFFER + 2 * MOD * (int)sizeof(float))

struct channel {
    struct dly_line line;
    float *mod;
    int mw;
    float m, sign;                      /* the modulation's triangle */
    float repeat;                       /* the line's last read */
    struct sos_state lp;
    float held;
};

struct state {
    float mix, feedback, tone, len, count;
    float time0, sync0;                 /* the Init's Time and Sync, for the first process call */
    int started, cleared;
    struct sos lp;
    struct channel l, r;
};

static void lowpass_set(struct sos *c, float w)
{
    float t = st_tanf(w), t2 = t * t, h = t2 * 0.7070000171661377f;
    float r = 1.0f / ((t + 0.7070000171661377f) + h), b0 = r * h;
    *c = (struct sos){ b0, b0 + b0, b0, 1.0f, r * st_fma(t2, 1.4140000343322754f, -1.4140000343322754f),
                       r * ((0.7070000171661377f - t) + h) };
}

/* the modulated line: its read 154 + 0.91 Mod m back (between two samples), x written after the read is placed */
static float mod_run(struct channel *c, float x, float depth)
{
    c->m = st_fma(c->sign, 9.070294618140906e-05f, c->m);
    if (c->m >= 1.0f) {
        c->sign = -1.0f;
        c->m = 1.0f;
    } else if (!(c->m > 0.0f)) {
        c->sign = 1.0f;
        c->m = 0.0f;
    }
    float d = st_fma(c->m, depth, 10.0f), whole = __builtin_truncf(d);              /* VRINTZ */
    int old = c->mw, i = (int)((float)(old - 144) - whole);
    if (i < 0)
        i += MOD;
    int older = i < 1 ? i + MOD - 1 : i - 1;
    c->mod[old] = x;
    c->mw = old > MOD - 2 ? 0 : old + 1;
    float a = c->mod[i];
    return st_fma(c->mod[older] - a, d - whole, a);
}

static void line_run(struct channel *c, float in, int whole, float frac)
{
    dly_write(&c->line, dly_clip2(in));
    c->repeat = dly_read(&c->line, whole, frac);
}

static float quantize(float x)
{
    float q = (float)(uint32_t)(int32_t)st_fma(st_abs(x), 4095.0f, 0.5f);
    return x >= 0.0f ? q * 0.00024420025874860585f : -(q * 0.00024420025874860585f);
}

static void clear(struct state *s)
{
    s->l.repeat = s->r.repeat = 0.0f;
    s->l.sign = 1.0f;
    s->r.sign = -1.0f;
    s->l.m = 0.0f;
    s->r.m = 1.0f;
    __builtin_memset(s->l.line.buf, 0, BUFFER);
    s->cleared = 1;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)env;
    *s = (struct state){ 0 };
    float *b = buffer;
    s->mix = k[0] * 0.01f;
    s->feedback = k[1] * 0.01f;
    s->tone = k[4] * 0.01f;
    s->time0 = k[2];
    s->sync0 = k[5];
    s->l.line.buf = b;
    s->r.line.buf = b + DLY_LINE;
    s->l.mod = b + 2 * DLY_LINE;
    s->r.mod = b + 2 * DLY_LINE + MOD;
    clear(s);
    lowpass_set(&s->lp, st_fma(k[4], 0.0028495171573013067f, 0.07123792916536331f));
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    if (!s->started) {
        s->len = dly_time(s->time0, s->sync0, io->beat_ms);
        s->started = 1;
    }
    float target = dly_time(k[2], k[5], io->beat_ms), tone = k[4] * 0.01f;
    if (io->frames == 0)
        return;
    float dm = (k[0] * 0.01f - s->mix) * 0.0002267600066261366f;
    float df = (k[1] * 0.01f - s->feedback) * 0.0002267600066261366f;
    float dl = (target - s->len) * 0.0002267600066261366f;
    float dt = (tone - s->tone) * 0.0002267600066261366f;
    float depth = k[3] * 0.9099999666213989f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        float xl = p[0], xr = p[1];
        float glided = s->len + dl;
        s->len = k[5] == 1.0f ? target : glided;
        if (s->tone != tone) {
            s->tone += dt;
            lowpass_set(&s->lp, st_fma(s->tone, 0.28495171666145325f, 0.07123792916536331f));
        }
        s->mix += dm;
        s->feedback += df;
        int whole = (int)s->len;
        float frac = s->len - __builtin_truncf(s->len);
        if (io->enabled) {
            float ml = mod_run(&s->l, xl, depth), mr = mod_run(&s->r, xr, depth);
            line_run(&s->l, st_fma(s->l.repeat, s->feedback, ml), whole, frac);
            line_run(&s->r, st_fma(s->r.repeat, s->feedback, mr), whole, frac);
            s->cleared = 0;
        } else if (k[6] == 1.0f) {
            line_run(&s->l, s->l.repeat * s->feedback, whole, frac);
            line_run(&s->r, s->r.repeat * s->feedback, whole, frac);
        } else {
            if (k[6] == 0.0f && !s->cleared)
                clear(s);
            continue;
        }
        float yl = sos_run(&s->lp, &s->l.lp, s->l.repeat), yr = sos_run(&s->lp, &s->r.lp, s->r.repeat);
        if (s->count >= 2.0f) {
            s->count = 0.0f;
            s->l.held = yl;
            s->r.held = yr;
        }
        s->count += 1.0f;
        float l = quantize(s->l.held), r = quantize(s->r.held);
        if (s->mix < 0.5f) {
            float m = s->mix * 1.600000023841858f;
            p[0] = st_fma(m, l, xl);
            p[1] = st_fma(m, r, xr);
        } else if (!io->enabled && (int)k[6] == 1) {
            p[0] = l * 0.800000011920929f + xl;
            p[1] = r * 0.800000011920929f + xr;
        } else {
            float m = 1.0f - s->mix;
            p[0] = st_fma(xl + xl, m, l * 0.800000011920929f);
            p[1] = st_fma(xr + xr, m, r * 0.800000011920929f);
        }
    }
}

const struct wvfx_desc dly_rack_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_DLY, 0x10C),
    .menu = WVFX_DLY,
    .nparams = sizeof params / sizeof params[0],
    .name = "Rack",
    .category = "Delay",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = BUFFER,
    .init = init,
    .process = process,
    .poll = 0,
};
