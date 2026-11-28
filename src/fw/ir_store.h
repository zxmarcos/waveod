/*
 * Impulse responses and Clone captures, read the way the original app resolves them for the Cab (menu 5),
 * User IR (menu 6) and Clone (menu 11) engines.
 *
 * IR directory (flash 0x700000): header {u16 0x5AA5, .., u16 block size @4, .., u8 count @0xA}, then entries
 * of 0x48 bytes from +0x10: {u16 index, u16 block, u32 id, char name[16], ...}. Entries 0..count-31 are the
 * factory cabinets (data at 0x700000 + block * block size); the next 20 name the user IRs and the last 10
 * the Clone slots (their data lives in two small file systems, below).
 *
 * Small file systems (user IRs at 0x753000, Clone captures at 0x40A000): three directory sectors with a
 * 16-byte header {u16 0x5AFA, .., u16 sector size @4, ...} and fixed-size entries marked 'Z' whose u16 @2
 * is the data block.
 */
#pragma once

#include <stdint.h>

#define IR_USER_COUNT   20
#define IR_CLONE_COUNT  10                      /* the directory's Clone entries: the original's slots */
#define IR_NAME_LEN     16

/*
 * waveod's Clone slots: the original's 10, then 10 more in the same file system, entries 10..19 of its first
 * directory part. The original keeps them as they are (its mount marks their blocks used, its writes and renames
 * copy the whole part: tools/check_irfs.py) and never reads them (it stops at 10). The directory has no entry for
 * them: their ids and names continue the Clone entries' (0x0F000000 + n, "Clone n + 1").
 */
#define IR_CLONE_SLOTS  20
#define IR_CLONE_ID(n)  (0x0F000000u + (uint32_t)(n))

struct ir_dir {
    const uint8_t *base;
    uint16_t block_size;
    uint8_t count;
};

int ir_dir_open(struct ir_dir *d);

/* Factory cabinets in the directory (count - 30). */
int ir_cab_count(const struct ir_dir *d);

/* Entry `i` of the whole directory: id and name; returns -1 when out of range. */
int ir_dir_entry(const struct ir_dir *d, int i, uint32_t *id, char name[IR_NAME_LEN + 1]);

/* Directory index of `id`, or -1. */
int ir_dir_find(const struct ir_dir *d, uint32_t id);

/* Flash offsets of the data the engines read (0 = no data). */
uint32_t ir_cab_data(const struct ir_dir *d, int cab);
uint32_t ir_user_data(int n);
uint32_t ir_clone_data(const struct ir_dir *d, int n);

/* The name a user IR (clone = 0) or Clone capture (clone = 1) was stored with: 0, or -1 when the slot is empty. */
#define IR_FS_NAME_LEN  17
int ir_fs_name(int clone, int n, char name[IR_FS_NAME_LEN + 1]);

/* Directory indexes of the first user IR and the first Clone slot. */
#define IR_DIR_USER_FIRST(d)   (ir_cab_count(d))
#define IR_DIR_CLONE_FIRST(d)  (ir_cab_count(d) + IR_USER_COUNT)
