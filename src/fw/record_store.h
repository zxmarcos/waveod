/*
 * Record store of the original firmware (flash 0x780000, 512 KB): numbered records grouped by sector,
 * rewritten by copying their group to a fresh sector.
 *
 * Store header (sector 0, written by FUN_00026000):
 *   +0x00 u16 0x8F9A   +0x04 u16 1   +0x08 u32 store size   +0x0C u16 sector size   +0x0E u16 page size
 *   +0x10 u16 sector count   +0x12 u16 records per sector   +0x14 u16 sector header size   +0x16 u16 record size
 * Data sector (1 .. count-1), header of 0x1C bytes:
 *   +0x00 u16 0x8E8A in use (0x8E80 once superseded, 0xFFFF erased)   +0x04 u8 group (= record / per sector)
 *   +0x05 u8 sequence   +0x06 u8 free mask   +0x07 u8 written mask   (record k valid: bit k written, not free)
 * The newest sector of a group wins (sequence compared modulo 256).
 *
 * Records used by the app: 0 = global settings (0x238 bytes), 1 = preset -> record map (240 x i16),
 * 2 .. 299 = user presets (0x48C bytes).
 *
 * Writing follows the original exactly (FUN_00026160 mount, FUN_000262F4 write), so both firmwares keep
 * reading and writing the same store; tools/check_store.py compares the results byte for byte.
 */
#pragma once

#include <stdint.h>

#define STORE_MAX_GROUPS   128
#define STORE_MAX_SECTORS  256
#define STORE_MAX_RECORDS  300             /* the original refuses record numbers from 300 on */

struct record_store {
    const uint8_t *base;                    /* memory-mapped start, 0 = not formatted */
    uint32_t offset;                        /* flash offset */
    uint32_t sector_size;
    uint16_t sectors, per_sector, header_size, record_size;
    uint8_t group_sector[STORE_MAX_GROUPS]; /* 0 = group never written */
    uint8_t group_seq[STORE_MAX_GROUPS];

    /* writer state, valid once mounted */
    uint8_t mounted;
    uint16_t next;                          /* where the search for a free sector starts */
    uint32_t available[STORE_MAX_SECTORS / 32];
};

/* Scan the store at flash `offset` (read only); -1 when it was never formatted. */
int store_open(struct record_store *s, uint32_t offset, uint32_t size);

/* Pointer to record `n` (record_size bytes) or 0 when it is absent. */
const uint8_t *store_record(const struct record_store *s, unsigned n);

/* record_store_write.c ------------------------------------------------------------------------------- */

/* Write the store header the original writes on an unformatted area, then open it. */
int store_format(struct record_store *s, uint32_t offset, uint32_t size);

/* Prepare for writing as the original does at start-up: erase superseded and empty sectors, list the free
   ones. store_write mounts on its own when needed. */
int store_mount(struct record_store *s);

/* Write record `n` (`len` <= record_size bytes; the rest of the record stays erased). 0 = written, else the
   step that failed (the record keeps its previous copy). */
enum {
    STORE_ERR_ARGS = -1, STORE_ERR_MOUNT = -2, STORE_ERR_FULL = -3, STORE_ERR_ERASE = -4,
    STORE_ERR_COPY = -5, STORE_ERR_PROGRAM = -6, STORE_ERR_COMMIT = -7,
};
int store_write(struct record_store *s, unsigned n, const void *data, unsigned len);
