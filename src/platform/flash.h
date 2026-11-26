/*
 * The 8 MB QSPI flash.
 *
 * On the pedal the flash is memory mapped (FlexSPI XIP at 0x60000000); the simulator maps a file image.
 * Erasing and programming are only allowed in the data areas: the user IR file system and the record store
 * [FLASH_WRITABLE_START, FLASH_SIZE), and waveod's effect library and store followed by the Clone file system
 * [FLASH_LOW_START, FLASH_LOW_END). Everything else holds the bootloader, this application, the effects and the factory IRs and
 * presets, and the platform refuses to touch it.
 *
 * While an erase or program runs the flash cannot be read, so nothing may read it from an interrupt then
 * (the caller keeps the audio engine suspended). Both calls block until the flash is ready again.
 */
#pragma once

#include <stdint.h>

#define FLASH_SIZE            0x800000u
#define FLASH_SECTOR_SIZE     0x1000u
#define FLASH_PAGE_SIZE       0x100u
#define FLASH_WRITABLE_START  0x753000u        /* user IR file system, then the record store */
#define FLASH_LOW_START     0x2A0000u        /* waveod's effect library (FLASH_WV_LIBRARY), its store (FLASH_WV_STORE),
                                                then the Clone file system */
#define FLASH_LOW_END       0x440000u

/* 1 when [offset, offset + len) lies in one of the areas above */
static inline int flash_writable(uint32_t offset, uint32_t len)
{
    if (offset >= FLASH_WRITABLE_START && offset <= FLASH_SIZE && len <= FLASH_SIZE - offset)
        return 1;
    return offset >= FLASH_LOW_START && offset <= FLASH_LOW_END && len <= FLASH_LOW_END - offset;
}

/* Pointer to `offset` in the flash, or 0 when [offset, offset + len) is out of range. */
const uint8_t *flash_ptr(uint32_t offset, uint32_t len);

/* Address of `offset` as seen by the effect modules (Cab/IR/Clone read their data through this). */
uint32_t flash_device_addr(uint32_t offset);

/* Erase the 4 KB sector at `offset` (sector aligned). 0 = done, -1 = refused or failed. */
int flash_erase_sector(uint32_t offset);

/* Program `len` bytes (NOR: bits only go from 1 to 0). 0 = done and verified, -1 = refused or failed. */
int flash_program(uint32_t offset, const void *data, uint32_t len);

/* Diagnostics text (several lines): controller set-up, flash id, last error. */
void flash_diag(char *out, unsigned size);

/* The chip's JEDEC id and its three status registers: as the chip came up (read first thing at start-up, before
   the lock) and as last read (after the lock).
   Status 1: BUSY WEL BP0-2 TB SEC SRP; 2: SRL QE - LB1-3 CMP SUS; 3: - - WPS - - DRV0-1 - (Winbond 25Q). */
struct flash_status {
    uint32_t jedec;                     /* 0 = not read */
    int16_t sr[3];                      /* at start-up; -1 = not read */
    int16_t sr_now[3];                  /* last read */
    uint8_t lock;                       /* enum flash_lock */
};
void flash_status(struct flash_status *out);

/* Status registers 1-3 read now (the lock test's checks); -1 where a read failed. */
void flash_read_status(int sr[3]);

/*
 * Write protection in the chip itself, against code that drives the flash controller directly (an effect
 * module runs privileged): `protect` = status register 1's BP0-2 / TB / SEC bits, written volatile (50h) with
 * status register 2's SRL (power supply lock-down) after it, so nothing can take the protection away until the
 * power goes; then the chip is back to its own (unprotected) state. Only for a W25Q64 found as the pedal has it
 * (nothing protected, QE on); anything else is left alone. A warm restart keeps the lock (it reads back as on).
 */
enum flash_lock { FLASH_LOCK_NONE, FLASH_LOCK_ON, FLASH_LOCK_FAILED, FLASH_LOCK_UNSUPPORTED };
#define FLASH_PROTECT_BOOT  0x24u       /* TB + BP0: 0x000000-0x01FFFF, the bootloader (128 KB) */
#define FLASH_PROTECT_TOP4K 0x44u       /* SEC + BP0: 0x7FF000-0x7FFFFF (the lock test's sector) */
int flash_lock(uint8_t protect);        /* enum flash_lock */
int flash_lock_state(void);
