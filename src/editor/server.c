/*
 * The editor protocol's dispatcher (FUN_0003A084), its two ways of answering (FUN_00030B38 compressed,
 * FUN_0003568C notification queue) and the pending work of the main loop.
 */
#include "editor/server.h"
#include "editor/link.h"
#include "editor/lzo.h"
#include "fw/ir_fs.h"
#include "model/globals.h"
#include "platform/time.h"
#include <stdarg.h>
#include <string.h>
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define WORK_MAX            (12 * 1024)
#define NOTIFY_HOLD_MS      150             /* FUN_00035648 */
#define NOTIFY_MAX_HELD     10

struct server srv;

static struct {
    uint8_t work[WORK_MAX];
    /* FUN_0003568C's state */
    uint32_t last_cmd, last_word, cur_cmd, cur_word;
    int state;                      /* 1 = a notification is held */
    uint32_t since;
    uint16_t count;
    uint8_t held[128];
    uint32_t held_len;
    uint8_t rec[PRESET_RECORD_SIZE];
} sv SDRAM_BSS;

/* ---------------------------------------------------------------- answers */

void reply(uint32_t cmd, const void *payload, uint32_t n)
{
    link_send(cmd, payload, n);
}

void reply_raw(const void *msg, uint32_t len)
{
    link_send_raw(msg, len, LINK_DATA);
}

static void send_held(void)
{
    uint32_t cmd;
    memcpy(&cmd, sv.held, 4);
    reply(cmd, sv.held + 8, sv.held_len - 8);
    sv.count = 0;
}

void notify(uint32_t cmd, const void *payload, uint32_t n)
{
    if (n + 8 > sizeof sv.held)
        n = sizeof sv.held - 8;
    uint32_t word = 0;
    if (n >= 4)
        memcpy(&word, payload, 4);
    sv.cur_cmd = cmd;
    sv.cur_word = word;
    if (sv.last_cmd == cmd && sv.last_word == word) {      /* the same again: hold the newest */
        memcpy(sv.held, &cmd, 4);
        memcpy(sv.held + 4, &n, 4);
        memcpy(sv.held + 8, payload, n);
        sv.held_len = n + 8;
        sv.state = 1;
        sv.since = time_ms();
        sv.count++;
        return;
    }
    if (sv.state == 1)
        send_held();
    sv.last_cmd = sv.cur_cmd;
    sv.last_word = sv.cur_word;
    reply(cmd, payload, n);
    sv.count = 0;
    sv.state = 2;
}

void notify_words(uint32_t cmd, int count, ...)
{
    uint32_t w[8];
    va_list ap;
    va_start(ap, count);
    for (int i = 0; i < count && i < 8; i++)
        w[i] = va_arg(ap, uint32_t);
    va_end(ap);
    notify(cmd, w, 4u * (uint32_t)count);
}

static void notify_flush(void)
{
    if ((sv.state != 1 || time_ms() - sv.since < NOTIFY_HOLD_MS) && sv.count < NOTIFY_MAX_HELD)
        return;
    sv.since = time_ms();
    if (sv.held_len)
        send_held();
    sv.state = 2;
}

/* ---------------------------------------------------------------- shared state */

const uint8_t *edit_record(void)
{
    preset_encode(&srv.rig->preset, srv.rig->index, sv.rec);
    return sv.rec;
}

void name_copy(uint8_t *field, const uint8_t *src, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        field[i] = src[i];
        if (!src[i])
            break;
    }
}

void edit_name_copy(const uint8_t *src, unsigned n)
{
    struct preset *p = &srv.rig->preset;
    name_copy(p->raw + 0x0a, src, n);
    unsigned k = 0;                         /* the name as the record now reads (what preset_decode keeps) */
    for (; k < PRESET_NAME_LEN && p->raw[0x0a + k] && p->raw[0x0a + k] != 0xff; k++)
        p->name[k] = (char)p->raw[0x0a + k];
    while (k > 0 && p->name[k - 1] == ' ')
        k--;
    p->name[k] = 0;
    srv.rig->gen++;
}

void globals_record(uint8_t out[GLOBALS_RECORD_SIZE])
{
    globals_encode(srv.globals, srv.globals->raw, out);
}


/* ---------------------------------------------------------------- dispatch (FUN_0003A084) */

static void dispatch(const struct msg *m)
{
    switch ((m->cmd & 0x0fffffffu) >> 24) {
    case 0: msg_preset(m); break;
    case 1: msg_global(m); break;
    case 3: msg_drum(m); break;
    case 4: case 8: msg_media(m); break;
    case 5: msg_system(m); break;
    case 6: msg_global_eq(m); break;
    case 15: msg_waveod(m); break;
    default: break;                        /* 2 (the bootloader's), 7: ignored */
    }
}

static void on_transfer(const uint8_t *data, int len, int flag)
{
    int at = 0;
    while (at + 8 <= len) {
        uint32_t cmd, n;
        memcpy(&cmd, data + at, 4);
        memcpy(&n, data + at + 4, 4);
        if (n > (uint32_t)(len - at - 8))
            break;
        const uint8_t *payload = data + at + 8;
        struct msg m = { cmd, sv.work, 8, (uint32_t)len, flag };
        uint32_t group = (cmd & 0x0fffffffu) >> 24, sub = cmd & 0xf;
        uint32_t out = WORK_MAX - 8;
        if (group == 5 && sub >= 8) {       /* the file transfer messages come uncompressed */
            out = n < out ? n : out;
            memcpy(sv.work + 8, payload, out);
        } else if (n == 0 || lzo1x_decompress(payload, n, sv.work + 8, &out) != 0) {
            out = 0;                        /* the original carries on with whatever came out */
        }
        uint32_t wlen = out + 8;
        memcpy(sv.work, &cmd, 4);
        memcpy(sv.work + 4, &wlen, 4);
        m.len = wlen;
        if (group != 5)                     /* FUN_0003A084 hands groups 0..8 the decompressed size + 8 */
            m.total = wlen;
        dispatch(&m);
        at += (int)n + 8;
    }
}

void editor_init(struct rig *rig, struct globals *globals, struct settings *settings)
{
    memset(&srv, 0, sizeof srv);
    memset(&sv, 0, sizeof sv);
    srv.rig = rig;
    srv.globals = globals;
    srv.settings = settings;
    link_init(on_transfer);
    rig_listen(rig, preset_rig_event);
    irfs_mount();
}

void editor_set_master(float level)
{
    srv.master = level;
}

void editor_feed(const uint8_t *buf, int n)
{
    link_feed(buf, n);
}

void editor_poll(void)
{
    link_poll();
    notify_flush();
}
