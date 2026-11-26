/* The footswitch control table (fw/preset_format.h): conversion from the original's fields, upkeep. */
#include "fw/preset_format.h"
#include "platform/sdram.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

int preset_control_add(struct preset_controls *c, const struct control *link)
{
    if (c->count >= CONTROL_MAX)
        return -1;
    c->c[c->count++] = *link;
    return 0;
}

void preset_control_remove(struct preset_controls *c, int i)
{
    if (i < 0 || i >= c->count)
        return;
    memmove(&c->c[i], &c->c[i + 1], (size_t)(c->count - i - 1) * sizeof c->c[0]);
    c->count--;
}

void preset_controls_forget(struct preset *p, int slot, int params_only)
{
    struct preset_controls *c = &p->controls;
    for (int i = c->count - 1; i >= 0; i--) {
        const struct control *l = &c->c[i];
        if (l->slot == slot && (l->action == CA_PARAM || (!params_only && l->action == CA_BLOCK)))
            preset_control_remove(c, i);
    }
}

/* the same link in another mode: one entry for both */
static void add(struct preset_controls *c, int key, int mode, int when, int action, int slot, int arg)
{
    for (int i = 0; i < c->count; i++) {
        struct control *l = &c->c[i];
        if (l->key == key && l->when == when && l->action == action && l->slot == slot && l->arg == arg) {
            l->modes |= (uint8_t)mode;
            return;
        }
    }
    struct control l = { .key = (uint8_t)key, .modes = (uint8_t)mode, .when = (uint8_t)when, .action = (uint8_t)action,
                         .slot = (int8_t)slot, .arg = (uint8_t)arg };
    preset_control_add(c, &l);
}

/* the members of CTRL n that are blocks, in chain order */
static void ctrl_blocks(const struct preset *p, struct preset_controls *c, int n, int key, int mode, int when)
{
    for (int pos = 0; pos < p->count; pos++) {
        int slot = p->order[pos];
        if (slot >= 0 && slot < PRESET_SLOTS && p->slot[slot].menu >= 0 && p->ctrl[n][slot] == 1)
            add(c, key, mode, when, CA_BLOCK, slot, 0);
    }
}

/* one footswitch function of the original */
static void legacy_fn(const struct preset *p, struct preset_controls *c, int fn, int key, int mode, int when)
{
    if (fn == FSF_NONE)
        return;
    if (fn >= FSF_CTRL1 && fn <= FSF_CTRL4)
        ctrl_blocks(p, c, fn - FSF_CTRL1, key, mode, when);
    else if (fn >= FSF_SLOT1 && fn <= FSF_SLOT12)
        add(c, key, mode, when, CA_BLOCK, fn - FSF_SLOT1, 0);
    else
        add(c, key, mode, when, CA_FN, -1, fn);
}

void preset_controls_original(const struct preset *p, struct preset_controls *out)
{
    memset(out, 0, sizeof *out);
    static const struct { int group, mode; } groups[2] = { { FS_PRESET, CM_PRESET }, { FS_STOMP, CM_STOMP } };
    for (int g = 0; g < 2; g++) {
        const int8_t *fs = p->fs[groups[g].group];
        int mode = groups[g].mode;
        /* scene mode: A-D and BANK -/+ its own (no long presses: a preset load by accident), the pairs as in
           preset mode */
        int with_scene = groups[g].group == FS_PRESET ? CM_SCENE : 0;
        for (int k = 0; k < CK_KEYS; k++) {
            legacy_fn(p, out, fs[2 * k], k, mode, CW_PRESS);
            legacy_fn(p, out, fs[2 * k + 1], k, mode, CW_HOLD);
        }
        for (int i = 0; i < 4; i++)
            legacy_fn(p, out, fs[FS_COMBO + i], CK_COMBO + i, mode | with_scene, CW_PRESS);
    }
    for (int n = 0; n < 4; n++)
        add(out, n, CM_SCENE, CW_PRESS, CA_SCENE, -1, n);
    add(out, CK_BANK_DOWN, CM_SCENE, CW_PRESS, CA_FN, -1, FSF_PRESET_DOWN);
    add(out, CK_BANK_UP, CM_SCENE, CW_PRESS, CA_FN, -1, FSF_PRESET_UP);
    add(out, CK_BANK_DOWN, CM_SCENE, CW_HOLD, CA_FN, -1, FSF_PRESET_DOWN);
    add(out, CK_BANK_UP, CM_SCENE, CW_HOLD, CA_FN, -1, FSF_PRESET_UP);
    ctrl_blocks(p, out, CTRL_COUNT - 1, CK_AGAIN, CM_PRESET, CW_PRESS);   /* the preset CTRL */

    /* the pedals: each EXP target a parameter link (min at the heel, max at the toe; the parameter still in
       library order), the toe switch flips EXP 1's group (whose wah and volume targets follow it:
       model/control.c) */
    add(out, CK_TOE, CM_ALL, CW_PRESS, CA_FN, -1, FSF_EXP_AB);
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++) {
            const struct exp_target *x = &p->exp[g][t];
            if (x->slot < 0 || x->slot >= PRESET_SLOTS || x->param < 0 || p->slot[x->slot].menu < 0)
                continue;
            struct control l = { .key = (uint8_t)(CK_EXP1A + g), .modes = CM_ALL, .when = CW_PRESS,
                                 .action = CA_PARAM, .slot = x->slot, .arg = (uint8_t)x->param,
                                 .flags = CF_ORDER, .off = x->min, .on = x->max };
            preset_control_add(out, &l);
        }
}

/* the footswitch functions of every factory preset (stomp mode A-D are its CTRL 1-4, preset mode press and hold
   alike) */
static const int8_t factory_fs[FS_GROUPS][FS_ENTRIES] = {
    [FS_STOMP] = { FSF_CTRL1, 0, FSF_CTRL1 + 1, 0, FSF_CTRL1 + 2, 0, FSF_CTRL4, 0, FSF_BANK_DOWN, 0, FSF_BANK_UP, 0,
                   [FS_COMBO] = FSF_TUNER, FSF_NONE, FSF_LOOPER, FSF_MODE },
    [FS_PRESET] = { FSF_PRESET_A, FSF_PRESET_A, FSF_PRESET_A + 1, FSF_PRESET_A + 1, FSF_PRESET_A + 2,
                    FSF_PRESET_A + 2, FSF_PRESET_D, FSF_PRESET_D, FSF_BANK_DOWN, FSF_BANK_DOWN, FSF_BANK_UP,
                    FSF_BANK_UP, [FS_COMBO] = FSF_TUNER, FSF_NONE, FSF_LOOPER, FSF_MODE },
};

void preset_controls_factory(struct preset_controls *out)
{
    static struct preset p SDRAM_BSS;       /* no blocks, no CTRL members, no EXP targets */
    memset(&p, 0, sizeof p);
    memcpy(p.fs, factory_fs, sizeof p.fs);
    for (int s = 0; s < PRESET_SLOTS; s++)
        p.slot[s].menu = -1;
    for (int g = 0; g < EXP_GROUPS; g++)
        for (int t = 0; t < EXP_TARGETS; t++)
            p.exp[g][t].slot = -1;
    preset_controls_original(&p, out);
}

int preset_control_global_ok(const struct control *l)
{
    return l->action == CA_FN || l->action == CA_SCENE || l->action == CA_MIDI;
}

static int same_link(const struct control *x, const struct control *y)
{
    return x->when == y->when && x->action == y->action && x->slot == y->slot && x->arg == y->arg &&
           x->curve == y->curve && x->flags == y->flags && x->off == y->off && x->on == y->on;
}

static int in_group(const struct control *l, int key, int mode, int hold)
{
    return l->key == key && (l->modes & mode) && (hold ? l->when == CW_HOLD : l->when != CW_HOLD);
}

/* how many links of group (key, mode, hold) in a are like l */
static int alike(const struct preset_controls *a, int key, int mode, int hold, const struct control *l)
{
    int n = 0;
    for (int i = 0; i < a->count; i++)
        n += in_group(&a->c[i], key, mode, hold) && same_link(&a->c[i], l);
    return n;
}

static int group_size(const struct preset_controls *a, int key, int mode, int hold)
{
    int n = 0;
    for (int i = 0; i < a->count; i++)
        n += in_group(&a->c[i], key, mode, hold);
    return n;
}

/* the same links (as many of each) in group (key, mode, hold) of a and b; *na = how many a has */
static int same_group(const struct preset_controls *a, const struct preset_controls *b, int key, int mode, int hold,
                      int *na)
{
    int n = 0, same = 1;
    for (int i = 0; i < a->count; i++)
        if (in_group(&a->c[i], key, mode, hold)) {
            n++;
            same &= alike(a, key, mode, hold, &a->c[i]) == alike(b, key, mode, hold, &a->c[i]);
        }
    *na = n;
    return same && n == group_size(b, key, mode, hold);
}

void preset_controls_legacy(const struct preset *p, struct preset_controls *out)
{
    static struct preset_controls factory SDRAM_BSS;
    static uint8_t ready;
    if (!ready) {
        preset_controls_factory(&factory);
        ready = 1;
    }
    preset_controls_original(p, out);
    uint8_t keep[CONTROL_MAX];
    for (int i = 0; i < out->count; i++)
        keep[i] = out->c[i].modes;
    for (int key = 0; key < CK_COUNT; key++)
        for (int mode = CM_PRESET; mode <= CM_SCENE; mode <<= 1)
            for (int hold = 0; hold < 2; hold++) {
                int n;
                if (same_group(out, &factory, key, mode, hold, &n)) {
                    for (int i = 0; i < out->count; i++)
                        if (in_group(&out->c[i], key, mode, hold))
                            keep[i] &= (uint8_t)~mode;
                } else if (n == 0 && group_size(&factory, key, mode, hold) > 0) {
                    struct control none = { .key = (uint8_t)key, .modes = (uint8_t)mode,
                                            .when = (uint8_t)(hold ? CW_HOLD : CW_PRESS), .action = CA_NOTHING,
                                            .slot = -1 };
                    int merged = 0;
                    for (int i = 0; i < out->count && !merged; i++)
                        if (out->c[i].key == key && out->c[i].action == CA_NOTHING && out->c[i].when == none.when) {
                            keep[i] |= (uint8_t)mode;
                            merged = 1;
                        }
                    if (!merged && out->count < CONTROL_MAX) {
                        keep[out->count] = (uint8_t)mode;
                        out->c[out->count++] = none;
                    }
                }
            }
    int n = 0;
    for (int i = 0; i < out->count; i++)
        if (keep[i]) {
            out->c[n] = out->c[i];
            out->c[n++].modes = keep[i];
        }
    out->count = (uint8_t)n;
}
