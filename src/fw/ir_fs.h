/*
 * Writing the user IR and Clone file systems (fw/ir_store.h reads them) exactly as the original does, so
 * each firmware reads what the other stored:
 *   mount   FUN_000255D4 (user IRs), FUN_00024C28 (Clone): current directory sector of each part, free blocks
 *   write   FUN_00025A84 / FUN_00025D30: the data goes to a free block first; then the entry is written in
 *           place when its position was never used ('_' then 'Z'), otherwise the directory sector is copied
 *           to a spare one with the new entry (header 0x5AFA) and the old sector marked 0x5AF8
 *   rename  FUN_000252D8 / FUN_00024D80: a copy of the directory sector with the new name
 *   delete  FUN_00024974 (Clone): the entry's marker cleared
 * The RAM state (free blocks, current sectors) is kept between calls like the original's: a deleted capture's
 * block only counts as free again after the next start.
 *
 * Blocks while the flash is written: suspend the audio first. 0 = done, -1 = refused or failed.
 */
#pragma once

#include <stdint.h>

enum irfs_kind { IRFS_USER, IRFS_CLONE };

#define IRFS_USER_DATA    0x1000u           /* bytes of one user IR (4096 / 3 = 1365 samples of 24 bits) */
#define IRFS_CLONE_DATA   0x2000u

void irfs_mount(void);

/* store data (up to the kind's size) and name (17 characters) in slot (0..19 user IRs, 0..9 Clone) */
int irfs_write(enum irfs_kind kind, int slot, const char *name, const uint8_t *data, uint32_t len);

int irfs_rename(enum irfs_kind kind, int slot, const char *name);

int irfs_delete_clone(int slot);
