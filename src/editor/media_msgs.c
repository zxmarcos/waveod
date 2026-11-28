/*
 * Groups 4 and 8 of the editor protocol: user IRs and Clone captures (and the system group's file transfer,
 * 0x0500000A..C, which the original uses for the same data).
 *   0x14000000 / 0x18000000  names of the 20 user IRs / 10 Clone slots (FUN_000265B4 / FUN_00024A18): the name
 *                            a slot was stored with, else the IR directory's ("User IR 1", "Clone 1" ...)
 *   0x14000001 / 0x18000001  data {slot, 0, ., ., ., name[17] at +0x14, data at +0x28 (4096 / 8192 bytes)}:
 *                            stored (fw/ir_fs.h), answered 0x14000003 / 0x18000003 {slot, accepted, 0}
 *   0x14000002 / 0x18000002  rename {slot, name[17]}
 *                            (Clone slots 0..19: waveod's 20, fw/ir_store.h IR_CLONE_SLOTS; the names message and the
 *                            delete stay the original's 10)
 *   0x11000001 sub 0x9000 | n  delete Clone capture n (FUN_0002D344)
 * The file transfer (0x0500000A..C, unused by the editor) is refused.
 */
#include "editor/server.h"
#include "engine/audio_engine.h"
#include "fw/ir_fs.h"
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define NAME_FIELD  17

static void slot_name(int clone, int n, int dir_index, uint8_t out[NAME_FIELD])
{
    char name[IR_FS_NAME_LEN + 1];
    memset(out, 0, NAME_FIELD);
    if (ir_fs_name(clone, n, name) == 0 || ir_dir_entry(&srv.rig->cat.irs, dir_index, 0, name) == 0)
        memcpy(out, name, strlen(name) < NAME_FIELD ? strlen(name) : NAME_FIELD);
    else
        memcpy(out, "Invalid", 7);
}

static void ir_names(void)
{
    static uint8_t p[500] SDRAM_BSS;
    const struct ir_dir *d = &srv.rig->cat.irs;
    memset(p, 0, sizeof p);
    for (int i = 0; i < IR_USER_COUNT; i++)
        slot_name(0, i, IR_DIR_USER_FIRST(d) + i, p + 160 + NAME_FIELD * i);
    reply(CMD_IR, p, sizeof p);
}

static void clone_names(void)
{
    static uint8_t p[0xfc] SDRAM_BSS;
    const struct ir_dir *d = &srv.rig->cat.irs;
    memset(p, 0, sizeof p);
    for (int i = 0; i < IR_CLONE_COUNT; i++)
        slot_name(1, i, IR_DIR_CLONE_FIRST(d) + i, p + 80 + NAME_FIELD * i);
    reply(CMD_CLONE, p, sizeof p);
}

static void text_at(const struct msg *m, uint32_t at, char out[18])
{
    for (int i = 0; i < 17; i++)
        out[i] = (char)msg_byte(m, at + (uint32_t)i);
    out[17] = 0;
}

/* FUN_0003A084 groups 4 / 8, sub 1: accepted when word 3 is 0 (stored when the slot exists) */
static void store(const struct msg *m, enum irfs_kind kind)
{
    uint32_t slot = msg_word(m, 2), ok = 0, size = kind == IRFS_CLONE ? IRFS_CLONE_DATA : IRFS_USER_DATA;
    if (msg_word(m, 3) == 0) {
        ok = 1;
        if (slot < (kind == IRFS_CLONE ? IR_CLONE_SLOTS : IR_USER_COUNT)) {
            char name[18];
            text_at(m, 0x14, name);
            uint32_t n = m->len > 0x28 ? m->len - 0x28 : 0;
            int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
            irfs_write(kind, (int)slot, name, m->data + 0x28, n < size ? n : size);
            engine_flash_end(flash);
            rig_rebuild(srv.rig);
        }
    }
    uint32_t a[3] = { slot, ok, 0 };
    reply(kind == IRFS_CLONE ? 0x18000003u : 0x14000003u, a, sizeof a);
}

static void rename_slot(const struct msg *m, enum irfs_kind kind)
{
    uint32_t slot = kind == IRFS_CLONE ? (msg_word(m, 2) & 0xffff) : msg_word(m, 2);
    if (slot >= (kind == IRFS_CLONE ? IR_CLONE_SLOTS : IR_USER_COUNT))
        return;
    char name[18];
    text_at(m, 0x0c, name);
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    irfs_rename(kind, (int)slot, name);
    engine_flash_end(flash);
}

/* FUN_0002D344: Clone capture n deleted, the chain reloaded, the request told back {sub | n, flag} */
void media_delete_clone(uint32_t sub, uint32_t n, int flag)
{
    if (n > 9)
        return;
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    irfs_delete_clone((int)n);
    engine_flash_end(flash);
    rig_rebuild(srv.rig);
    notify_words(CMD_GLOBAL_SUB, 2, sub | n, (uint32_t)flag);
}

void msg_media(const struct msg *m)
{
    uint32_t group = (m->cmd & 0x0fffffffu) >> 24, sub = m->cmd & 0xf;
    if (group == 4) {
        if (sub == 0 && m->flag == FLAG_REQUEST)
            ir_names();
        else if (sub == 1)
            store(m, IRFS_USER);
        else if (sub == 2)
            rename_slot(m, IRFS_USER);
    } else if (group == 8) {
        if (sub == 0 && m->flag == FLAG_REQUEST)
            clone_names();
        else if (sub == 1)
            store(m, IRFS_CLONE);
        else if (sub == 2)
            rename_slot(m, IRFS_CLONE);
    } else if (group == 5) {                /* file transfer: {u16 tag, ...} -> 0x0500000D {tag, 0 = refused} */
        uint16_t r[2] = { (uint16_t)(m->data[8] | m->data[9] << 8), 0 };
        uint32_t a[2] = { 0x0500000Du, 4 };
        uint8_t buf[12];
        memcpy(buf, a, 8);
        memcpy(buf + 8, r, 4);
        reply_raw(buf, sizeof buf);
    }
}
