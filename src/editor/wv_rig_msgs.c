/*
 * Group 0xF of the editor protocol, protocol 8: the edited preset as the pedal's chain screen shows it, the
 * catalog of effects, and live edits through the same rig calls the pedal's own screens make (waveod Studio's
 * GRID). Requests and answers as in editor/wv_msgs.c; texts are {u8 n, n bytes} (UTF-8, not terminated).
 *
 *   0x1F000015 rig     {}                       -> {u32 gen, u32 loads, u16 index, u8 dirty, u8 volume, u16 tempo,
 *                                                  u8 route mode, split, merge, u8 joint[6], u8 scene active,
 *                                                  u8 scenes in play, text name, text scene names[4], u8 order
 *                                                  count, i8 order[count], u8 slots, per used slot {u8 slot, u8 on,
 *                                                  i8 menu, i16 index, u32 id, i8 cell, u8 kind, u8 parameters, text
 *                                                  name, text type}}
 *   0x1F000016 block   {u32 slot}               -> {u32 slot, i32 error, u8 parameters, per parameter (library
 *                                                  order) {u8 knob, u8 flags, f32 min, max, step, default, value,
 *                                                  text name, text value, u8 options, options x text}}
 *   0x1F000017 catalog {u32 menu, u32 first}    -> {u32 menu, u32 count, u32 first, u8 n, per item {u16 index, u32
 *                                                  id, u8 kind, text name, text type}}; as many as fit
 *   0x1F000018 edit    {u32 op, u32 a, b, c, f32 v} -> {u32 op, i32 result, u32 gen}, for EDIT_PARAM then the
 *                                                  parameter as block's; enum edit_op (EDIT_SAVE writes the
 *                                                  flash, the others change the edited preset only; protocol 10
 *                                                  adds the scene menu's operations, EDIT_SCENE_NAME with the
 *                                                  name after v; protocol 12 EDIT_NAME, the same way)
 *
 * kind: enum item_kind. Parameter flags: 1 tempo-synced (min..max are the note divisions), 2 a scene parameter,
 * 4 named values (options).
 */
#include "editor/server.h"
#include "fw/bytes.h"
#include "fw/catalog.h"
#include "model/param_format.h"
#include "model/rig.h"
#include "model/route.h"
#include "platform/sdram.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

enum item_kind { KIND_LIBRARY, KIND_BUILTIN, KIND_MODULE, KIND_MISSING, KIND_ROUTING };

enum edit_op {
    EDIT_PARAM,         /* slot a, parameter b (library order) = v */
    EDIT_ON,            /* slot a on (b != 0) / off */
    EDIT_PLACE,         /* cell a gets item c of menu b; result = its slot */
    EDIT_MOVE,          /* slot a to cell b (a block there swaps) */
    EDIT_REMOVE,        /* slot a */
    EDIT_REPLACE,       /* slot a gets item c of menu b */
    EDIT_ROUTE,         /* mode a, split b, merge c; result = 1 changed */
    EDIT_JOINT,         /* joint a = b (0..100) */
    EDIT_SCENE,         /* scene a */
    EDIT_VOLUME,        /* a = 0..100 */
    EDIT_BPM,           /* a = 40..300 */
    EDIT_SAVE,          /* the edited preset into place a (0..239), as the pedal's SAVE; it becomes the active one */
    /* protocol 10: the scene menu (ui/widgets/top_bar.c) and KNOBS NA CENA's switch (ui/screens/scr_link.c) */
    EDIT_SCENE_PICK,    /* scene a plays and edits go to it (the scene menu's pick: the scenes are in play) */
    EDIT_SCENE_COPY,    /* scene a over scene b */
    EDIT_SCENE_REVERT,  /* scene a as the stored preset has it; -1 = the preset is not stored */
    EDIT_SCENE_NAME,    /* scene a's name: {u8 n, n bytes} after v (the default name or nothing: none) */
    EDIT_SCENE_PARAM,   /* parameter b of slot a becomes a scene parameter, or the preset's again; result = 1 / 0 */
    /* protocol 12 */
    EDIT_NAME,          /* the edited preset's name, {u8 n, n bytes} after v, as the save screen's NOME: written by
                           EDIT_SAVE */
};

/* the text after v ({u8 n, n bytes}) into out, at most max characters */
static void edit_text(const struct msg *m, char *out, unsigned max)
{
    unsigned n = m->len >= 29 ? m->data[28] : 0;
    if (n > max)
        n = max;
    if (29 + n > m->len)
        n = m->len > 29 ? m->len - 29 : 0;
    memcpy(out, m->data + 29, n);
    out[n] = 0;
}

#define P_SYNCED 1
#define P_SCENE  2
#define P_LIST   4

static uint8_t buf[16 + WV_CHUNK] SDRAM_BSS;
static unsigned at;

static void b8(uint32_t v) { buf[at++] = (uint8_t)v; }
static void b16(uint32_t v) { put16le(buf + at, (uint16_t)v); at += 2; }
static void b32(uint32_t v) { put32le(buf + at, v); at += 4; }

static void bf32(float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    b32(u);
}

static void btext(const char *s)
{
    unsigned n = s ? (unsigned)strlen(s) : 0;
    if (n > 255)
        n = 255;
    b8(n);
    if (n)
        memcpy(buf + at, s, n);
    at += n;
}

static int fits(unsigned more)
{
    return at + more <= WV_CHUNK;
}

static int kind_of(const struct catalog_item *item, int index)
{
    if (index == CATALOG_MISSING)
        return KIND_MISSING;
    if (item->block)
        return KIND_BUILTIN;
    return item->engine.wv ? KIND_MODULE : KIND_LIBRARY;
}

/* the type an item is listed under, as the pedal's browser groups them (ui/screens/scr_browse.c) */
static const char *type_of(int menu, const struct catalog_item *item)
{
    if (menu == MENU_CLONE)
        return item->block ? "NAM" : "Capturas";
    if (menu == MENU_IR)
        return "Usu\xC3\xA1rio";
    if (catalog_is_special(menu))
        return "Gabinetes";
    return item->engine.category && item->engine.category[0] ? item->engine.category : "-";
}

static void rig_state(const struct msg *m)
{
    struct rig *r = srv.rig;
    const struct preset *p = &r->preset;
    at = 0;
    b32(r->gen);
    b32(r->loads);
    b16((uint32_t)r->index);
    b8(r->dirty);
    b8(p->volume);
    b16(rig_tempo(r));
    b8(p->route.mode);
    b8(p->route.split);
    b8(p->route.merge);
    for (int k = 0; k < JOINT_PARAMS; k++)
        b8(p->route.joint[k]);
    b8((uint32_t)rig_scene_active(r));
    b8((uint32_t)rig_scene_editing(r));
    btext(p->name);
    for (int n = 0; n < SCENE_COUNT; n++) {
        char name[SCENE_NAME_LEN + 1];
        rig_scene_name(r, n, name);
        btext(name);
    }
    b8(p->count);
    for (int i = 0; i < p->count; i++)
        b8((uint8_t)p->order[i]);
    unsigned count_at = at;
    b8(0);
    for (int s = 0; s < PRESET_SLOTS; s++) {
        struct catalog_item item;
        if (!rig_slot_used(r, s) || rig_slot_item(r, s, &item) != 0)
            continue;
        const struct preset_slot *ps = &p->slot[s];
        b8((uint32_t)s);
        b8(ps->on);
        b8((uint8_t)ps->menu);
        b16((uint16_t)ps->index);
        b32(ps->id);
        b8((uint8_t)p->route.cell[s]);
        b8(rig_is_routing(r, s) ? KIND_ROUTING : (uint32_t)kind_of(&item, ps->index));
        b8((uint32_t)rig_param_count(r, s));
        btext(item.name);
        btext(type_of(ps->menu, &item));
        buf[count_at]++;
    }
    reply(m->cmd, buf, at);
}

/* parameter k of slot: 0 = written */
static int param(struct rig *r, int slot, int k)
{
    struct rig_param p;
    if (rig_param(r, slot, k, &p) != 0)
        return -1;
    int list = p.synced ? 11 : param_list_size(&p.def);
    if (!fits(2 + 20 + 2 + 24 + 25 + 1 + (unsigned)list * 24))
        return -1;
    b8(p.def.idx);
    b8((p.synced ? P_SYNCED : 0) | (rig_scene_param(r, slot, k) ? P_SCENE : 0) | (list ? P_LIST : 0));
    bf32(p.def.min);
    bf32(p.def.max);
    bf32(p.def.step);
    bf32(p.def.def);
    bf32(p.value);
    btext(p.def.name);
    btext(p.text);
    b8((uint32_t)list);
    for (int i = 0; i < list; i++)
        btext(p.synced ? param_sync_divisions[i] : param_list_item(&p.def, i));
    return 0;
}

static void block(const struct msg *m)
{
    struct rig *r = srv.rig;
    int slot = (int)msg_word(m, 2);
    at = 0;
    b32((uint32_t)slot);
    if (slot < 0 || slot >= PRESET_SLOTS || !rig_slot_used(r, slot)) {
        b32((uint32_t)-10);
        reply(m->cmd, buf, at);
        return;
    }
    b32(0);
    unsigned count_at = at;
    b8(0);
    int n = rig_param_count(r, slot);
    for (int k = 0; k < n; k++)
        if (param(r, slot, k) == 0)
            buf[count_at]++;
    reply(m->cmd, buf, at);
}

static void catalog(const struct msg *m)
{
    const struct catalog *c = &srv.rig->cat;
    int menu = (int)msg_word(m, 2), first = (int)msg_word(m, 3);
    int count = menu >= 0 && menu < c->lib.menus ? catalog_count(c, menu) : 0;
    at = 0;
    b32((uint32_t)menu);
    b32((uint32_t)count);
    b32((uint32_t)first);
    unsigned n_at = at;
    b8(0);
    for (int k = first < 0 ? 0 : first; k < count && buf[n_at] < 255; k++) {
        struct catalog_item item;
        if (catalog_item(c, menu, k, &item) != 0)
            continue;
        const char *type = type_of(menu, &item);
        if (!fits(2 + 4 + 1 + 1 + (unsigned)strlen(item.name) + 1 + (unsigned)strlen(type)))
            break;
        b16((uint32_t)k);
        b32(item.id);
        b8((uint32_t)kind_of(&item, k));
        btext(item.name);
        btext(type);
        buf[n_at]++;
    }
    reply(m->cmd, buf, at);
}

static int remove_slot(struct rig *r, int slot)
{
    for (int pos = 0; pos < r->preset.count; pos++)
        if (r->preset.order[pos] == slot)
            return rig_remove(r, pos);
    return -1;
}

static void edit(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t op = msg_word(m, 2);
    int a = (int)msg_word(m, 3), b = (int)msg_word(m, 4), c = (int)msg_word(m, 5);
    float v = 24 <= m->len ? rdf32(m->data + 24) : 0.0f;
    int res = -21;
    switch (op) {
    case EDIT_PARAM:
        if (a >= 0 && a < PRESET_SLOTS && rig_slot_used(r, a) && b >= 0 && b < rig_param_count(r, a)) {
            rig_param_set(r, a, b, v);
            res = 0;
        }
        break;
    case EDIT_ON:
        if (a >= 0 && a < PRESET_SLOTS && rig_slot_used(r, a)) {
            rig_set_on(r, a, b != 0);
            res = 0;
        }
        break;
    case EDIT_PLACE: res = rig_place(r, a, b, c); break;
    case EDIT_MOVE: res = a >= 0 && a < PRESET_SLOTS ? rig_move_cell(r, a, b) : -21; break;
    case EDIT_REMOVE: res = a >= 0 && a < PRESET_SLOTS ? remove_slot(r, a) : -21; break;
    case EDIT_REPLACE: res = a >= 0 && a < PRESET_SLOTS ? rig_replace(r, a, b, c) : -21; break;
    case EDIT_ROUTE: res = a >= 0 && a < ROUTE_MODES ? rig_route(r, a, b, c) : -21; break;
    case EDIT_JOINT:
        if (a >= 0 && a < JOINT_PARAMS) {
            rig_joint(r, a, b);
            res = 0;
        }
        break;
    case EDIT_SCENE: res = rig_scene(r, a); break;
    case EDIT_VOLUME:
        rig_set_volume(r, a);
        res = 0;
        break;
    case EDIT_BPM:
        if (a >= 40 && a <= 300) {
            rig_set_bpm(r, a);
            res = 0;
        }
        break;
    case EDIT_SAVE: res = a >= 0 && a < PRESET_COUNT ? rig_save(r, a) : -21; break;
    case EDIT_SCENE_PICK:
        if (a >= 0 && a < SCENE_COUNT) {
            r->scene_chosen = 1;
            res = rig_scene(r, a);
        }
        break;
    case EDIT_SCENE_COPY:
        if (a >= 0 && a < SCENE_COUNT && b >= 0 && b < SCENE_COUNT && a != b) {
            rig_scene_copy(r, a, b);
            res = 0;
        }
        break;
    case EDIT_SCENE_REVERT: res = a >= 0 && a < SCENE_COUNT ? rig_scene_revert(r, a) : -21; break;
    case EDIT_SCENE_NAME:
        if (a >= 0 && a < SCENE_COUNT && m->len >= 29) {
            char name[SCENE_NAME_LEN + 1];
            edit_text(m, name, SCENE_NAME_LEN);
            rig_scene_set_name(r, a, name);
            res = 0;
        }
        break;
    case EDIT_NAME:
        if (m->len >= 29) {
            char name[PRESET_NAME_LEN + 1];
            edit_text(m, name, PRESET_NAME_LEN);
            rig_set_name(r, name);
            res = 0;
        }
        break;
    case EDIT_SCENE_PARAM:
        res = a >= 0 && a < PRESET_SLOTS && rig_slot_used(r, a) && b >= 0 && b < rig_param_count(r, a)
                  ? rig_scene_param_toggle(r, a, b) : -21;
        break;
    default: break;
    }
    at = 0;
    b32(op);
    b32((uint32_t)res);
    b32(r->gen);
    if (op == EDIT_PARAM && res == 0)
        param(r, a, b);
    reply(m->cmd, buf, at);
}

void msg_waveod_rig(const struct msg *m)
{
    switch (m->cmd) {
    case CMD_WV_RIG: rig_state(m); break;
    case CMD_WV_BLOCK: block(m); break;
    case CMD_WV_CATALOG: catalog(m); break;
    case CMD_WV_EDIT: edit(m); break;
    default: break;
    }
}
