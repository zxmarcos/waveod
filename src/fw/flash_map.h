/*
 * Layout of the QME-200 QSPI flash (V1.2.2), as used by the original application.
 *
 * Everything below 0x20000 belongs to the bootloader and the application image starts at 0x20000; neither
 * is touched here. The data areas are shared with the original firmware, so they are read with exactly
 * its formats: going back to the original firmware keeps every preset, IR and capture.
 *
 * waveod's own store sits at the end of the application partition, where no update writes (the original's
 * app ends at 0x3A0004, the patched research builds at 0x3A2180; tools/mkfw.py keeps waveod's image below it).
 */
#pragma once

#define FLASH_APP             0x20000u    /* the application section (firmware section 0) */
#define FLASH_WV_LIBRARY      0x2A0000u   /* waveod's effect library (fw/wv_library.h), past waveod's image (the
                                             original's image reaches over it: going back to it loses the library) */
#define FLASH_WV_LIBRARY_SIZE 0x103000u   /* up to the preset store */
#define FLASH_WV_STORE        0x3A3000u   /* waveod's presets (fw/wv_store.c), up to the Clone file system */
#define FLASH_WV_STORE_SIZE   0x67000u

#define FLASH_CLONE_FS        0x40A000u   /* 10 Clone captures (own file system), 0x36000 bytes */
#define FLASH_CLONE_FS_SIZE   0x36000u
#define FLASH_FACTORY_BANK    0x440000u   /* 120 factory presets, 3 per 4 KB sector */
#define FLASH_FACTORY_COUNT   120
#define FLASH_LIBRARY         0x480000u   /* effect library (firmware section 1) */
#define FLASH_LIBRARY_MAX     0x280000u
#define FLASH_IR_LIBRARY      0x700000u   /* cabinet IR directory + data (firmware section 2) */
#define FLASH_USER_IR_FS      0x753000u   /* 20 user IRs (own file system), 0x2D000 bytes */
#define FLASH_USER_IR_FS_SIZE 0x2D000u
#define FLASH_RECORD_STORE    0x780000u   /* global settings, preset map and user presets */
#define FLASH_RECORD_STORE_SIZE 0x80000u
