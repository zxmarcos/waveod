/*
 * Group 0xF of the editor protocol, waveod's own (the original dispatches nothing there): what waveod Studio
 * (the PC app for this firmware) uses. Requests and answers are ordinary messages {u32 cmd, u32 n, payload};
 * words are little endian, an answer carries the request's cmd.
 *
 *   0x1F000000 flash   {u32 offset, u32 length <= WV_CHUNK}      -> {offset, length, the bytes} (length 0: out of
 *                                                                     the 8 MB); read only (tools/flashdump.py)
 *   0x1F000001 info    {}                                        -> "WVOD", u16 protocol, u16 presets, u16 active,
 *                                                                     u8 modified, u8 0, u32 store sectors, used,
 *                                                                     free, live bytes, char version[24]
 *   0x1F000002 list    {}                                        -> u16 presets, u16 active, then per preset
 *                                                                     {u8 origin, u8 place, char name[17]}
 *   0x1F000003 get     {u32 index (WV_EDITED: the edited preset)} -> {u32 index, i32 origin, u32 n, the preset in
 *                                                                     waveod's format (fw/wv_preset.h), its
 *                                                                     effects resolved like rig_load does}
 *   0x1F000004 put     {u32 index, u32 n, the preset}            -> {u32 index, i32 error}; the active preset is
 *                                                                     loaded again when it was written
 *   0x1F000005 load    {u32 index}                               -> {u32 index, i32 0}
 *   0x1F000006 move    {u32 from, u32 to}                        -> {u32 from, u32 to, i32 error}
 *   0x1F000007 copy    {u32 from, u32 to}                        -> {u32 from, u32 to, i32 error}
 *   0x1F000008 describe {u32 index (WV_EDITED: the edited one)}  -> {u32 index, i32 origin, then the control table
 *                                                                     as its chunk (fw/wv_preset.h: u8 11, u16 n,
 *                                                                     body; the converted one when the preset has
 *                                                                     none), u8 blocks, per used block in chain
 *                                                                     order {u8 slot, i8 menu, u8 n, name, u8
 *                                                                     parameters, per parameter (library order)
 *                                                                     {u8 knob, u8 n, name, f32 min, max, step}}}
 *                                                                     (routing blocks left out)
 *   0x1F000009 global  {}                                        -> the global control table as its chunk (as
 *                                                                     describe's; fw/presets.c)
 *   0x1F00000A global put {u32 n, the chunk}                     -> {i32 error}; stored and played at once (the
 *                                                                     links that may not be global left out)
 *   0x1F00000B latency {u32 route (engine/latency.h; ~0 cancels)} -> {i32 error}; the test runs on its own
 *   0x1F00000C latency result {}                                 -> {u32 seq, u8 status, u8 route, u8 runs done,
 *                                                                     i8 level dBFS, f32 min, max, mean (samples),
 *                                                                     f32 match, u16 runs, u16 buffers' samples,
 *                                                                     u32 sample rate}
 *   0x1F00000D library {}                                        -> {u32 flash offset, u32 capacity, u32 total
 *                                                                     (0: none), u32 CRC}; the PC reads it with
 *                                                                     flash (docs/blocks.md, fw/wv_library.h)
 *   0x1F00000E library write {u32 offset, u32 n <= 4096, bytes}  -> {u32 offset, i32 error}; sector by sector
 *                                                                     from 0 (the effects of the library vanish
 *                                                                     from the catalog until the commit)
 *   0x1F00000F library commit {u32 total}                        -> {i32 error}; checked, read again, the edited
 *                                                                     preset's effects found again, the chain
 *                                                                     rebuilt (its edits kept)
 *   0x1F000010 clones  {}                                        -> 20 x 40 bytes (10 before the 20 slots of
 *                                                                     fw/ir_store.h IR_CLONE_SLOTS: the count is the
 *                                                                     length), one per Clone slot: {u8 kind
 *                                                                     (enum wv_clone), u8 named (stored with a
 *                                                                     name), u16 0, f32 sample rate, f32 loudness,
 *                                                                     u32 weights' CRC, u32 flags, char name[20]};
 *                                                                     rate .. flags of a NAM capture only
 *                                                                     (docs/nam.md)
 *   0x1F000011 drums   {}                                        -> MEUS RITMOS (model/drum_user.h): {u8 places,
 *                                                                     u8 sounds in the bank, u16 events at most,
 *                                                                     u8 place playing (0xFF: the bank's pattern),
 *                                                                     u8 playing, u16 0, then per place {u8 used,
 *                                                                     u8 flags, u16 BPM, u16 events, char name[16]}}
 *   0x1F000012 drum get {u32 place}                              -> {u32 place, u32 n, the record (n = 0: empty)}
 *   0x1F000013 drum put {u32 place, u32 n, the record}           -> {u32 place, i32 error}; n = 0 empties it; the
 *                                                                     place playing plays the new one
 *   0x1F000014 drum play {u32 place (~0: the bank's), u32 on}    -> {i32 error}; chosen, started (on) or stopped
 *   0x1F000015..18 the chain screen (protocol 8, wv_rig_msgs.c); 0x1F000019..1A the GLOBAL screen's settings
 *   (protocol 9, wv_settings_msgs.c); 0x1F00001B..1D an effect's cost on the CPU (protocol 11, wv_bench_msgs.c)
 *   0x1F00001E flash status {}                                   -> {u32 JEDEC id (0: not read), i16 status 1,
 *                                                                     i16 status 2, i16 status 3 (-1: not read) as
 *                                                                     the chip came up, u8 enum flash_lock, u8 the
 *                                                                     header's size (20; protocol 13: 0 = 12 bytes,
 *                                                                     the registers as last read), i16 x 3 the
 *                                                                     registers as last read (after the lock),
 *                                                                     u16 0}; then the diagnostics text
 *                                                                     (platform/flash.h), NUL ended
 *   0x1F00001F lock test {u32 'LOCK'}  (WAVEOD_LOCK_TEST builds) -> {i32 enum flash_lock (as the start-up left
 *                                                                     it), i32 erase, i32 program (flash_erase_sector
 *                                                                     / flash_program, -2: not tried), u32 first
 *                                                                     changed byte (~0: none), i32 restored (0 not
 *                                                                     needed, 1 done, -1 failed), u32 the program
 *                                                                     test's offset (~0: no erased spot), then 8 x
 *                                                                     {u8 status 1, 2, 3} read now}: the top 4 KB
 *                                                                     (locked at start-up) copied to RAM, erased and
 *                                                                     programmed, compared, put back if it changed
 *   0x1F000020 controls {u32 buttons held (BTN_x), i32 turn[4] (detents for MENU, K1, K2, K3), i32 touch
 *                        (-1 the panel's, 0 up, 1 down), i32 x, i32 y, u16 pot[3] (MASTER, EXP1, EXP2: 0..4095,
 *                        0xFFFF the pedal's), u16 0}         -> {i32 0}; laid over the physical controls
 *                                                                     (input/remote.h) until the next one or 2 s
 *                                                                     without any
 *   0x1F000021 screen  {u32 first row, u32 rows}                 -> {u32 first row, u32 rows, the pixels row by
 *                                                                     row, u32 0x00RRGGBB each}; at most 4 rows a
 *                                                                     message (the picture last shown)
 *
 * origin: enum preset_origin (0 user, 1 factory, 2 empty); place: enum preset_place (0 waveod's record, 1 the
 * original's preset, 2 empty). Errors are presets_store's.
 */
#include "editor/server.h"
#include "engine/audio_engine.h"
#include "engine/drums.h"
#include "engine/latency.h"
#include "fw/bytes.h"
#include "fw/crc32.h"
#include "fw/ir_fs.h"
#include "fw/library.h"
#include "nam/capture.h"
#include "fw/flash_map.h"
#include "fw/wv_library.h"
#include "fw/wv_preset.h"
#include "fw/wv_store.h"
#include "input/remote.h"
#include "model/drum_user.h"
#include "platform/audio.h"
#include "platform/display.h"
#include "platform/flash.h"
#include "platform/sdram.h"
#include "ui/ui.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#ifndef WAVEOD_VERSION
#define WAVEOD_VERSION "dev"
#endif

#define PROTOCOL 15                        /* 2: describe, 3: the global control table, 4: latency, 5: library,
                                               6: Clone slots, 7: MEUS RITMOS, 8: the chain screen (wv_rig_msgs.c),
                                               9: the GLOBAL screen's settings (wv_settings_msgs.c), 10: the scene
                                               menu's edits (wv_rig_msgs.c), 11: an effect's cost on the CPU
                                               (wv_bench_msgs.c), 12: the edited preset's name (wv_rig_msgs.c),
                                               13: the flash chip's status registers, 14: their start-up values
                                               and the lock, 15: the controls from the computer and the
                                               display's picture */

/* what a Clone slot holds (0x1F000010) */
enum wv_clone { WV_CLONE_EMPTY, WV_CLONE_ORIGINAL, WV_CLONE_NAM, WV_CLONE_NAM_48K, WV_CLONE_NAM_BROKEN };
#define CLONE_ENTRY 40

static uint8_t out[16 + WV_CHUNK] SDRAM_BSS;
static struct preset tmp SDRAM_BSS;

static void w16(unsigned at, uint32_t v)
{
    out[at] = (uint8_t)v;
    out[at + 1] = (uint8_t)(v >> 8);
}

static void w32(unsigned at, uint32_t v)
{
    w16(at, v & 0xffff);
    w16(at + 2, v >> 16);
}

static void flash_read(const struct msg *m)
{
    uint32_t off = msg_word(m, 2), len = msg_word(m, 3);
    if (len > WV_CHUNK)
        len = WV_CHUNK;
    const uint8_t *p = flash_ptr(off, len);
    if (!p)
        len = 0;
    w32(0, off);
    w32(4, len);
    if (len)
        memcpy(out + 8, p, len);
    reply(m->cmd, out, 8 + len);
}

static void flash_status_msg(const struct msg *m)
{
    struct flash_status st;
    flash_status(&st);
    memset(out, 0, 20);
    w32(0, st.jedec);
    for (int i = 0; i < 3; i++) {
        w16(4 + 2 * (unsigned)i, (uint16_t)st.sr[i]);
        w16(12 + 2 * (unsigned)i, (uint16_t)st.sr_now[i]);
    }
    out[10] = st.lock;
    out[11] = 20;
    flash_diag((char *)out + 20, 512);
    reply(m->cmd, out, 20 + (uint32_t)strlen((char *)out + 20) + 1);
}

#ifdef WAVEOD_LOCK_TEST
/* The flash lock on the record store's last sector (0x7FF000), locked at start-up (board.c) like the bootloader
   will be: copied to RAM first, so whatever happens it is put back when the protection did not hold. Until the
   power goes the sector stays read-only; a store write that lands there fails before it commits, the old copy
   stays. */
#define LOCK_TEST_SECTOR 0x7ff000u
#define LOCK_TEST_READS  8
static uint8_t lock_copy[FLASH_SECTOR_SIZE] SDRAM_BSS;

static void lock_test(const struct msg *m)
{
    int32_t state = flash_lock_state(), erase = -2, prog = -2, restored = 0;
    uint32_t diff = ~0u, spot = ~0u;
    memset(out, 0, 24 + 3 * LOCK_TEST_READS);
    if (msg_word(m, 2) == 0x4b434f4cu) {                    /* "LOCK" */
        for (int k = 0; k < LOCK_TEST_READS; k++) {         /* how the registers read after pins_init */
            int sr[3];
            flash_read_status(sr);
            for (int i = 0; i < 3; i++)
                out[24 + 3 * k + i] = (uint8_t)sr[i];
        }
        engine_suspend();                                   /* nothing may read the flash while it is written */
        memcpy(lock_copy, flash_ptr(LOCK_TEST_SECTOR, FLASH_SECTOR_SIZE), FLASH_SECTOR_SIZE);
        /* the program test needs bytes still erased: the last 16 0xFF bytes in a row from the end */
        for (uint32_t at = FLASH_SECTOR_SIZE - 16; at + 16 > 16 && spot == ~0u; at -= 16) {
            uint32_t k = 0;
            while (k < 16 && lock_copy[at + k] == 0xff)
                k++;
            if (k == 16)
                spot = at;
        }
        if (state == FLASH_LOCK_ON) {
            erase = flash_erase_sector(LOCK_TEST_SECTOR);
            if (spot != ~0u) {
                static const uint8_t zeros[16];
                prog = flash_program(LOCK_TEST_SECTOR + spot, zeros, sizeof zeros);
            }
            const uint8_t *p = flash_ptr(LOCK_TEST_SECTOR, FLASH_SECTOR_SIZE);
            for (uint32_t i = 0; i < FLASH_SECTOR_SIZE && diff == ~0u; i++)
                if (p[i] != lock_copy[i])
                    diff = i;
            if (diff != ~0u)                                /* not protected after all: the sector back */
                restored = flash_erase_sector(LOCK_TEST_SECTOR) == 0 &&
                           flash_program(LOCK_TEST_SECTOR, lock_copy, FLASH_SECTOR_SIZE) == 0 ? 1 : -1;
        }
        engine_resume();
    }
    w32(0, (uint32_t)state);
    w32(4, (uint32_t)erase);
    w32(8, (uint32_t)prog);
    w32(12, diff);
    w32(16, (uint32_t)restored);
    w32(20, spot);
    reply(m->cmd, out, 24 + 3 * LOCK_TEST_READS);
}
#endif

static void info(const struct msg *m)
{
    struct rig *r = srv.rig;
    struct wvs_stats s;
    wvs_stats(&s);
    memset(out, 0, 48);
    memcpy(out, "WVOD", 4);
    w16(4, PROTOCOL);
    w16(6, PRESET_COUNT);
    w16(8, (uint32_t)r->index);
    out[10] = r->dirty;
    w32(12, s.sectors);
    w32(16, s.used);
    w32(20, s.free);
    w32(24, s.live_bytes);
    strncpy((char *)out + 28, WAVEOD_VERSION, 23);
    reply(m->cmd, out, 52);
}

static void list(const struct msg *m)
{
    struct rig *r = srv.rig;
    w16(0, PRESET_COUNT);
    w16(2, (uint32_t)r->index);
    for (int i = 0; i < PRESET_COUNT; i++) {
        uint8_t *e = out + 4 + 19 * i;
        char name[PRESET_NAME_LEN + 1];
        e[0] = (uint8_t)presets_origin(&r->src, i);
        e[1] = (uint8_t)presets_place(&r->src, i);
        presets_name(&r->src, i, name);
        memset(e + 2, 0, PRESET_NAME_LEN);
        memcpy(e + 2, name, strlen(name));
    }
    reply(m->cmd, out, 4 + 19 * PRESET_COUNT);
}

static void get(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t index = msg_word(m, 2);
    int origin;
    const struct preset *p = &tmp;
    if (index == WV_EDITED) {
        p = &r->preset;
        origin = (int)r->origin;
    } else if (index < PRESET_COUNT) {
        origin = (int)presets_load(&r->src, (int)index, &tmp);
        rig_resolve(r, &tmp);                       /* the blocks as the pedal plays them */
    } else {
        origin = -1;
    }
    int n = origin < 0 ? 0 : wvp_encode(p, out + 12, WV_CHUNK);
    if (n < 0) {
        origin = -1;
        n = 0;
    }
    w32(0, index);
    w32(4, (uint32_t)origin);
    w32(8, (uint32_t)n);
    reply(m->cmd, out, 12 + (uint32_t)n);
}

static unsigned text(unsigned at, const char *s)
{
    unsigned n = (unsigned)strlen(s);
    if (n > 255)
        n = 255;
    out[at] = (uint8_t)n;
    memcpy(out + at + 1, s, n);
    return at + 1 + n;
}

static unsigned wf32(unsigned at, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    w32(at, u);
    return at + 4;
}

static int routing(const struct preset_slot *s)
{
    return s->menu == MENU_EQ && (s->id == ID_SPLIT || s->id == ID_PATH_B || s->id == ID_MERGE);
}

/* the control table and the names of the blocks and parameters it points at */
static void describe(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t index = msg_word(m, 2);
    const struct preset *p = &tmp;
    int origin;
    if (index == WV_EDITED) {
        p = &r->preset;
        origin = (int)r->origin;
    } else if (index < PRESET_COUNT) {
        origin = (int)presets_load(&r->src, (int)index, &tmp);
        rig_resolve(r, &tmp);
    } else {
        w32(0, index);
        w32(4, (uint32_t)-1);
        reply(m->cmd, out, 8);
        return;
    }
    w32(0, index);
    w32(4, (uint32_t)origin);
    int n = wvp_encode_controls(&p->controls, out + 8, WV_CHUNK / 2);
    unsigned at = 8 + (unsigned)(n < 0 ? 0 : n), count_at = at++;
    out[count_at] = 0;
    for (int pos = 0; pos < p->count; pos++) {
        int slot = p->order[pos];
        const struct preset_slot *s = slot >= 0 && slot < PRESET_SLOTS ? &p->slot[slot] : 0;
        struct catalog_item item;
        if (!s || s->menu < 0 || routing(s) || catalog_item(&r->cat, s->menu, s->index, &item) != 0)
            continue;
        if (at + 2 + 24 + 1 + LIB_MAX_PARAMS * (2 + 24 + 12) > WV_CHUNK)
            break;
        out[at] = (uint8_t)slot;
        out[at + 1] = (uint8_t)s->menu;
        at = text(at + 2, item.name);
        unsigned np_at = at++;
        out[np_at] = 0;
        for (int k = 0; k < item.nparams && k < LIB_MAX_PARAMS; k++) {     /* the entry's, then its block's */
            struct lib_param d;
            if (catalog_param(&item, k, &d) != 0)
                continue;
            out[at] = d.idx;
            at = text(at + 1, d.name ? d.name : "");
            at = wf32(at, d.min);
            at = wf32(at, d.max);
            at = wf32(at, d.step);
            out[np_at]++;
        }
        out[count_at]++;
    }
    reply(m->cmd, out, at);
}

static void global_get(const struct msg *m)
{
    int n = wvp_encode_controls(&srv.rig->global, out, WV_CHUNK);
    reply(m->cmd, out, (uint32_t)(n < 0 ? 0 : n));
}

static void global_put(const struct msg *m)
{
    static struct preset_controls c SDRAM_BSS;
    uint32_t n = msg_word(m, 2);
    int err = -10;
    if (12 + n <= m->len) {
        err = wvp_decode_controls(m->data + 12, n, &c) == 0 ? 0 : -12;
        if (!err) {
            srv.rig->global = c;
            err = rig_store_global(srv.rig);
        }
    }
    w32(0, (uint32_t)err);
    reply(m->cmd, out, 4);
}

static void latency(const struct msg *m)
{
    uint32_t route = msg_word(m, 2);
    int err = 0;
    if (route == 0xFFFFFFFFu)
        latency_cancel();
    else if (route < LAT_ROUTES)
        latency_start((int)route);
    else
        err = -10;
    w32(0, (uint32_t)err);
    reply(m->cmd, out, 4);
}

static void latency_result(const struct msg *m)
{
    struct latency_result r;
    latency_get(&r);
    w32(0, r.seq);
    out[4] = r.status;
    out[5] = r.route;
    out[6] = r.runs;
    out[7] = (uint8_t)r.level_db;
    wf32(8, r.min);
    wf32(12, r.max);
    wf32(16, r.mean);
    wf32(20, r.match);
    w16(24, LAT_RUNS);
    w16(26, LAT_BUFFERS);
    w32(28, AUDIO_RATE);
    reply(m->cmd, out, 32);
}

static void library_info(const struct msg *m)
{
    const struct library *l = &srv.rig->cat.wvl;
    w32(0, FLASH_WV_LIBRARY);
    w32(4, FLASH_WV_LIBRARY_SIZE);
    w32(8, l->base ? l->size : 0);
    w32(12, l->base ? rd32(l->base + 0x10) : 0);
    reply(m->cmd, out, 16);
}

static void library_write(const struct msg *m)
{
    uint32_t off = msg_word(m, 2), n = msg_word(m, 3);
    int err = -10;
    if (16 + n <= m->len) {
        if (off == 0)
            rig_library_writing(srv.rig);
        engine_suspend();                           /* nothing may read the flash while it is written */
        err = wvlib_write(off, m->data + 16, n) == 0 ? 0 : -11;
        engine_resume();
    }
    w32(0, off);
    w32(4, (uint32_t)err);
    reply(m->cmd, out, 8);
}

static void library_commit(const struct msg *m)
{
    engine_suspend();
    int err = wvlib_commit(msg_word(m, 2)) == 0 ? 0 : -11;
    engine_resume();
    rig_library_changed(srv.rig);                   /* failed: no library, the effects in it missing */
    w32(0, (uint32_t)err);
    reply(m->cmd, out, 4);
}

static void clones(const struct msg *m)
{
    const struct catalog *cat = &srv.rig->cat;
    memset(out, 0, IR_CLONE_SLOTS * CLONE_ENTRY);
    for (int i = 0; i < IR_CLONE_SLOTS; i++) {
        uint8_t *e = out + i * CLONE_ENTRY;
        uint32_t off = catalog_data(cat, MENU_CLONE, i);
        const uint8_t *p = off ? flash_ptr(off, IRFS_CLONE_DATA) : 0;
        const struct nam_capture *h = (const struct nam_capture *)p;
        if (!p) {
            e[0] = WV_CLONE_EMPTY;
        } else if (!nam_capture_is(p, IRFS_CLONE_DATA)) {
            e[0] = WV_CLONE_ORIGINAL;
        } else {
            e[0] = crc32(0, p + NAM_CAPTURE_HEADER, A2_NUM_WEIGHTS * 4) != h->crc ? WV_CLONE_NAM_BROKEN :
                   nam_capture_resampled(h) ? WV_CLONE_NAM_48K : WV_CLONE_NAM;
            memcpy(e + 4, &h->sample_rate, 4);
            memcpy(e + 8, &h->loudness, 4);
            memcpy(e + 12, &h->crc, 4);
            memcpy(e + 16, &h->flags, 4);
        }
        char name[IR_FS_NAME_LEN + 1];
        e[1] = ir_fs_name(1, i, name) == 0 && name[0];
        struct catalog_item item;
        if (catalog_item(cat, MENU_CLONE, i, &item) == 0)
            strncpy((char *)e + 20, item.name, 19);
    }
    reply(m->cmd, out, IR_CLONE_SLOTS * CLONE_ENTRY);
}

static void answer(uint32_t cmd, int words, uint32_t a, uint32_t b, int err);

static void drums(const struct msg *m)
{
    memset(out, 0, 8 + DRUM_USER_SLOTS * 22);
    out[0] = DRUM_USER_SLOTS;
    out[1] = (uint8_t)drums_samples();
    w16(2, DRUM_USER_EVENTS);
    int sel = drum_user_selected();
    out[4] = (uint8_t)(sel < 0 ? 0xff : sel);
    out[5] = (uint8_t)drums_playing();
    for (int i = 0; i < DRUM_USER_SLOTS; i++) {
        uint8_t *e = out + 8 + 22 * i;
        struct drum_user_info info;
        if (drum_user_info(i, &info) != 0)
            continue;
        e[0] = 1;
        e[1] = info.flags;
        e[2] = (uint8_t)info.bpm;
        e[3] = (uint8_t)(info.bpm >> 8);
        e[4] = (uint8_t)info.count;
        e[5] = (uint8_t)(info.count >> 8);
        memcpy(e + 6, info.name, DRUM_USER_NAME);
    }
    reply(m->cmd, out, 8 + DRUM_USER_SLOTS * 22);
}

static void drum_get(const struct msg *m)
{
    uint32_t place = msg_word(m, 2);
    unsigned n = 0;
    const uint8_t *p = drum_user_record((int)place, &n);
    if (!p || n > WV_CHUNK)
        n = 0;
    w32(0, place);
    w32(4, n);
    if (n)
        memcpy(out + 8, p, n);
    reply(m->cmd, out, 8 + n);
}

static void drum_put(const struct msg *m)
{
    uint32_t place = msg_word(m, 2), n = msg_word(m, 3);
    int err = -10;
    if (16 + n <= m->len) {
        int flash = engine_flash_begin();       /* the audio plays on: nothing in it reads the flash */
        err = drum_user_store((int)place, m->data + 16, n);
        engine_flash_end(flash);
        if (!err)
            drum_user_changed((int)place);
    }
    answer(m->cmd, 2, place, 0, err);
}

static void drum_play(const struct msg *m)
{
    uint32_t place = msg_word(m, 2), on = msg_word(m, 3);
    int err = 0;
    if (place == 0xFFFFFFFFu)
        drum_user_deselect();
    else if (drum_user_select((int)place) != 0)
        err = -10;
    if (!err)
        drums_play(on != 0);
    w32(0, (uint32_t)err);
    reply(m->cmd, out, 4);
}

static void answer(uint32_t cmd, int words, uint32_t a, uint32_t b, int err)
{
    w32(0, a);
    if (words == 3)
        w32(4, b);
    w32(4 * (words - 1), (uint32_t)err);
    reply(cmd, out, 4u * (uint32_t)words);
}

static void put(const struct msg *m)
{
    struct rig *r = srv.rig;
    uint32_t index = msg_word(m, 2), n = msg_word(m, 3);
    int err = -10;
    if (index < PRESET_COUNT && 16 + n <= m->len) {
        err = wvp_decode(m->data + 16, n, &tmp) == 0 ? 0 : -12;
        if (!err) {
            int flash = engine_flash_begin();   /* the audio plays on: nothing in it reads the flash */
            err = presets_store(&r->src, (int)index, &tmp);
            engine_flash_end(flash);
        }
        if (!err && (int)index == r->index)
            rig_load(r, (int)index);
    }
    answer(m->cmd, 2, index, 0, err);
}

static void move_or_copy(const struct msg *m, int move)
{
    struct rig *r = srv.rig;
    int from = (int)msg_word(m, 2), to = (int)msg_word(m, 3), cur = r->index, err = -10;
    if (from >= 0 && from < PRESET_COUNT && to >= 0 && to < PRESET_COUNT) {
        if (!move && from == cur && r->dirty)
            rig_save(r, cur);                       /* like the editor's copy: the edits go with it */
        int flash = engine_flash_begin();       /* the audio plays on: nothing in it reads the flash */
        err = move ? presets_move(&r->src, from, to) : presets_copy(&r->src, from, to);
        engine_flash_end(flash);
        if (!err && move) {                         /* the active preset keeps being the same preset */
            if (cur == from)
                cur = to;
            else if (from < cur && cur <= to)
                cur--;
            else if (to <= cur && cur < from)
                cur++;
            r->index = cur;
        } else if (!err && to == cur) {
            rig_load(r, cur);
        }
    }
    answer(m->cmd, 3, (uint32_t)from, (uint32_t)to, err);
}

static void controls(const struct msg *m)
{
    int32_t turn[ENC_COUNT];
    for (int e = 0; e < ENC_COUNT; e++)
        turn[e] = (int32_t)msg_word(m, 3 + e);
    uint16_t pot[POT_COUNT];
    for (int i = 0; i < POT_COUNT; i++)
        pot[i] = (uint16_t)(msg_word(m, 10 + i / 2) >> (16 * (i & 1)));
    remote_set(msg_word(m, 2), turn, (int32_t)msg_word(m, 7), (int32_t)msg_word(m, 8), (int32_t)msg_word(m, 9),
               pot);
    w32(0, 0);
    reply(m->cmd, out, 4);
}

#define SCREEN_ROWS 4                       /* 4 x 480 x 4 = 7680 bytes <= WV_CHUNK */

static void screen(const struct msg *m)
{
    uint32_t y = msg_word(m, 2), n = msg_word(m, 3);
    if (y > DISPLAY_H)
        y = DISPLAY_H;
    if (n > SCREEN_ROWS)
        n = SCREEN_ROWS;
    if (n > DISPLAY_H - y)
        n = DISPLAY_H - y;
    w32(0, y);
    w32(4, n);
    memcpy(out + 8, ui_screen() + y * DISPLAY_W, n * DISPLAY_W * 4);
    reply(m->cmd, out, 8 + n * DISPLAY_W * 4);
}

void msg_waveod(const struct msg *m){
    switch (m->cmd) {
    case CMD_WV_FLASH: flash_read(m); break;
    case CMD_WV_FLASH_STATUS: flash_status_msg(m); break;
#ifdef WAVEOD_LOCK_TEST
    case CMD_WV_LOCK_TEST: lock_test(m); break;
#endif
    case CMD_WV_INFO: info(m); break;
    case CMD_WV_LIST: list(m); break;
    case CMD_WV_GET: get(m); break;
    case CMD_WV_PUT: put(m); break;
    case CMD_WV_LOAD:
        if (msg_word(m, 2) < PRESET_COUNT)
            rig_load(srv.rig, (int)msg_word(m, 2));
        answer(m->cmd, 2, msg_word(m, 2), 0, 0);
        break;
    case CMD_WV_MOVE: move_or_copy(m, 1); break;
    case CMD_WV_COPY: move_or_copy(m, 0); break;
    case CMD_WV_DESCRIBE: describe(m); break;
    case CMD_WV_GLOBAL: global_get(m); break;
    case CMD_WV_GLOBAL_PUT: global_put(m); break;
    case CMD_WV_LATENCY: latency(m); break;
    case CMD_WV_LATENCY_GET: latency_result(m); break;
    case CMD_WV_LIB_INFO: library_info(m); break;
    case CMD_WV_LIB_WRITE: library_write(m); break;
    case CMD_WV_LIB_COMMIT: library_commit(m); break;
    case CMD_WV_CLONES: clones(m); break;
    case CMD_WV_DRUMS: drums(m); break;
    case CMD_WV_DRUM_GET: drum_get(m); break;
    case CMD_WV_DRUM_PUT: drum_put(m); break;
    case CMD_WV_DRUM_PLAY: drum_play(m); break;
    case CMD_WV_CONTROLS: controls(m); break;
    case CMD_WV_SCREEN: screen(m); break;
    case CMD_WV_SETTINGS:
    case CMD_WV_SETTINGS_PUT: msg_waveod_settings(m); break;
    case CMD_WV_BENCH_ELF:
    case CMD_WV_BENCH:
    case CMD_WV_PROFILE: msg_waveod_bench(m); break;
    default: msg_waveod_rig(m); break;
    }
}
