#include "fw/preset_format.h"
#include "fw/bytes.h"
#include <string.h>

#define PRESET_MAGIC 0x5AA5u

static void copy_text(char *dst, const uint8_t *src, unsigned len)
{
    unsigned i = 0;
    for (; i < len && src[i] && src[i] != 0xff; i++)
        dst[i] = (char)src[i];
    while (i > 0 && dst[i - 1] == ' ')
        i--;
    dst[i] = 0;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void putf32(uint8_t *p, float v)
{
    memcpy(p, &v, 4);
}

/* a record that was never stored: the original's template (no targets, factory footswitch functions) */
static void blank_record(uint8_t *rec)
{
    memcpy(rec, preset_template, PRESET_RECORD_SIZE);
}

/* the routing in the CTRL 1 display bytes (cells) and CTRL 2 display bytes (magic, mode, joints, check) */
#define ROUTE_CELLS_AT (0x3d4 + 12)
#define ROUTE_HEAD_AT  (0x3d4 + 0x18 + 12)
#define ROUTE_MAGIC0   0x57                 /* "WR" */
#define ROUTE_MAGIC1   0x52

static uint8_t route_check(const uint8_t *rec)
{
    uint8_t sum = 0xA5;
    for (int i = 0; i < 12; i++)
        sum = (uint8_t)(sum * 31 + rec[ROUTE_CELLS_AT + i]);
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(sum * 31 + rec[ROUTE_HEAD_AT + i]);
    return sum;
}

static void decode_route(const uint8_t *rec, struct preset_route *r)
{
    const uint8_t *h = rec + ROUTE_HEAD_AT;
    memset(r, 0, sizeof *r);
    if (h[0] != ROUTE_MAGIC0 || h[1] != ROUTE_MAGIC1 || h[11] != route_check(rec) || h[2] >= ROUTE_MODES ||
        h[3] >= ROUTE_COLS || h[4] <= h[3] || h[4] > ROUTE_COLS)
        return;
    r->stored = 1;
    r->mode = h[2];
    r->split = h[3];
    r->merge = h[4];
    for (int i = 0; i < PRESET_SLOTS; i++) {
        uint8_t c = rec[ROUTE_CELLS_AT + i];
        r->cell[i] = c < ROUTE_CELLS ? (int8_t)c : -1;
    }
    for (int k = 0; k < JOINT_PARAMS; k++)
        r->joint[k] = h[5 + k] > 100 ? 100 : h[5 + k];
}

static void encode_route(const struct preset_route *r, uint8_t *out)
{
    uint8_t *h = out + ROUTE_HEAD_AT;
    for (int i = 0; i < PRESET_SLOTS; i++)
        out[ROUTE_CELLS_AT + i] = r->cell[i] >= 0 ? (uint8_t)r->cell[i] : 0xff;
    h[0] = ROUTE_MAGIC0;
    h[1] = ROUTE_MAGIC1;
    h[2] = r->mode;
    h[3] = r->split;
    h[4] = r->merge;
    for (int k = 0; k < JOINT_PARAMS; k++)
        h[5 + k] = r->joint[k];
    h[11] = route_check(out);
}

static void decode_assign(const uint8_t *rec, struct preset *out)
{
    for (int i = 0; i < QUICK_KNOBS; i++) {
        out->qk_target[i] = (int8_t)rec[0x360 + i];
        out->qk_param[i] = (int8_t)rec[0x363 + i];
    }
    out->fx_loop = rec[0x366];
    out->exp_group = rec[0x367];
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            const uint8_t *e = rec + 0x368 + 12 * (g * EXP_TARGETS + t);
            struct exp_target *x = &out->exp[g][t];
            x->slot = (int8_t)e[0];
            x->param = (int8_t)e[1];
            x->min = rdf32(e + 4);
            x->max = rdf32(e + 8);
        }
    for (int c = 0; c < CTRL_COUNT; c++)
        memcpy(out->ctrl[c], rec + 0x3d4 + 0x18 * c, PRESET_SLOTS);
    memcpy(out->fs, rec + 0x44d, sizeof out->fs);
    decode_route(rec, &out->route);
}

static void encode_assign(const struct preset *p, uint8_t *out)
{
    for (int i = 0; i < QUICK_KNOBS; i++) {
        out[0x360 + i] = (uint8_t)p->qk_target[i];
        out[0x363 + i] = (uint8_t)p->qk_param[i];
    }
    out[0x366] = p->fx_loop;
    out[0x367] = p->exp_group;
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            uint8_t *e = out + 0x368 + 12 * (g * EXP_TARGETS + t);
            const struct exp_target *x = &p->exp[g][t];
            e[0] = (uint8_t)x->slot;
            e[1] = (uint8_t)x->param;
            putf32(e + 4, x->min);
            putf32(e + 8, x->max);
        }
    for (int c = 0; c < CTRL_COUNT; c++)
        memcpy(out + 0x3d4 + 0x18 * c, p->ctrl[c], PRESET_SLOTS);
    memcpy(out + 0x44d, p->fs, sizeof p->fs);
    /* the original's bytes stay as they were while the chain is the plain series of `order` */
    struct preset_route def;
    preset_route_default(p, &def);
    def.stored = p->route.stored;
    if (p->route.stored || memcmp(&def, &p->route, sizeof def) != 0)
        encode_route(&p->route, out);
}

void preset_route_default(const struct preset *p, struct preset_route *r)
{
    static const uint8_t joints[JOINT_PARAMS] = { 50, 100, 100, 50, 50, 50 };
    memset(r, 0, sizeof *r);
    r->mode = ROUTE_SERIAL;
    r->split = 1;                           /* the design's: PRE 1, lane A 3, POST 2 */
    r->merge = 4;
    memcpy(r->joint, joints, sizeof joints);
    for (int i = 0; i < PRESET_SLOTS; i++)
        r->cell[i] = -1;
    for (int pos = 0; pos < p->count; pos++)
        if (p->order[pos] >= 0 && p->order[pos] < PRESET_SLOTS)
            r->cell[p->order[pos]] = (int8_t)pos;
}

void preset_unassign_slot(struct preset *p, int slot)
{
    for (int c = 0; c < CTRL_COUNT; c++)
        if (p->ctrl[c][slot] == 1)
            p->ctrl[c][slot] = 0;
    for (int i = 0; i < QUICK_KNOBS; i++)
        if (p->qk_target[i] == slot) {
            p->qk_target[i] = -1;
            p->qk_param[i] = 0;
        }
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            struct exp_target *x = &p->exp[g][t];
            if (x->slot == slot) {
                x->slot = -1;
                x->param = 0;
                x->min = 0.0f;
                x->max = 100.0f;
            }
        }
    preset_scene_forget(p, slot);
    preset_controls_forget(p, slot, 0);
}

void preset_scenes_reset(struct preset *p)
{
    struct preset_scenes *sc = &p->scenes;
    memset(sc, 0, sizeof *sc);
    uint16_t on = 0;
    for (int i = 0; i < PRESET_SLOTS; i++)
        if (p->slot[i].menu >= 0 && p->slot[i].on)
            on |= (uint16_t)(1u << i);
    for (int n = 0; n < SCENE_COUNT; n++)
        sc->on[n] = on;
}

int preset_scene_param(const struct preset *p, int slot, int knob)
{
    for (int i = 0; i < p->scenes.nparams; i++)
        if (p->scenes.param[i].slot == slot && p->scenes.param[i].knob == knob)
            return i;
    return -1;
}

void preset_scene_forget(struct preset *p, int slot)
{
    struct preset_scenes *sc = &p->scenes;
    int n = 0;
    for (int i = 0; i < sc->nparams; i++)
        if (sc->param[i].slot != slot)
            sc->param[n++] = sc->param[i];
    sc->nparams = (uint8_t)n;
}

int preset_scenes_used(const struct preset *p)
{
    const struct preset_scenes *sc = &p->scenes;
    if (sc->active || sc->nparams)
        return 1;
    for (int n = 0; n < SCENE_COUNT; n++)
        if (sc->name[n][0] || sc->on[n] != sc->on[0])
            return 1;
    return 0;
}

void preset_mod_default(struct preset_mod *m, int type)
{
    memset(m, 0, sizeof *m);
    m->type = (uint8_t)type;
    m->shape = MS_SINE;
    m->rate = 1.0f;
    m->steps = 8;
    m->attack = 10;
    m->release = 200;
    for (int i = 0; i < MOD_STEPS; i++)
        m->step[i] = (uint8_t)(i % 8 * 100 / 7);          /* 0 .. 100 over the 8 steps, twice */
}

int preset_mods_used(const struct preset_mods *mods)
{
    for (int i = 0; i < MOD_COUNT; i++)
        if (mods->m[i].type != MOD_OFF)
            return 1;
    return 0;
}

void preset_clear(struct preset *p, const char *name)
{
    memset(p, 0, sizeof *p);
    blank_record(p->raw);
    decode_assign(p->raw, p);
    for (int i = 0; i < PRESET_SLOTS; i++) {
        p->order[i] = -1;
        p->slot[i].menu = -1;
    }
    strncpy(p->name, name, PRESET_NAME_LEN);
    p->volume = 50;
    p->bpm = 120;
    preset_route_default(p, &p->route);
    preset_controls_legacy(p, &p->controls);
}

int preset_decode(const uint8_t *rec, struct preset *out)
{
    if (!rec || rd16(rec) != PRESET_MAGIC)
        return -1;
    memset(out, 0, sizeof *out);
    memcpy(out->raw, rec, PRESET_RECORD_SIZE);
    copy_text(out->name, rec + 0x0a, PRESET_NAME_LEN);
    copy_text(out->style, rec + 0x481, 10);
    out->volume = rec[0x1e];
    out->pan = (int8_t)rec[0x1f];
    out->bpm = rd16(rec + 0x20);
    out->count = 0;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        out->order[i] = (int8_t)rec[0x22 + i];
        if (out->order[i] >= 0 && out->order[i] < PRESET_SLOTS && out->count == i)
            out->count++;
    }
    for (int i = out->count; i < PRESET_SLOTS; i++)
        out->order[i] = -1;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        const uint8_t *s = rec + 0x30 + 0x44 * i;
        struct preset_slot *d = &out->slot[i];
        d->on = s[0];
        d->menu = (int8_t)s[1];
        d->index = rds16(s + 2);
        d->id = rd32(s + 4);
        for (int k = 0; k < PRESET_KNOBS; k++)
            d->knobs[k] = rdf32(s + 8 + 4 * k);
    }
    decode_assign(rec, out);
    if (!out->route.stored)
        preset_route_default(out, &out->route);
    preset_scenes_reset(out);
    preset_controls_legacy(out, &out->controls);
    return 0;
}

void preset_encode(const struct preset *p, int index, uint8_t out[PRESET_RECORD_SIZE])
{
    memcpy(out, p->raw, PRESET_RECORD_SIZE);
    if (rd16(out) != PRESET_MAGIC)
        blank_record(out);
    put16(out + 0x08, (uint16_t)index);
    /* the original leaves old characters after the terminator: keep the bytes while the name is the same */
    char stored[PRESET_NAME_LEN + 1];
    copy_text(stored, out + 0x0a, PRESET_NAME_LEN);
    if (strcmp(stored, p->name) != 0) {
        memset(out + 0x0a, 0, PRESET_NAME_LEN);
        for (int i = 0; i < PRESET_NAME_LEN && p->name[i]; i++)
            out[0x0a + i] = (uint8_t)p->name[i];
    }
    copy_text(stored, out + 0x481, 10);     /* the style the same way */
    if (strcmp(stored, p->style) != 0) {
        memset(out + 0x481, 0, 10);
        for (int i = 0; i < 10 && p->style[i]; i++)
            out[0x481 + i] = (uint8_t)p->style[i];
    }
    out[0x1e] = p->volume;
    out[0x1f] = (uint8_t)p->pan;
    put16(out + 0x20, p->bpm);
    for (int i = 0; i < PRESET_SLOTS; i++)
        out[0x22 + i] = (uint8_t)(i < p->count ? p->order[i] : -1);
    for (int i = 0; i < PRESET_SLOTS; i++) {
        uint8_t *s = out + 0x30 + 0x44 * i;
        const struct preset_slot *d = &p->slot[i];
        /* an empty slot keeps its old knob values and index, like the original's edit buffer */
        s[0] = d->menu < 0 ? 0 : d->on;
        s[1] = d->menu < 0 ? 0xff : (uint8_t)d->menu;
        put16(s + 2, (uint16_t)(d->menu >= 0 && d->index == -2 ? 0 : d->index));  /* -2: waveod's, not installed
                                                                                 (fw/catalog.h CATALOG_MISSING) */
        memcpy(s + 4, &d->id, 4);
        memcpy(s + 8, d->knobs, 4 * PRESET_KNOBS);
    }
    encode_assign(p, out);
}

void preset_label(int index, char out[8])
{
    int bank = index / 4 + 1;
    out[0] = (char)('0' + bank / 10 % 10);
    out[1] = (char)('0' + bank % 10);
    out[2] = (char)('A' + index % 4);
    out[3] = 0;
}
