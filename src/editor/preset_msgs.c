/*
 * Group 0 of the editor protocol: presets (FUN_0002E8C8 edited preset, FUN_0002E110 export / import, the
 * sub-address table 0x42CC0 for 0x10000001), and the notifications the original sends when the edited preset
 * changes (FUN_000213CC knob, FUN_00021326 switch, FUN_0001EDE8 CTRL, FUN_00021660 preset, chain edits ...).
 *
 * The original's edited preset is a stored record (+0x238 of its state), so every reply is built from the
 * record of the edited (edit_record) or a stored preset (presets_read), byte for byte. Bytes the original
 * leaves uninitialised on its stack (the preset's version, author and info, five bytes of the CTRL block)
 * go out as zeros.
 */
#include "editor/server.h"
#include "engine/audio_engine.h"
#include "fw/bytes.h"
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define SUB_DIMS        0x01000000u
#define SUB_INFO        0x01010000u
#define SUB_QUICK       0x01020000u
#define SUB_FS          0x01030000u
#define SUB_CHAIN       0x01040000u
#define SUB_ALGR        0x01050000u
#define SUB_KNOB        0x01050001u
#define SUB_SWITCH      0x01050002u
#define SUB_VOLUME      0x01050003u
#define SUB_ALL_ON      0x01050007u
#define SUB_EXP1        0x01050008u
#define SUB_EXP2        0x01050009u
#define SUB_EXP         0x01070000u
#define SUB_CTRL        0x01080000u
#define SUB_VERSION     0x01090000u
#define SUB_LIST        0x010A0000u
#define SUB_INDEX       0x010A0001u
#define VERSION_WORD    0x300000C8u         /* the last word of the 0x0109 block */
#define PRESET_REPLY    0x4BB
#define LIST_REPLY      0x1D14

struct out {
    uint8_t *p;
    uint32_t n;
};

static void o8(struct out *o, uint32_t v) { o->p[o->n++] = (uint8_t)v; }
static void o16(struct out *o, uint32_t v) { o8(o, v); o8(o, v >> 8); }
static void o32(struct out *o, uint32_t v) { o16(o, v); o16(o, v >> 16); }
static void on(struct out *o, const void *src, uint32_t n) { memcpy(o->p + o->n, src, n); o->n += n; }
static void oz(struct out *o, uint32_t n) { memset(o->p + o->n, 0, n); o->n += n; }

static void block(struct out *o, uint32_t cmd, uint32_t len, uint32_t sub)
{
    o32(o, cmd);
    o32(o, len);
    o32(o, sub);
}

static const uint8_t *slot_rec(const uint8_t *rec, int s) { return rec + 0x30 + 0x44 * s; }

/* the 0x0104 / 0x0105 / ... blocks shared by the preset reply and the sub-address requests */
static void chain_lists(struct out *o, const uint8_t *rec)
{
    on(o, rec + 0x22, 12);
    for (int s = 0; s < 12; s++)
        o8(o, slot_rec(rec, s)[1]);
    for (int s = 0; s < 12; s++)
        on(o, slot_rec(rec, s) + 4, 4);
}

static void exp_lists(struct out *o, const uint8_t *rec)
{
    o8(o, rec[0x367]);
    for (int j = 0; j < 9; j++)
        o8(o, rec[0x368 + 12 * j]);
    for (int j = 0; j < 9; j++)
        o8(o, rec[0x369 + 12 * j]);
    for (int j = 0; j < 9; j++)
        on(o, rec + 0x36c + 12 * j, 4);
    for (int j = 0; j < 9; j++)
        on(o, rec + 0x370 + 12 * j, 4);
}

static void ctrl_lists(struct out *o, const uint8_t *rec)
{
    oz(o, 5);
    for (int c = 0; c < 5; c++)
        on(o, rec + 0x3d4 + 0x18 * c, 12);
}

static void fs_list(struct out *o, const uint8_t *rec)
{
    for (int k = 0; k < 4; k++)
        o8(o, rec[0x44d + 2 * k]);
    for (int k = 0; k < 4; k++)
        o8(o, rec[0x44e + 2 * k]);
    on(o, rec + 0x461, 3);
}

/* FUN_0002E8C8 / FUN_0002E110: the whole preset in nine blocks */
static uint32_t preset_reply(uint8_t *buf, uint32_t cmd, uint32_t index, const uint8_t *rec)
{
    static const uint8_t dims[7] = { 4, 3, 12, 1, 5, 1, 15 };
    struct out o = { buf, 0 };
    o32(&o, index);
    block(&o, cmd, 0x0b, SUB_DIMS);
    on(&o, dims, 7);
    block(&o, cmd, 0x69, SUB_INFO);
    oz(&o, 4);                              /* version: not set by the original */
    on(&o, rec + 0x0a, 20);                 /* name and the 3 bytes after it, then author / info unset */
    oz(&o, 67);
    on(&o, rec + 0x481, 10);
    block(&o, cmd, 10, SUB_QUICK);
    on(&o, rec + 0x360, 6);
    block(&o, cmd, 0x0f, SUB_FS);
    fs_list(&o, rec);
    block(&o, cmd, 0x4c, SUB_CHAIN);
    chain_lists(&o, rec);
    block(&o, cmd, 0x2e6, SUB_ALGR);
    for (int s = 0; s < 12; s++)
        on(&o, slot_rec(rec, s) + 8, 60);
    for (int s = 0; s < 12; s++)
        o8(&o, slot_rec(rec, s)[0]);
    o16(&o, rec[0x1e]);
    on(&o, rec + 0x20, 2);
    o8(&o, rec[0x1f]);
    o8(&o, rec[0x366]);
    block(&o, cmd, 0x5f, SUB_EXP);
    exp_lists(&o, rec);
    block(&o, cmd, 0x45, SUB_CTRL);
    ctrl_lists(&o, rec);
    block(&o, cmd, 0x0c, SUB_VERSION);
    o32(&o, 0x3f800000u);
    o32(&o, VERSION_WORD);
    return o.n;
}

/* position of parameter k of a slot in its knob array (FUN_0002048C; 0 beyond the effect's parameters) */
static int knob_pos(int slot, int k)
{
    struct rig_param p;
    if (slot < 0 || slot >= PRESET_SLOTS || rig_param(srv.rig, slot, k, &p) != 0)
        return 0;
    return p.def.idx < PRESET_KNOBS ? p.def.idx : 0;
}

/* ---------------------------------------------------------------- import (FUN_0002E110, flag 0x12) */

static int16_t resolve_index(int menu, uint32_t id)
{
    int m = menu, index = 0;
    if (catalog_resolve(&srv.rig->cat, &m, id, &index) == RESOLVE_INVALID || m != menu)
        return -1;
    return (int16_t)index;
}

static void import_preset(const struct msg *m, uint32_t index)
{
    static uint8_t rec[PRESET_RECORD_SIZE] SDRAM_BSS;
    memcpy(rec, preset_template, sizeof rec);
    rec[8] = (uint8_t)index;
    rec[9] = (uint8_t)(index >> 8);
    uint32_t at = 0x0c, left = m->total > 0x0c ? m->total - 0x0c : 0;
    while (at + 12 <= m->len) {
        const uint8_t *b = m->data + at;
        uint32_t len = rd32(b + 4);
        if (len > left || (int32_t)left <= 0)
            break;
        uint32_t sub = rd32(b + 8);
        if ((sub & 0xffff) == 0 && at + 8 + len <= m->len) {
            switch (sub >> 16) {
            case 0x101:
                memcpy(rec + 0x0a, b + 0x10, 17);
                memcpy(rec + 0x481, b + 0x67, 10);
                break;
            case 0x102:
                memcpy(rec + 0x360, b + 0x0c, 6);
                break;
            case 0x103:
                for (int k = 0; k < 4; k++) {
                    rec[0x44d + 2 * k] = b[0x0c + k];
                    rec[0x44e + 2 * k] = b[0x10 + k];
                }
                memcpy(rec + 0x461, b + 0x14, 3);
                break;
            case 0x104:
                for (int s = 0; s < 12; s++) {
                    uint8_t *d = rec + 0x30 + 0x44 * s;
                    rec[0x22 + s] = b[0x0c + s];
                    d[1] = b[0x18 + s];
                    memcpy(d + 4, b + 0x24 + 4 * s, 4);
                    int16_t idx = resolve_index((int8_t)d[1], rd32(d + 4));
                    d[2] = (uint8_t)idx;
                    d[3] = (uint8_t)(idx >> 8);
                }
                break;
            case 0x105:
                for (int s = 0; s < 12; s++) {
                    memcpy(rec + 0x38 + 0x44 * s, b + 0x0c + 60 * s, 60);
                    rec[0x30 + 0x44 * s] = b[0x2dc + s];
                }
                rec[0x1e] = b[0x2e8];
                memcpy(rec + 0x20, b + 0x2ea, 2);
                rec[0x1f] = b[0x2ec];
                rec[0x366] = b[0x2ed];
                break;
            case 0x107:
                rec[0x367] = b[0x0c];
                for (int j = 0; j < 9; j++) {
                    rec[0x368 + 12 * j] = b[0x0d + j];
                    rec[0x369 + 12 * j] = b[0x16 + j];
                    memcpy(rec + 0x36c + 12 * j, b + 0x1f + 4 * j, 4);
                    memcpy(rec + 0x370 + 12 * j, b + 0x43 + 4 * j, 4);
                }
                break;
            case 0x108:
                for (int c = 0; c < 5; c++)
                    memcpy(rec + 0x3d4 + 0x18 * c, b + 0x11 + 12 * c, 12);
                break;
            }
        }
        left -= len + 8;
        at += len + 8;
    }
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    presets_save(&srv.rig->src, (int)index, rec);
    engine_flash_end(flash);
    uint32_t ack = CMD_PRESET_PORT;         /* {0x17000000, 4, 0x10000002} */
    reply(CMD_PRESET_ACK, &ack, 4);
    if ((int)index == srv.rig->index)
        rig_load(srv.rig, (int)index);
}

/* ---------------------------------------------------------------- sub-addresses (0x10000001) */

static void sub_info(const struct msg *m, uint32_t sub)
{
    uint8_t buf[0x80];
    struct out o = { buf, 0 };
    uint8_t b = sub & 0xff;
    uint32_t index = msg_word(m, 3);
    if (b == 0 && m->flag == FLAG_REQUEST) {
        const uint8_t *rec = edit_record();
        o32(&o, sub);
        oz(&o, 4);
        on(&o, rec + 0x0a, 20);
        oz(&o, 67);
        on(&o, rec + 0x481, 10);
        reply(m->cmd, buf, o.n);
    } else if ((b == 1 || b == 4) && m->flag == FLAG_DATA) {   /* rename / restyle a stored preset */
        static uint8_t rec[PRESET_RECORD_SIZE] SDRAM_BSS;
        if (index >= PRESET_COUNT)
            return;
        presets_read(&srv.rig->src, (int)index, rec);
        int name = b == 1;
        name_copy(rec + (name ? 0x0a : 0x481), m->data + 0x10, name ? 17 : 10);
        int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
        presets_save(&srv.rig->src, (int)index, rec);
        engine_flash_end(flash);
        if (name)                           /* the edited preset takes the name too, whichever was renamed */
            edit_name_copy(m->data + 0x10, 17);
        o32(&o, sub);
        o32(&o, index);
        on(&o, m->data + 0x10, name ? 17 : 10);
        reply(CMD_PRESET_SUB, buf, name ? 0x19 : 0x12);
    } else if ((b == 1 || b == 4) && m->flag == FLAG_REQUEST) {
        /* {u16 0x11 (name) / unset (style), 2 unset bytes, index, the edited preset's name / style} */
        const uint8_t *rec = edit_record();
        o32(&o, b == 1 ? 0x11u : 0u);
        o32(&o, index);
        on(&o, rec + (b == 1 ? 0x0a : 0x481), b == 1 ? 17 : 10);
        reply(CMD_PRESET_SUB, buf, b == 1 ? 0x19 : 0x12);
    }
}

static void sub_quick(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct preset *p = &srv.rig->preset;
    if (b == 0) {
        if (m->flag == FLAG_REQUEST) {
            uint8_t buf[12];
            struct out o = { buf, 0 };
            o32(&o, sub);
            on(&o, edit_record() + 0x360, 6);
            reply(m->cmd, buf, 10);
        }
        return;
    }
    uint32_t k = msg_byte(m, 0x0c);
    if (m->flag == FLAG_DATA) {
        if (k >= QUICK_KNOBS)
            return;
        if (b == 1)
            p->qk_target[k] = (int8_t)msg_byte(m, 0x10);
        else
            p->qk_param[k] = (int8_t)msg_byte(m, 0x10);
        rig_set_dirty(srv.rig, 1);
        notify_words(CMD_PRESET_SUB, 3, sub, k, (uint32_t)(int32_t)(b == 1 ? p->qk_target[k] : p->qk_param[k]));
    } else if (m->flag == FLAG_REQUEST) {
        notify_words(CMD_PRESET_SUB, 3, msg_word(m, 2), msg_word(m, 3), 0u);
    }
}

static void sub_fs(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct preset *p = &srv.rig->preset;
    if (b == 0 || (b == 4 && m->flag == FLAG_REQUEST)) {
        if (m->flag == FLAG_REQUEST) {
            uint8_t buf[16];
            struct out o = { buf, 0 };
            o32(&o, sub);
            fs_list(&o, edit_record());
            reply(m->cmd, buf, 15);
        }
        return;
    }
    if (b == 4) {                           /* reset: the template's functions */
        memcpy(p->fs, preset_template + 0x44d, sizeof p->fs);
        rig_set_dirty(srv.rig, 1);
        return;
    }
    if (m->flag != FLAG_DATA)
        return;
    int key = (int8_t)msg_byte(m, 0x0c), value = (int8_t)msg_byte(m, 0x10);
    int entry = b == 1 ? (key & 0x7f) * 2 : b == 2 ? key * 2 + 1 : key + FS_COMBO;
    if (entry < 0 || entry >= FS_ENTRIES)
        return;
    p->fs[FS_STOMP][entry] = (int8_t)value;     /* FUN_00033AB0(0, entry, value) */
    rig_set_dirty(srv.rig, 1);
    uint32_t what = entry >= FS_COMBO ? 0x01030003u : entry & 1 ? 0x01030002u : 0x01030001u;
    uint32_t n = entry >= FS_COMBO ? (uint32_t)(entry - FS_COMBO) : (uint32_t)entry >> 1;
    notify_words(CMD_PRESET_SUB, 3, what, n, (uint32_t)(int32_t)p->fs[FS_STOMP][entry]);
}

static void chain_reply(uint32_t cmd, uint8_t changed)
{
    uint8_t buf[0x60];
    struct out o = { buf, 0 };
    const uint8_t *rec = edit_record();
    o32(&o, SUB_CHAIN);
    chain_lists(&o, rec);
    for (int s = 0; s < 12; s++)
        o8(&o, slot_rec(rec, s)[0]);
    o8(&o, changed);
    reply(cmd, buf, o.n);
}

static void sub_chain(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct rig *r = srv.rig;
    if (b == 0) {
        if (m->flag == FLAG_REQUEST)
            chain_reply(m->cmd, msg_byte(m, 0x60));
        return;
    }
    if (m->flag != FLAG_DATA)
        return;
    if (b == 2) {                           /* another algorithm in a slot, by slot */
        int slot = msg_byte(m, 0x0c), menu = (int8_t)msg_byte(m, 0x0d);
        int16_t idx = resolve_index(menu, rd32(m->data + 0x0e));
        if (slot < PRESET_SLOTS && idx >= 0)
            rig_replace(r, slot, menu, idx);
        return;
    }
    if (b != 1 && b != 3)
        return;
    int old = (int8_t)msg_byte(m, 0x0c), pos = (int8_t)msg_byte(m, 0x0d), menu = (int8_t)msg_byte(m, 0x0e);
    uint32_t id = msg_byte(m, 0x0f) | (uint32_t)msg_byte(m, 0x10) << 8 | (uint32_t)msg_byte(m, 0x11) << 16 |
                  (uint32_t)msg_byte(m, 0x12) << 24;
    int16_t idx = resolve_index(menu, id);
    int slot = -1;
    if (old == -1 && pos != -1) {
        if (idx < 0 || rig_insert(r, pos, menu, idx) != 0)
            return;
        slot = r->preset.order[pos < r->preset.count ? pos : r->preset.count - 1];
    } else if (b == 1 && old != -1 && pos == -1) {
        rig_remove(r, old);
        return;
    } else if (b == 1 && old != pos) {
        rig_move(r, old, pos);
        return;
    } else if (old == pos && pos >= 0 && pos < r->preset.count) {
        slot = r->preset.order[pos];
        if (idx < 0 || rig_replace(r, slot, menu, idx) != 0)
            return;
    }
    if (b != 3 || slot < 0)
        return;
    /* paste: the copied block's values (FUN_000213CC without telling), then the request echoed */
    int count = rig_param_count(r, slot);
    for (int k = 0; k < count && 0x13 + 4 * k + 4 <= (int)m->len; k++) {
        r->quiet++;
        r->preset_wide++;                   /* the block's values, not a scene's */
        rig_param_set(r, slot, k, rdf32(m->data + 0x13 + 4 * k));
        r->preset_wide--;
        r->quiet--;
    }
    uint8_t buf[0x50];
    struct out o = { buf, 0 };
    o32(&o, sub);
    o8(&o, (uint8_t)old);
    o8(&o, (uint8_t)pos);
    o8(&o, (uint8_t)menu);
    o32(&o, id);
    for (int k = 0; k < count && k < 15; k++)
        on(&o, m->data + 0x13 + 4 * k, 4);
    reply(m->cmd, buf, 0x47);
}

static void sub_algr(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct rig *r = srv.rig;
    if (b == 0) {
        if (m->flag != FLAG_REQUEST)
            return;
        static uint8_t buf[0x2f0] SDRAM_BSS;
        struct out o = { buf, 0 };
        const uint8_t *rec = edit_record();
        o32(&o, sub);
        for (int s = 0; s < 12; s++)            /* in parameter order, unlike the preset reply */
            for (int k = 0; k < 15; k++)
                on(&o, slot_rec(rec, s) + 8 + 4 * knob_pos(s, k), 4);
        for (int s = 0; s < 12; s++)
            o8(&o, slot_rec(rec, s)[0]);
        o16(&o, rig_tempo(r));
        oz(&o, 4);
        reply(m->cmd, buf, 0x2e6);
        return;
    }
    uint32_t slot = msg_word(m, 3) & 0xff;
    if (b == 1) {
        uint32_t k = msg_word(m, 4);
        if (m->flag == FLAG_DATA) {
            if (slot < PRESET_SLOTS && k < 15)
                rig_param_set(r, (int)slot, (int)k, rdf32(m->data + 0x14));
        } else if (m->flag == FLAG_REQUEST && slot < PRESET_SLOTS) {
            int pos = knob_pos((int)slot, (int)k);
            uint32_t v;
            memcpy(&v, &r->preset.slot[slot].knobs[pos], 4);
            notify_words(CMD_PRESET_SUB, 4, SUB_KNOB, slot, (uint32_t)pos, k < 15 ? v : 0u);
        }
    } else if (b == 2) {
        if (m->flag == FLAG_DATA) {
            if (slot < PRESET_SLOTS && rig_slot_used(r, (int)slot) &&
                r->preset.slot[slot].on != (msg_byte(m, 0x10) != 0))
                rig_toggle(r, (int)slot);
        } else if (m->flag == FLAG_REQUEST && slot < PRESET_SLOTS) {
            notify_words(CMD_PRESET_SUB, 3, SUB_SWITCH, slot, (uint32_t)r->preset.slot[slot].on);
        }
    } else if (b == 3) {
        if (m->flag == FLAG_DATA && msg_byte(m, 0x0c) <= 100)
            rig_set_volume(r, msg_byte(m, 0x0c));
        else if (m->flag == FLAG_REQUEST)
            notify_words(CMD_PRESET_SUB, 2, SUB_VOLUME, (uint32_t)r->preset.volume);
    } else if (b == 4) {
        if (m->flag == FLAG_DATA)
            rig_set_bpm(r, (int)msg_word(m, 3));
        else if (m->flag == FLAG_REQUEST)
            notify_words(CMD_PRESET_SUB, 2, 0x01050004u, (uint32_t)rig_tempo(r));
    } else if (b == 5) {
        int pan = (int8_t)msg_byte(m, 0x0c);
        if (m->flag == FLAG_DATA && pan >= -100 && pan <= 100) {
            r->preset.pan = (int8_t)pan;
            engine_set_preset_level(r->preset.volume, r->preset.pan);
            rig_set_dirty(r, 1);
        }
        if (m->flag == FLAG_DATA || m->flag == FLAG_REQUEST)
            notify_words(CMD_PRESET_SUB, 2, 0x01050005u, (uint32_t)(int32_t)r->preset.pan);
    }
}

static void sub_exp(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct rig *r = srv.rig;
    if (b == 0) {
        if (m->flag == FLAG_REQUEST) {
            uint8_t buf[0x64];
            struct out o = { buf, 0 };
            o32(&o, sub);
            exp_lists(&o, edit_record());
            notify(m->cmd, buf, 0x5f);
        }
        return;
    }
    uint32_t g = msg_word(m, 3) & 0xff, t = msg_word(m, 4) & 0xff;
    if (g >= EXP_GROUPS || t >= EXP_TARGETS || m->flag != FLAG_DATA)
        return;
    struct exp_target *x = &r->preset.exp[g][t];
    if (b == 2) {                           /* FUN_0001FE78: target slot / parameter */
        int slot = (int8_t)msg_byte(m, 0x14), param = (int8_t)msg_byte(m, 0x18);
        if (slot >= PRESET_SLOTS || param >= 15 || (x->slot == slot && x->param == param))
            return;
        if (slot < 0 || param < 0) {
            x->slot = -1;
            x->param = -1;
        } else {
            struct rig_param p;
            float lo = 0, hi = 100;
            if (rig_param(r, slot, param, &p) == 0) {
                lo = p.def.min;
                hi = p.def.max;
            }
            x->slot = (int8_t)slot;
            x->param = (int8_t)param;
            x->min = lo;
            x->max = hi;
        }
        rig_set_dirty(r, 1);
        uint8_t buf[0x24];
        struct out o = { buf, 0 };
        o32(&o, 0x01070002u);
        o32(&o, g);
        o32(&o, t);
        o32(&o, (uint32_t)(int32_t)x->slot);
        o32(&o, (uint32_t)(int32_t)x->param);
        uint32_t lo, hi;
        memcpy(&lo, &x->min, 4);
        memcpy(&hi, &x->max, 4);
        o32(&o, lo);
        o32(&o, hi);
        reply(CMD_PRESET_SUB, buf, 0x1c);
    } else if (b == 3 || b == 4) {          /* range, clamped to the parameter's */
        float v = rdf32(m->data + 0x14), lo = 0, hi = 100;
        struct rig_param p;
        if (x->slot >= 0 && rig_param(r, x->slot, x->param, &p) == 0) {
            lo = p.def.min;
            hi = p.def.max;
        }
        v = v < lo ? lo : v > hi ? hi : v;
        for (int k = 0; k < EXP_TARGETS; k++) {
            struct exp_target *y = &r->preset.exp[g][k];
            if (y->slot == x->slot && y->param == x->param) {
                if (b == 3)
                    y->min = v;
                else
                    y->max = v;
            }
        }
        rig_set_dirty(r, 1);
        uint32_t w;
        float cur = b == 3 ? x->min : x->max;
        memcpy(&w, &cur, 4);
        notify_words(CMD_PRESET_SUB, 4, b == 3 ? 0x01070003u : 0x01070004u, g, t, w);
    }
}

static void sub_ctrl(const struct msg *m, uint32_t sub)
{
    uint8_t b = sub & 0xff;
    struct preset *p = &srv.rig->preset;
    if (b == 0) {
        if (m->flag == FLAG_REQUEST) {
            uint8_t buf[0x48];
            struct out o = { buf, 0 };
            o32(&o, sub);
            ctrl_lists(&o, edit_record());
            reply(m->cmd, buf, 0x45);
        }
        return;
    }
    uint32_t c = msg_word(m, 3) & 0xff, k = msg_word(m, 4) & 0xff;
    if (c >= CTRL_COUNT || k >= PRESET_SLOTS)
        return;
    if (m->flag == FLAG_DATA) {
        p->ctrl[c][k] = msg_byte(m, 0x14);
        rig_set_dirty(srv.rig, 1);
    } else if (m->flag != FLAG_REQUEST) {
        return;
    }
    notify_words(CMD_PRESET_SUB, 4, 0x01080001u, c, k, (uint32_t)p->ctrl[c][k]);
}

static void sub_list(const struct msg *m, uint32_t sub)
{
    if ((sub & 0xff) == 0) {
        if (m->flag != FLAG_REQUEST)
            return;
        static uint8_t buf[LIST_REPLY] SDRAM_BSS;
        static uint8_t rec[PRESET_RECORD_SIZE] SDRAM_BSS;
        struct out o = { buf, 0 };
        o32(&o, sub);
        for (int i = 0; i < PRESET_COUNT; i++)
            o32(&o, (uint32_t)presets_map_value(&srv.rig->src, i));
        for (int i = 0; i < PRESET_COUNT; i++) {
            presets_read(&srv.rig->src, i, rec);
            memcpy(buf + 4 + 4 * PRESET_COUNT + 17 * i, rec + 0x0a, 17);
            memcpy(buf + 4 + 4 * PRESET_COUNT + 17 * PRESET_COUNT + 10 * i, rec + 0x481, 10);
        }
        reply(m->cmd, buf, LIST_REPLY);
        return;
    }
    if (m->flag == FLAG_DATA) {
        uint32_t index = msg_word(m, 3) & 0xffff;
        if (index < PRESET_COUNT)
            rig_load(srv.rig, (int)index);
    } else if (m->flag == FLAG_REQUEST) {
        notify_words(CMD_PRESET_SUB, 2, SUB_INDEX, (uint32_t)srv.rig->index);
    }
}

void msg_preset(const struct msg *m)
{
    static uint8_t buf[PRESET_REPLY] SDRAM_BSS;
    static uint8_t rec[PRESET_RECORD_SIZE] SDRAM_BSS;
    uint32_t sub = m->cmd & 0xf, word = msg_word(m, 2);
    if (sub == 0) {                         /* the edited preset */
        if (m->flag == FLAG_REQUEST && word < PRESET_COUNT)
            reply(m->cmd, buf, preset_reply(buf, m->cmd, word, edit_record()));
    } else if (sub == 2) {                  /* a stored preset: export, or import over it */
        if (m->flag == FLAG_REQUEST && word < PRESET_COUNT) {
            presets_read(&srv.rig->src, (int)word, rec);
            reply(m->cmd, buf, preset_reply(buf, m->cmd, word, rec));
        } else if (m->flag == FLAG_DATA && word < PRESET_COUNT) {
            import_preset(m, word);
        }
    } else if (sub == 1) {
        switch ((word & 0xfffff) >> 16) {
        case 0x0:
            if (m->flag == FLAG_REQUEST) {
                static const uint8_t dims[7] = { 4, 3, 12, 1, 5, 1, 15 };
                uint8_t d[11];
                memcpy(d, &word, 4);
                memcpy(d + 4, dims, 7);
                reply(m->cmd, d, 11);
            }
            break;
        case 0x1: sub_info(m, word); break;
        case 0x2: sub_quick(m, word); break;
        case 0x3: sub_fs(m, word); break;
        case 0x4: sub_chain(m, word); break;
        case 0x5: sub_algr(m, word); break;
        case 0x7: sub_exp(m, word); break;
        case 0x8: sub_ctrl(m, word); break;
        case 0x9: {
            uint32_t v[3] = { SUB_VERSION, 0x3f800000u, VERSION_WORD };
            reply(CMD_PRESET_SUB, v, sizeof v);
            break;
        }
        case 0xA: sub_list(m, word); break;
        }
    }
}

/* ---------------------------------------------------------------- the pedal's own changes */

void preset_rig_event(struct rig *r, enum rig_event ev, int a, int b)
{
    switch (ev) {
    case RIG_EV_LOAD:
        notify_words(CMD_PRESET_SUB, 2, SUB_INDEX, (uint32_t)a);
        break;
    case RIG_EV_PARAM: {
        int pos = knob_pos(a, b);
        uint32_t v;
        memcpy(&v, &r->preset.slot[a].knobs[pos], 4);
        notify_words(CMD_PRESET_SUB, 4, SUB_KNOB, (uint32_t)a, (uint32_t)pos, v);
        break;
    }
    case RIG_EV_ON:
        notify_words(CMD_PRESET_SUB, 3, SUB_SWITCH, (uint32_t)a, (uint32_t)r->preset.slot[a].on);
        break;
    case RIG_EV_ALL_ON: {
        uint8_t buf[16];
        memcpy(buf, &(uint32_t){ SUB_ALL_ON }, 4);
        for (int s = 0; s < 12; s++)
            buf[4 + s] = r->preset.slot[s].on;
        reply(CMD_PRESET_SUB, buf, 16);
        break;
    }
    case RIG_EV_CHAIN:
        chain_reply(CMD_PRESET_SUB, a < 0 ? 0xff : (uint8_t)a);
        break;
    case RIG_EV_ASSIGN: {                   /* FUN_00021138 / FUN_00021230: every assignment after the change */
        static uint8_t buf[0xa0] SDRAM_BSS;
        struct out o = { buf, 0 };
        const uint8_t *rec = edit_record();
        o32(&o, 0x01040004u);
        on(&o, rec + 0x360, 6);
        for (int j = 0; j < 9; j++)
            o8(&o, rec[0x368 + 12 * j]);
        for (int j = 0; j < 9; j++)
            o8(&o, rec[0x369 + 12 * j]);
        for (int j = 0; j < 9; j++)
            on(&o, rec + 0x36c + 12 * j, 4);
        for (int j = 0; j < 9; j++)
            on(&o, rec + 0x370 + 12 * j, 4);
        for (int c = 0; c < 5; c++)
            on(&o, rec + 0x3d4 + 0x18 * c, 12);
        reply(CMD_PRESET_SUB, buf, o.n);
        break;
    }
    case RIG_EV_CHAIN_FULL:
        srv.overload = 2;
        notify_words(CMD_GLOBAL_SUB, 2, 0x0700u, 2u);
        rig_set_dirty(r, 1);
        chain_reply(CMD_PRESET_SUB, 0xff);
        break;
    case RIG_EV_DIRTY:
        notify_words(CMD_GLOBAL_SUB, 2, 0x0800u, (uint32_t)a);
        break;
    case RIG_EV_VOLUME:
        notify_words(CMD_PRESET_SUB, 2, SUB_VOLUME, (uint32_t)r->preset.volume);
        break;
    case RIG_EV_EXP: {                      /* FUN_000201D8: the targets of the group and their values */
        uint8_t buf[0x16];
        struct out o = { buf, 0 };
        o32(&o, a < 2 ? SUB_EXP1 : SUB_EXP2);
        const struct exp_target *x = r->preset.exp[a];
        for (int t = 0; t < 3; t++)
            o8(&o, x[t].slot >= 0 && x[t].param >= 0 ? (uint8_t)x[t].slot : 0xff);
        for (int t = 0; t < 3; t++)
            o8(&o, x[t].slot >= 0 && x[t].param >= 0 ? (uint8_t)knob_pos(x[t].slot, x[t].param) : 0xff);
        for (int t = 0; t < 3; t++) {
            uint32_t v = 0;
            if (x[t].slot >= 0 && x[t].slot < PRESET_SLOTS && x[t].param >= 0)
                memcpy(&v, &r->preset.slot[x[t].slot].knobs[knob_pos(x[t].slot, x[t].param)], 4);
            o32(&o, v);
        }
        notify(CMD_PRESET_SUB, buf, o.n);
        break;
    }
    case RIG_EV_SAVED: {
        uint8_t buf[0x15];
        memcpy(buf, &a, 4);
        memcpy(buf + 4, edit_record() + 0x0a, 17);
        reply(0x05000001u, buf, sizeof buf);
        if (b)
            notify_words(CMD_PRESET_SUB, 2, SUB_INDEX, (uint32_t)a);
        break;
    }
    case RIG_EV_SCENE:                      /* the original's editor has no scenes (RIG_EV_ALL_ON told the blocks) */
        break;
    }
}
