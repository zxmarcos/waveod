/*
 * Phaser ST (MOD › Phaser): the original's Phaser ST (QME-200 V1.2.2, library entry 8.13, id 0x0400001B) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The left input, the result on both outputs. Single
 * precision.
 *
 * The LFO: the phase 2 pi r n wrapped to [0, 2 pi) and folded into a triangle over [-pi/2, pi/2], mapped to [0, 1]
 * (tri). The sweep: e = tan(C + B tan(A tri)) with A, B, C by Color, g = (e - 1) / (e + 1). The path: the input plus
 * 0.4 Color x the last output of stage 4 (the feedback), a fixed 1st-order high pass, four 1st-order allpasses
 * y = x1 + (x - y1) g; out = (stage 4 + the high pass) / 2. The rate and Color (0 or 1) glide 0.00022676 of the way
 * to the knobs each sample; the rate starts at 0. (The original also glides a copy of Rate it never reads.)
 */
#include "mod.h"

static const struct wvfx_param params[] = {
    { "Color", 0.0f, 1.0f, 1.0f, 0.0f, 0, 0x7b, 0x00, 0, 0, 0 },
    { "Rate", 0.10000000149011612f, 10.0f, 0.10000000149011612f, 0.5f, 1, 0x22, 0x12, 0, 0, 0 },
    { "Sync", 0.0f, 1.0f, 1.0f, 0.0f, 2, 0x00, 0x21, 0, 0, 0 },
};

static const struct fos HP = { 0.9971734881401062f, -0.9971734881401062f, -0.994347095489502f };

struct state {
    float color, rate;
    int n;
    struct fos_state hp;
    struct ap1_state ap[4];             /* x1, y1 of each allpass */
};

/* the phase folded into a triangle, -pi/2 .. pi/2 */
static float fold(float ph)
{
    ph = st_fma(__builtin_floorf(ph * 0.15915493667125702f), -6.2831854820251465f, ph);   /* VRINTM */
    float mirrored = 4.71238899230957f >= ph ? 3.1415927410125732f - ph : ph;
    if (ph > 1.5707963705062866f)
        ph = mirrored;
    float wrapped = ph + -6.2831854820251465f;
    return ph > 4.71238899230957f ? wrapped : ph;
}

static int init(void *state, void *buffer, const struct wvfx_env *env, const float *k)
{
    struct state *s = state;
    (void)buffer; (void)env;
    *s = (struct state){ 0 };
    s->color = k[0] > 0.5f ? 1.0f : 0.0f;
    return 0;
}

static void process(void *state, const struct wvfx_io *io)
{
    struct state *s = state;
    const float *k = io->knobs;
    float rate = mod_rate(k[1], k[2], io->beat_ms), color = k[0] > 0.5f ? 1.0f : 0.0f;
    if (!io->enabled || io->frames == 0)
        return;
    float dr = (rate - s->rate) * 0.0002267600066261366f, dc = (color - s->color) * 0.0002267600066261366f;
    int dark = k[0] < 0.5f;
    float A = dark ? 1.0499999523162842f : 1.149999976158142f;
    float B = dark ? 0.07123792916536331f : 0.08904740959405899f;
    float C = dark ? 0.014247586019337177f : 0.009260930120944977f;
    float *p = io->io;
    for (int i = 0; i < io->frames; i++, p += 2) {
        s->rate += dr;
        s->color += dc;
        float ph = (s->rate * 6.2831854820251465f) * (float)s->n, period = 1.0f / s->rate;
        float tri = st_fma(fold(ph), 0.31830987334251404f, 0.5f);
        s->n++;
        if (period <= (float)s->n)
            s->n = 0;
        float e = st_tanf(st_fma(st_tanf(tri * A), B, C));
        float x = st_fma(s->color * 0.4000000059604645f, s->ap[3].y1, p[0]);
        float h = fos_run(&HP, &s->hp, x), g = (e + -1.0f) / (e + 1.0f), y = h;
        for (int j = 0; j < 4; j++) {
            float out = st_fma(y - s->ap[j].y1, g, s->ap[j].x1);
            s->ap[j].x1 = y;
            s->ap[j].y1 = out;
            y = out;
        }
        y = (y + h) * 0.5f;
        p[0] = y;
        p[1] = y;
    }
}

const struct wvfx_desc mod_phaser_st_block = {
    .magic = WVFX_MAGIC,
    .abi = WVFX_ABI,
    .id = WVFX_ID(WVFX_MOD, 0x10D),
    .menu = WVFX_MOD,
    .nparams = sizeof params / sizeof params[0],
    .name = "Phaser ST",
    .category = "Phaser",
    .params = params,
    .state_size = sizeof(struct state),
    .buffer_size = 0,
    .init = init,
    .process = process,
    .poll = 0,
};
