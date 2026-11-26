/*
 * Group 5 of the editor protocol: the system table 0x42DC8 (cmd & 0xF):
 *   1 save the edited preset as preset n (FUN_00030328)      2 activate preset n (FUN_00030318)
 *   3 move a preset in the list (FUN_000301E0)               4 store the list (FUN_00030300)
 *   5 delete a preset (FUN_0003015C)                         6 copy a preset over another (FUN_000300E8)
 *   0x0A..0x0C file transfer (FUN_000305C8 / FUN_00030374 / FUN_000304B4; media_msgs.c)
 *   0x0E restart in the bootloader, for a firmware update (FUN_00030020); 0 (heartbeat) and the rest: nothing
 */
#include "editor/server.h"
#include "engine/audio_engine.h"
#include "platform/platform.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

static void ack(uint32_t what)
{
    reply(CMD_PRESET_ACK, &what, 4);
}

/* FUN_00030328: the edited preset stored as preset `pos` with the name given; the active preset stays */
static void save_as(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t pos = msg_word(m, 2);
    if (pos >= PRESET_COUNT)
        return;
    edit_name_copy(m->data + 0x0c, 0x14);  /* +0x242: 20 bytes, strcpy */
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    int err = presets_store(&r->src, (int)pos, &r->preset);
    engine_flash_end(flash);
    if (err)
        return;
    if ((int)pos == r->index)
        r->origin = PRESET_USER;
    rig_set_dirty(r, 0);
}

/* FUN_000301E0: preset `from` goes to place `to`, the others shift; the active one keeps its preset */
static void move(const struct msg *m)
{
    struct rig *r = srv.rig;
    int from = (int)msg_word(m, 2), to = (int)msg_word(m, 3), cur = r->index;
    if (from < 0 || from >= PRESET_COUNT || to < 0 || to >= PRESET_COUNT)
        return;
    if (cur == from)
        cur = to;
    else if (from < cur && cur <= to)
        cur--;
    else if (to <= cur && cur < from)
        cur++;
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    int err = presets_move(&r->src, from, to);
    engine_flash_end(flash);
    if (!err)
        r->index = cur;
}
/* FUN_0003015C: the template written over the preset */
static void delete_preset(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t pos = msg_word(m, 2);
    if (pos >= PRESET_COUNT)
        return;
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    presets_save(&r->src, (int)pos, preset_template);
    engine_flash_end(flash);
    ack(0x05000005u);
    if ((int)pos == r->index)
        rig_load(r, (int)pos);
}

/* FUN_000300E8: preset `from` copied over `to` (the edited preset saved first when it is the source) */
static void copy_preset(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t from = msg_word(m, 2), to = msg_word(m, 3);
    if (from >= PRESET_COUNT || to >= PRESET_COUNT)
        return;
    if ((int)from == r->index)
        rig_save(r, r->index);
    int flash = engine_flash_begin();     /* the audio plays on: nothing in it reads the flash */
    presets_copy(&r->src, (int)from, (int)to);
    engine_flash_end(flash);

    ack(0x05000006u);
    if ((int)to == r->index)
        rig_load(r, (int)to);
}

void msg_system(const struct msg *m)
{
    switch (m->cmd & 0xf) {
    case 0x1: save_as(m); break;
    case 0x2:
        if (msg_word(m, 3) < PRESET_COUNT)
            rig_load(srv.rig, (int)(msg_word(m, 3) & 0xffff));
        break;
    case 0x3: move(m); break;
    case 0x4:                               /* store the list: each move already stored waveod's map */
        break;
    case 0x5: delete_preset(m); break;
    case 0x6: copy_preset(m); break;
    case 0xA: case 0xB: case 0xC: msg_media(m); break;
    case 0xE:                               /* FUN_00030020: half a second, then the bootloader */
        engine_set_master(0.0f);
        plat_enter_bootloader(500);
        break;
    default: break;
    }
}
