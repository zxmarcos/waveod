/*
 * waveod's effect library in the flash (FLASH_WV_LIBRARY, docs/blocks.md): blocks built as modules
 * (tools/wvfx.py), in the container of the original's library (fw/library.h) so the same reader takes it:
 *
 *   +0x00 "WVLB"   +0x04 0x18 + 4 * 16   +0x08 index size   +0x0C total size
 *   +0x10 CRC-32 of the bytes from 0x18 to the total size    +0x14 16 menus   +0x18 list offset[16]
 *
 * Entries as the original's, with the module ELF (a "WVFX" header pointing at the block's descriptor); in a
 * parameter record the first field is an offset into the strings: the options ("Off\0On\0\0") when f1 has
 * LIB_P_OPTIONS, the unit template when it has LIB_P_UNIT, 0xFFFF for none.
 *
 * Nothing there, or anything that fails the checks, is an empty library.
 */
#pragma once

#include "fw/library.h"

#define WVLIB_MAGIC 0x424C5657u                 /* "WVLB" */

/* Open the library in the flash; 0 = there is one (lib->wv set), -1 = none (lib empty). */
int wvlib_open(struct library *lib);

/* The same checks on an image anywhere (a library on its way to the flash). */
int wvlib_check(const uint8_t *img, uint32_t size);

/*
 * Writing a new library (the PC app, editor/wv_msgs.c): wvlib_write takes the image a sector at a time, from
 * offset 0 up (offset sector aligned, n <= FLASH_SECTOR_SIZE): each sector is erased, then programmed. The first
 * 0x18 bytes (the header) are kept back, so until wvlib_commit the flash holds no library at all; wvlib_commit
 * programs them and checks the whole image (`total` bytes). Nothing may read the library while it is written
 * (the caller empties its catalog's copy and keeps the audio engine suspended around each call).
 * 0 = done, -1 = refused (out of the area, out of order) or the flash failed.
 */
int wvlib_write(uint32_t offset, const uint8_t *data, uint32_t n);
int wvlib_commit(uint32_t total);
