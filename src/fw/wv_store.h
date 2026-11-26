/*
 * waveod's own store (flash 0x3A3000-0x40A000, FLASH_WV_STORE): a log of records in 4 KB sectors, separate from
 * the original's record store. No update writes there: the bootloader erases only the sectors a section's data
 * reaches, the original's app (and the patched ones) end below 0x3A3000 and tools/mkfw.py refuses a waveod image
 * that would reach it (research/NOTES.md, "Dump da flash").
 *
 *   sector  "WVS1" u32 seq u32 ~seq u32 0xFFFFFFFF, then records
 *   record  u16 0x5257 "WR", u8 type, u8 0, u16 key, u16 length, u32 seq, u32 CRC-32 (of the 12 bytes before it and
 *           the payload), the payload; each record starts on a 16-byte boundary and stays inside its sector
 *
 * The newest record (highest seq) of a (type, key) wins. Records are only appended; when the free sectors run
 * low the oldest sector's live records are copied to the head and the sector erased. A record cut by a power
 * failure fails its CRC and closes its sector (nothing more is appended there). Sectors that hold anything
 * else (what earlier firmware left) are erased the first time they are needed.
 *
 * Writing blocks while the flash is busy: suspend the audio first (nothing may read the flash meanwhile).
 */
#pragma once

#include <stdint.h>

#define WVS_TYPES       3
#define WVS_MAP         0               /* key 0: the preset map (fw/presets.c) */
#define WVS_GLOBAL_CONTROLS 1           /* type WVS_MAP, key 1: the global control table (fw/presets.c) */
#define WVS_PRESET      1               /* key = preset record number (fw/wv_preset.h) */
#define WVS_DRUM        2               /* key = place of a drum pattern of waveod's (model/drum_user.h) */
#define WVS_KEYS        512             /* keys per type */
#define WVS_PAYLOAD_MAX 4048            /* sector size - sector header - record header - alignment */

/* Scan the area (read only). */
void wvs_open(void);

/* Payload of the newest record of (type, key) and its length, or 0. */
const uint8_t *wvs_get(int type, int key, unsigned *len);

/* Append a record. 0 = written and verified, -1 = bad arguments, -2 = no room, -3 = flash failure. */
int wvs_put(int type, int key, const void *data, unsigned len);

/* The record of (type, key) is no longer needed: the next clean-up drops it. */
void wvs_forget(int type, int key);

/* Statistics for the diagnostics and the tests. */
struct wvs_stats {
    unsigned sectors, used, free, live_bytes;
};
void wvs_stats(struct wvs_stats *out);
