#include "engine/modulators.h"
#include "platform/audio.h"
#include "platform/sdram.h"
#include "sdk/wvfx.h"
#include <string.h>

#define TWO_PI 6.28318531f

/* beats of the note divisions (model/param_format.c param_sync_divisions: 1/1 .. 1/16) */
static const float division_beats[11] = {
    4.0f, 2.0f, 3.0f, 4.0f / 3.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f,
};

struct mod_set {
    struct preset_mods mods;
    struct mod_route route[MOD_ROUTES];
    int n;
};

static struct {
    struct mod_set set[2];
    volatile uint8_t cur;               /* the set the interrupt reads; the main loop writes the other */
    volatile uint8_t restart;
    struct mod_state st[MOD_COUNT];
    float value[MOD_COUNT];
} md SDRAM_BSS;                          /* a block reads ~1 KB of it, cached */

/* cos 2 pi x, x any (error < 2e-5) */
static float cos_turns(float x)
{
    x -= (float)(int)x;
    if (x < 0.0f)
        x += 1.0f;
    float s = 1.0f;                     /* fold to [0, 0.25]: cos is even and cos(pi - a) = -cos a */
    if (x > 0.5f)
        x = 1.0f - x;
    if (x > 0.25f) {
        x = 0.5f - x;
        s = -1.0f;
    }
    float a = x * TWO_PI, a2 = a * a;
    return s * (1.0f - a2 * (0.5f - a2 * (1.0f / 24.0f - a2 * (1.0f / 720.0f - a2 * (1.0f / 40320.0f)))));
}

static float random01(uint32_t *seed)
{
    uint32_t x = *seed ? *seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/* samples of an LFO cycle or a SEQ step */
static float period(const struct preset_mod *m, uint32_t beat_ms)
{
    if (m->sync >= 1 && m->sync <= 11) {
        float ms = (float)(beat_ms & 0xffffu) * division_beats[m->sync - 1];
        return ms > 1.0f ? ms * (float)AUDIO_RATE / 1000.0f : (float)AUDIO_RATE;
    }
    float hz = wvfx_clamp(m->rate, 0.01f, 50.0f);
    return (float)AUDIO_RATE / hz;
}

void mod_reset(struct mod_state *s, const struct preset_mod *m)
{
    uint32_t seed = s->seed ? s->seed : 0x1234567u;
    memset(s, 0, sizeof *s);
    s->seed = seed;
    s->phase = m->type == MOD_LFO ? (float)(m->phase % 100) * 0.01f : 0.0f;
    s->from = random01(&s->seed);
    s->to = random01(&s->seed);
}

static float lfo(struct mod_state *s, const struct preset_mod *m)
{
    float p = s->phase;
    switch (m->shape) {
    case MS_TRIANGLE: return p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
    case MS_SQUARE:   return p < 0.5f ? 1.0f : 0.0f;
    case MS_SAW_UP:   return p;
    case MS_SAW_DOWN: return 1.0f - p;
    case MS_RANDOM:   return s->to;
    case MS_SMOOTH:   return s->from + (s->to - s->from) * (0.5f - 0.5f * cos_turns(0.5f * p));
    default:          return 0.5f - 0.5f * cos_turns(p);        /* a sine from its bottom */
    }
}

static float seq(const struct mod_state *s, const struct preset_mod *m)
{
    int n = m->steps >= 1 && m->steps <= MOD_STEPS ? m->steps : MOD_STEPS;
    int i = (int)(s->cycle % (uint32_t)n), prev = (i + n - 1) % n;
    float v = (float)(m->step[i] > 100 ? 100 : m->step[i]) * 0.01f;
    float g = (float)(m->glide > 100 ? 100 : m->glide) * 0.01f;
    if (g > 0.0f && s->phase < g && s->cycle > 0) {
        float u = (float)(m->step[prev] > 100 ? 100 : m->step[prev]) * 0.01f;
        return u + (v - u) * (s->phase / g);
    }
    return v;
}

float mod_step(struct mod_state *s, const struct preset_mod *m, const float *in, int frames, uint32_t beat_ms)
{
    if (m->type == MOD_ENV) {
        float peak = 0.0f;
        for (int k = 0; in && k < 2 * frames; k++) {
            float a = in[k] < 0 ? -in[k] : in[k];
            if (a > peak)
                peak = a;
        }
        float ms = peak > s->env ? (float)m->attack : (float)m->release;
        float c = ms > 0.0f ? wvfx_exp2(-1442.69504f * (float)frames / (ms * (float)AUDIO_RATE)) : 0.0f;
        s->env = peak + (s->env - peak) * c;
        float db = wvfx_to_db(s->env) + (float)m->sens;
        s->value = wvfx_clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        return s->value;
    }
    if (m->type == MOD_LFO)
        s->value = lfo(s, m);
    else if (m->type == MOD_SEQ)
        s->value = seq(s, m);
    else
        s->value = 0.0f;
    s->phase += (float)frames / period(m, beat_ms);
    while (s->phase >= 1.0f) {
        s->phase -= 1.0f;
        s->cycle++;
        s->from = s->to;
        s->to = random01(&s->seed);
    }
    return s->value;
}

static float curve(int c, float x)
{
    if (c == CC_SLOW)
        return x * x;
    if (c == CC_FAST)
        return 1.0f - (1.0f - x) * (1.0f - x);
    return x;
}

void mods_set(const struct preset_mods *mods, const struct mod_route *routes, int n)
{
    struct mod_set *s = &md.set[!md.cur];
    s->mods = *mods;
    s->n = n < 0 ? 0 : n > MOD_ROUTES ? MOD_ROUTES : n;
    memcpy(s->route, routes, sizeof(struct mod_route) * (unsigned)s->n);
    __asm__ volatile("" ::: "memory");
    md.cur = (uint8_t)!md.cur;
}

void mods_restart(void)
{
    md.restart = 1;
}

float mods_value(int i)
{
    return i >= 0 && i < MOD_COUNT ? md.value[i] : 0.0f;
}

void mods_run(struct chain *c, const float *in, int frames, uint32_t beat_ms)
{
    const struct mod_set *s = &md.set[md.cur];
    if (md.restart) {
        for (int i = 0; i < MOD_COUNT; i++)
            mod_reset(&md.st[i], &s->mods.m[i]);
        md.restart = 0;
    }
    for (int i = 0; i < MOD_COUNT; i++)
        md.value[i] = s->mods.m[i].type == MOD_OFF ? 0.0f : mod_step(&md.st[i], &s->mods.m[i], in, frames, beat_ms);
    if (!c)
        return;
    for (int k = 0; k < s->n; k++) {
        const struct mod_route *r = &s->route[k];
        if (r->slot >= PRESET_SLOTS || r->knob >= PRESET_KNOBS || r->mod >= MOD_COUNT)
            continue;
        float v = r->off + (r->on - r->off) * curve(r->curve, md.value[r->mod]);
        if (r->step > 0.0f)
            v = r->min + (float)(int)((v - r->min) / r->step + 0.5f) * r->step;
        c->slot[r->slot].knobs[r->knob] = wvfx_clamp(v, r->min, r->max);
    }
}
