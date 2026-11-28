/*
 * Internals of the editor protocol server (editor.h): the dispatcher (server.c) and the message groups,
 * after the original's FUN_0003A084 (group = cmd bits 24-27, sub = cmd & 0xF):
 *   0  presets: edited preset, stored presets (export / import), the 0x0100xxxx..0x010Axxxx sub-addresses
 *   1  global settings (block and single parameters)         3  drum machine
 *   4  user IRs                    5  system: save / move / delete / copy presets, file transfer, bootloader
 *   6  global EQ                   8  Clone captures
 *   F  waveod's own: waveod Studio's presets, the flash (wv_msgs.c)
 */
#pragma once

#include "editor/editor.h"
#include <stdint.h>

#define CMD_PRESET          0x10000000u
#define CMD_PRESET_SUB      0x10000001u
#define CMD_PRESET_PORT     0x10000002u     /* export (request) / import (data) */
#define CMD_GLOBAL          0x11000000u
#define CMD_GLOBAL_SUB      0x11000001u
#define CMD_DRUM            0x13000000u
#define CMD_IR              0x14000000u
#define CMD_EQ              0x16000000u
#define CMD_PRESET_ACK      0x17000000u
#define CMD_CLONE           0x18000000u
/* waveod's own group (wv_msgs.c) */
#define CMD_WV_FLASH        0x1F000000u
#define CMD_WV_INFO         0x1F000001u
#define CMD_WV_LIST         0x1F000002u
#define CMD_WV_GET          0x1F000003u
#define CMD_WV_PUT          0x1F000004u
#define CMD_WV_LOAD         0x1F000005u
#define CMD_WV_MOVE         0x1F000006u
#define CMD_WV_COPY         0x1F000007u
#define CMD_WV_DESCRIBE     0x1F000008u
#define CMD_WV_GLOBAL       0x1F000009u
#define CMD_WV_GLOBAL_PUT   0x1F00000Au
#define CMD_WV_LATENCY      0x1F00000Bu
#define CMD_WV_LATENCY_GET  0x1F00000Cu
#define CMD_WV_LIB_INFO     0x1F00000Du
#define CMD_WV_LIB_WRITE    0x1F00000Eu
#define CMD_WV_LIB_COMMIT   0x1F00000Fu
#define CMD_WV_CLONES       0x1F000010u
#define CMD_WV_DRUMS        0x1F000011u
#define CMD_WV_DRUM_GET     0x1F000012u
#define CMD_WV_DRUM_PUT     0x1F000013u
#define CMD_WV_DRUM_PLAY    0x1F000014u
/* protocol 8: the chain screen's state and edits (wv_rig_msgs.c) */
#define CMD_WV_RIG          0x1F000015u
#define CMD_WV_BLOCK        0x1F000016u
#define CMD_WV_CATALOG      0x1F000017u
#define CMD_WV_EDIT         0x1F000018u
/* protocol 9: the GLOBAL screen's settings (wv_settings_msgs.c) */
#define CMD_WV_SETTINGS     0x1F000019u
#define CMD_WV_SETTINGS_PUT 0x1F00001Au
/* protocol 11: an effect's cost on the CPU (wv_bench_msgs.c) */
#define CMD_WV_BENCH_ELF    0x1F00001Bu
#define CMD_WV_BENCH        0x1F00001Cu
#define CMD_WV_PROFILE      0x1F00001Du
/* protocol 13: the flash chip's id and status registers (wv_msgs.c) */
#define CMD_WV_FLASH_STATUS 0x1F00001Eu
#define CMD_WV_LOCK_TEST    0x1F00001Fu     /* test builds only (WAVEOD_LOCK_TEST) */
/* protocol 15: the controls driven from the computer, the picture on the display (wv_msgs.c) */
#define CMD_WV_CONTROLS     0x1F000020u
#define CMD_WV_SCREEN       0x1F000021u
#define WV_CHUNK            0x2000u         /* the largest flash read; room for any answer */
#define WV_EDITED           0xFFFFu         /* get: the edited preset */

#define FLAG_REQUEST        0x11
#define FLAG_DATA           0x12

/* one message of a transfer, its payload decompressed (`data` starts with the cmd and the payload length,
   like the original's work buffer, so handlers index words the same way) */
struct msg {
    uint32_t cmd;
    const uint8_t *data;        /* {cmd, n, payload...} */
    uint32_t len;               /* bytes in data */
    uint32_t total;             /* the original's second argument: the transfer's length for group 5, the
                                   decompressed payload + 8 for the others */
    int flag;
};

struct server {
    struct rig *rig;
    struct globals *globals;
    struct settings *settings;      /* waveod's own (model/settings.h) */
    float master;
    uint8_t run_mode;               /* +0xBDF: kept for the editor, not used */
    uint8_t overload;               /* +0x231: the "ailments" flag the editor reads once */

};

extern struct server srv;

static inline uint32_t msg_word(const struct msg *m, int k)
{
    uint32_t v = 0;
    if ((uint32_t)(4 * k + 4) <= m->len)
        for (int i = 3; i >= 0; i--)
            v = v << 8 | m->data[4 * k + i];
    return v;
}

static inline uint8_t msg_byte(const struct msg *m, uint32_t at)
{
    return at < m->len ? m->data[at] : 0;
}

/* FUN_00030B38: compressed message {cmd, n, payload} */
void reply(uint32_t cmd, const void *payload, uint32_t n);
/* FUN_00030A0C: a message sent as it is */
void reply_raw(const void *msg, uint32_t len);
/* FUN_0003568C: a notification; one repeating the previous (cmd, first word) waits 150 ms, the newest wins */
void notify(uint32_t cmd, const void *payload, uint32_t n);
void notify_words(uint32_t cmd, int count, ...);

/* the edited preset as a stored record (the original's edit buffer is in that layout) */
const uint8_t *edit_record(void);

/* the original copies names with strcpy (FUN_00000B6E): up to the zero, the rest of the field keeps its old
   bytes; n = most bytes taken. name_copy into any field, edit_name_copy into the edited preset's name field
   (+0x0A) without marking it modified */
void name_copy(uint8_t *field, const uint8_t *src, unsigned n);
void edit_name_copy(const uint8_t *src, unsigned n);

/* record 0 as it stands, edits included */
void globals_record(uint8_t out[GLOBALS_RECORD_SIZE]);


void msg_preset(const struct msg *m);
void msg_global(const struct msg *m);
void msg_global_eq(const struct msg *m);
void msg_drum(const struct msg *m);
void msg_system(const struct msg *m);
void msg_media(const struct msg *m);
void media_delete_clone(uint32_t sub, uint32_t n, int flag);
void msg_waveod(const struct msg *m);
void msg_waveod_rig(const struct msg *m);
void msg_waveod_settings(const struct msg *m);
void msg_waveod_bench(const struct msg *m);

/* rig events -> the original's notifications (preset_msgs.c) */
void preset_rig_event(struct rig *r, enum rig_event ev, int a, int b);
