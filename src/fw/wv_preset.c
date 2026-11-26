#include "fw/wv_preset.h"
#include "fw/bytes.h"
#include "platform/sdram.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define VERSION 1

enum { T_NAME = 1, T_STYLE, T_MAIN, T_SLOT, T_QK, T_EXP, T_CTRL, T_FS, T_ROUTE, T_SCENES, T_CONTROLS, T_MODS };
#define MOD_BYTES 32                        /* a modulator in T_MODS */

struct writer {
    uint8_t *p;
    unsigned n, max;
    int full;
};

static void put(struct writer *w, const void *data, unsigned len)
{
    if (w->n + len > w->max) {
        w->full = 1;
        return;
    }
    memcpy(w->p + w->n, data, len);
    w->n += len;
}

static void put8(struct writer *w, int v)
{
    uint8_t b = (uint8_t)v;
    put(w, &b, 1);
}

static void put16(struct writer *w, int v)
{
    put8(w, v);
    put8(w, v >> 8);
}

static void put32(struct writer *w, uint32_t v)
{
    put16(w, (int)(v & 0xffff));
    put16(w, (int)(v >> 16));
}

static void putf(struct writer *w, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    put32(w, u);
}

/* a chunk: its tag and a length patched in once the body is written */
static unsigned open_chunk(struct writer *w, int tag)
{
    put8(w, tag);
    put16(w, 0);
    return w->n;
}

static void close_chunk(struct writer *w, unsigned body)
{
    if (w->full)
        return;
    unsigned len = w->n - body;
    w->p[body - 2] = (uint8_t)len;
    w->p[body - 1] = (uint8_t)(len >> 8);
}

static void text_chunk(struct writer *w, int tag, const char *s, unsigned max)
{
    unsigned n = 0;
    while (n < max && s[n])
        n++;
    unsigned b = open_chunk(w, tag);
    put(w, s, n);
    close_chunk(w, b);
}

static int controls_equal(const struct preset_controls *a, const struct preset_controls *b)
{
    if (a->count != b->count || memcmp(a->colour, b->colour, sizeof a->colour) != 0)
        return 0;
    for (int k = 0; k < CK_KEYS; k++)
        if (strcmp(a->label[k], b->label[k]) != 0)
            return 0;
    for (int i = 0; i < a->count; i++) {
        const struct control *x = &a->c[i], *y = &b->c[i];
        if (x->key != y->key || x->modes != y->modes || x->when != y->when || x->action != y->action ||
            x->slot != y->slot || x->arg != y->arg || x->curve != y->curve || x->flags != y->flags ||
            ((x->action == CA_PARAM || x->action == CA_MIDI) && (x->off != y->off || x->on != y->on)))
            return 0;
    }
    return 1;
}

/* count, then {key, modes, when, action, slot, arg[, curve, flags for a pedal, a MIDI key or a MIDI out link][, f32
   off, f32 on for a parameter or MIDI out link]} per link, then {colour, label length, label} per key */
static void encode_controls(struct writer *w, const struct preset_controls *c)
{
    unsigned b = open_chunk(w, T_CONTROLS);
    put8(w, c->count);
    for (int i = 0; i < c->count; i++) {
        const struct control *l = &c->c[i];
        put8(w, l->key);
        put8(w, l->modes);
        put8(w, l->when);
        put8(w, l->action);
        put8(w, l->slot);
        put8(w, l->arg);
        if (CK_SWEEPS(l->key) || l->action == CA_MIDI) {
            put8(w, l->curve);
            put8(w, l->flags);
        }
        if (l->action == CA_PARAM || l->action == CA_MIDI) {
            putf(w, l->off);
            putf(w, l->on);
        }
    }
    for (int k = 0; k < CK_KEYS; k++) {
        unsigned len = 0;
        while (len < CONTROL_LABEL && c->label[k][len])
            len++;
        put8(w, c->colour[k]);
        put8(w, (int)len);
        put(w, c->label[k], len);
    }
    close_chunk(w, b);
}

int wvp_encode_controls(const struct preset_controls *c, uint8_t *out, unsigned max)
{
    struct writer w = { out, 0, max, 0 };
    encode_controls(&w, c);
    return w.full ? -1 : (int)w.n;
}

int wvp_encode(const struct preset *p, uint8_t *out, unsigned max)
{
    struct writer w = { out, 0, max, 0 };
    put8(&w, 'W');
    put8(&w, 'P');
    put8(&w, VERSION);
    put8(&w, 0);
    text_chunk(&w, T_NAME, p->name, PRESET_NAME_LEN);
    text_chunk(&w, T_STYLE, p->style, 10);

    unsigned b = open_chunk(&w, T_MAIN);
    put8(&w, p->volume);
    put8(&w, p->pan);
    put16(&w, p->bpm);
    put8(&w, p->fx_loop);
    put8(&w, p->exp_group);
    put8(&w, p->count);
    put(&w, p->order, PRESET_SLOTS);
    close_chunk(&w, b);

    for (int i = 0; i < PRESET_SLOTS; i++) {
        const struct preset_slot *s = &p->slot[i];
        if (s->menu < 0)
            continue;
        b = open_chunk(&w, T_SLOT);
        put8(&w, i);
        put8(&w, s->on);
        put8(&w, s->menu);
        put16(&w, s->index);
        put32(&w, s->id);
        put8(&w, PRESET_KNOBS);
        for (int k = 0; k < PRESET_KNOBS; k++)
            putf(&w, s->knobs[k]);
        close_chunk(&w, b);
    }

    b = open_chunk(&w, T_QK);
    put(&w, p->qk_target, QUICK_KNOBS);
    put(&w, p->qk_param, QUICK_KNOBS);
    close_chunk(&w, b);

    b = open_chunk(&w, T_EXP);
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            const struct exp_target *x = &p->exp[g][t];
            put8(&w, x->slot);
            put8(&w, x->param);
            putf(&w, x->min);
            putf(&w, x->max);
        }
    close_chunk(&w, b);

    /* the footswitches: the original's functions and CTRL groups while the table is what they give, else the
       table (fw/preset_controls.c) */
    static struct preset_controls legacy SDRAM_BSS;
    preset_controls_legacy(p, &legacy);
    if (controls_equal(&legacy, &p->controls)) {
        b = open_chunk(&w, T_CTRL);
        put(&w, p->ctrl, sizeof p->ctrl);
        close_chunk(&w, b);

        b = open_chunk(&w, T_FS);
        put(&w, p->fs, sizeof p->fs);
        close_chunk(&w, b);
    } else {
        encode_controls(&w, &p->controls);
    }

    const struct preset_route *r = &p->route;
    b = open_chunk(&w, T_ROUTE);
    put8(&w, r->stored);
    put8(&w, r->mode);
    put8(&w, r->split);
    put8(&w, r->merge);
    put(&w, r->cell, PRESET_SLOTS);
    put(&w, r->joint, JOINT_PARAMS);
    close_chunk(&w, b);

    /* scenes: active, count, {u16 on, u8 name length, name} per scene, then the parameters
       {slot, knob, f32 value per scene}; left out while they are the plain blocks of the preset */
    if (preset_scenes_used(p)) {
        const struct preset_scenes *sc = &p->scenes;
        b = open_chunk(&w, T_SCENES);
        put8(&w, sc->active);
        put8(&w, SCENE_COUNT);
        for (int n = 0; n < SCENE_COUNT; n++) {
            unsigned len = 0;
            while (len < SCENE_NAME_LEN && sc->name[n][len])
                len++;
            put16(&w, sc->on[n]);
            put8(&w, (int)len);
            put(&w, sc->name[n], len);
        }
        put8(&w, sc->nparams);
        for (int i = 0; i < sc->nparams; i++) {
            put8(&w, sc->param[i].slot);
            put8(&w, sc->param[i].knob);
            for (int n = 0; n < SCENE_COUNT; n++)
                putf(&w, sc->param[i].value[n]);
        }
        close_chunk(&w, b);
    }

    /* modulators: {u8 index, type, shape, sync, f32 rate, u8 steps, phase, glide, i8 sens, u16 attack, release,
       u8 step[16]} per one in use */
    if (preset_mods_used(&p->mods)) {
        b = open_chunk(&w, T_MODS);
        for (int i = 0; i < MOD_COUNT; i++) {
            const struct preset_mod *m = &p->mods.m[i];
            if (m->type == MOD_OFF)
                continue;
            put8(&w, i);
            put8(&w, m->type);
            put8(&w, m->shape);
            put8(&w, m->sync);
            putf(&w, m->rate);
            put8(&w, m->steps);
            put8(&w, m->phase);
            put8(&w, m->glide);
            put8(&w, (uint8_t)m->sens);
            put16(&w, m->attack);
            put16(&w, m->release);
            put(&w, m->step, MOD_STEPS);
        }
        close_chunk(&w, b);
    }

    return w.full ? -1 : (int)w.n;
}

/* ---------------------------------------------------------------- decoding */

static int header_ok(const uint8_t *in, unsigned len)
{
    return in && len >= 4 && in[0] == 'W' && in[1] == 'P' && in[2] >= 1;
}

static void copy_text(char *dst, unsigned room, const uint8_t *src, unsigned len)
{
    unsigned n = len < room ? len : room;
    memcpy(dst, src, n);
    dst[n] = 0;
}

struct chunk {
    unsigned at;                /* where the next one starts */
    int tag;
    unsigned n;
    const uint8_t *b;
};

/* the next chunk; 0 at the end or at one that runs past it */
static int next_chunk(const uint8_t *in, unsigned len, struct chunk *c)
{
    if (c->at < 4)
        c->at = 4;
    if (c->at + 3 > len)
        return 0;
    c->tag = in[c->at];
    c->n = (unsigned)(in[c->at + 1] | in[c->at + 2] << 8);
    c->b = in + c->at + 3;
    if (c->at + 3 + c->n > len)
        return 0;
    c->at += 3 + c->n;
    return 1;
}

/* the T_SCENES chunk; 0 = malformed (the preset then has no scenes) */
static int decode_scenes(const uint8_t *b, unsigned n, struct preset_scenes *sc)
{
    memset(sc, 0, sizeof *sc);
    if (n < 2)
        return 0;
    unsigned count = b[1], at = 2;
    sc->active = b[0];
    for (unsigned s = 0; s < count; s++) {
        if (at + 3 > n)
            return 0;
        unsigned len = b[at + 2];
        if (at + 3 + len > n)
            return 0;
        if (s < SCENE_COUNT) {
            sc->on[s] = rd16(b + at);
            copy_text(sc->name[s], SCENE_NAME_LEN, b + at + 3, len);
        }
        at += 3 + len;
    }
    for (unsigned s = count; s < SCENE_COUNT; s++)       /* fewer stored: the rest like the first */
        sc->on[s] = sc->on[0];
    if (sc->active >= SCENE_COUNT)
        sc->active = 0;
    if (at >= n || !count)
        return 1;
    unsigned np = b[at++];
    for (unsigned i = 0; i < np && at + 2 + 4 * count <= n; i++, at += 2 + 4 * count) {
        if (b[at] >= PRESET_SLOTS || b[at + 1] >= PRESET_KNOBS || sc->nparams >= SCENE_PARAMS)
            continue;
        struct scene_param *sp = &sc->param[sc->nparams++];
        sp->slot = (int8_t)b[at];
        sp->knob = (int8_t)b[at + 1];
        for (unsigned s = 0; s < SCENE_COUNT; s++)
            sp->value[s] = rdf32(b + at + 2 + 4 * (s < count ? s : 0));
    }
    return 1;
}

/* the T_CONTROLS chunk; 0 = malformed */
static int decode_controls(const uint8_t *b, unsigned n, struct preset_controls *c)
{
    memset(c, 0, sizeof *c);
    if (n < 1)
        return 0;
    unsigned count = b[0], at = 1;
    for (unsigned i = 0; i < count; i++) {
        if (at + 6 > n)
            return 0;
        struct control l = { .key = b[at], .modes = b[at + 1], .when = b[at + 2], .action = b[at + 3],
                             .slot = (int8_t)b[at + 4], .arg = b[at + 5] };
        at += 6;
        if (CK_SWEEPS(l.key) || l.action == CA_MIDI) {
            if (at + 2 > n)
                return 0;
            l.curve = b[at] < CC_COUNT ? b[at] : CC_LINEAR;
            l.flags = b[at + 1];
            at += 2;
        }
        if (l.action == CA_PARAM || l.action == CA_MIDI) {
            if (at + 8 > n)
                return 0;
            l.off = rdf32(b + at);
            l.on = rdf32(b + at + 4);
            at += 8;
        }
        int midi_ok = l.action == CA_MIDI && l.slot >= 0 && l.slot < 16 && l.arg < 128 && l.off >= 0.0f &&
                      l.off <= 127.0f && l.on >= 0.0f && l.on <= 127.0f;
        if (CK_VALID(l.key) && l.action > CA_NONE && l.action < CA_COUNT && l.when <= CW_MOMENT &&
            (l.action == CA_FN || l.action == CA_SCENE || l.action == CA_NOTHING || midi_ok ||
             (l.action != CA_MIDI && l.slot >= 0 && l.slot < PRESET_SLOTS)) &&
            (l.action != CA_PARAM || (l.flags & CF_ORDER) || l.arg < PRESET_KNOBS))
            preset_control_add(c, &l);
    }
    for (int k = 0; k < CK_KEYS && at + 2 <= n; k++) {
        unsigned len = b[at + 1];
        if (at + 2 + len > n)
            break;
        c->colour[k] = b[at];
        copy_text(c->label[k], CONTROL_LABEL, b + at + 2, len);
        at += 2 + len;
    }
    return 1;
}

int wvp_decode_controls(const uint8_t *in, unsigned len, struct preset_controls *out)
{
    memset(out, 0, sizeof *out);
    if (len < 3 || in[0] != T_CONTROLS || 3u + rd16(in + 1) > len)
        return -1;
    return decode_controls(in + 3, rd16(in + 1), out) ? 0 : -1;
}

int wvp_decode(const uint8_t *in, unsigned len, struct preset *out)
{
    preset_clear(out, "");
    if (!header_ok(in, len))
        return -1;
    struct chunk c = { 0 };
    int scenes = 0, controls = 0;
    while (next_chunk(in, len, &c)) {
        const uint8_t *b = c.b;
        unsigned n = c.n;
        switch (c.tag) {
        case T_NAME:
            copy_text(out->name, PRESET_NAME_LEN, b, n);
            break;
        case T_STYLE:
            copy_text(out->style, 10, b, n);
            break;
        case T_MAIN:
            if (n < 7 + PRESET_SLOTS)
                break;
            out->volume = b[0];
            out->pan = (int8_t)b[1];
            out->bpm = rd16(b + 2);
            out->fx_loop = b[4];
            out->exp_group = b[5];
            out->count = b[6] <= PRESET_SLOTS ? b[6] : 0;
            memcpy(out->order, b + 7, PRESET_SLOTS);
            break;
        case T_SLOT: {
            if (n < 10 || b[0] >= PRESET_SLOTS)
                break;
            struct preset_slot *s = &out->slot[b[0]];
            s->on = b[1];
            s->menu = (int8_t)b[2];
            s->index = rds16(b + 3);
            s->id = rd32(b + 5);
            unsigned k = b[9];
            for (unsigned i = 0; i < k && i < PRESET_KNOBS && 10 + 4 * i + 4 <= n; i++)
                s->knobs[i] = rdf32(b + 10 + 4 * i);
            break;
        }
        case T_QK:
            if (n >= 2 * QUICK_KNOBS) {
                memcpy(out->qk_target, b, QUICK_KNOBS);
                memcpy(out->qk_param, b + QUICK_KNOBS, QUICK_KNOBS);
            }
            break;
        case T_EXP:
            if (n < 10u * EXP_GROUPS * EXP_TARGETS)
                break;
            for (int g = 0; g < EXP_GROUPS; g++)
                for (int t = 0; t < EXP_TARGETS; t++) {
                    const uint8_t *e = b + 10 * (g * EXP_TARGETS + t);
                    struct exp_target *x = &out->exp[g][t];
                    x->slot = (int8_t)e[0];
                    x->param = (int8_t)e[1];
                    x->min = rdf32(e + 2);
                    x->max = rdf32(e + 6);
                }
            break;
        case T_CTRL:
            if (n >= sizeof out->ctrl)
                memcpy(out->ctrl, b, sizeof out->ctrl);
            break;
        case T_FS:
            if (n >= sizeof out->fs)
                memcpy(out->fs, b, sizeof out->fs);
            break;
        case T_ROUTE: {
            struct preset_route *r = &out->route;
            if (n < 4 + PRESET_SLOTS + JOINT_PARAMS || b[1] >= ROUTE_MODES)
                break;
            r->stored = b[0];
            r->mode = b[1];
            r->split = b[2];
            r->merge = b[3];
            memcpy(r->cell, b + 4, PRESET_SLOTS);
            memcpy(r->joint, b + 4 + PRESET_SLOTS, JOINT_PARAMS);
            break;
        }
        case T_SCENES:
            scenes = decode_scenes(b, n, &out->scenes);
            break;
        case T_CONTROLS:
            controls = decode_controls(b, n, &out->controls);
            break;
        case T_MODS:
            for (unsigned at = 0; at + MOD_BYTES <= n; at += MOD_BYTES) {
                const uint8_t *e = b + at;
                if (e[0] >= MOD_COUNT || e[1] >= MOD_TYPES)
                    continue;
                struct preset_mod *m = &out->mods.m[e[0]];
                m->type = e[1];
                m->shape = e[2] < MS_SHAPES ? e[2] : MS_SINE;
                m->sync = e[3] <= 11 ? e[3] : 0;
                m->rate = rdf32(e + 4);
                m->steps = e[8] >= 1 && e[8] <= MOD_STEPS ? e[8] : MOD_STEPS;
                m->phase = e[9];
                m->glide = e[10];
                m->sens = (int8_t)e[11];
                m->attack = rd16(e + 12);
                m->release = rd16(e + 14);
                memcpy(m->step, e + 16, MOD_STEPS);
            }
            break;
        default:
            break;                          /* a later version's */
        }
    }
    for (int i = out->count; i < PRESET_SLOTS; i++)
        out->order[i] = -1;
    if (!controls)                          /* the original's footswitch functions and CTRL groups */
        preset_controls_legacy(out, &out->controls);
    if (!scenes) {
        preset_scenes_reset(out);
    } else {
        /* the slots are what plays: the active scene follows them */
        struct preset_scenes *sc = &out->scenes;
        uint16_t on = 0;
        for (int i = 0; i < PRESET_SLOTS; i++)
            if (out->slot[i].menu >= 0 && out->slot[i].on)
                on |= (uint16_t)(1u << i);
        sc->on[sc->active] = on;
        for (int i = 0; i < sc->nparams; i++)
            sc->param[i].value[sc->active] = out->slot[sc->param[i].slot].knobs[sc->param[i].knob];
    }
    return 0;
}

int wvp_name(const uint8_t *in, unsigned len, char out[PRESET_NAME_LEN + 1])
{
    out[0] = 0;
    if (!header_ok(in, len))
        return -1;
    struct chunk c = { 0 };
    while (next_chunk(in, len, &c))
        if (c.tag == T_NAME) {
            copy_text(out, PRESET_NAME_LEN, c.b, c.n);
            break;
        }
    return 0;
}
