/*
 * The four memory pools effect modules are loaded into, at the addresses the original app uses (modules
 * shared between firmwares, like the parallel routing ones, rely on them):
 *
 *   region 0  code + header   ITCM   0x00058000  128 KB   (ELF addresses 0x0xxxxxxx)
 *   region 1  constants       SDRAM  0x81301000  128 KB   (0x1xxxxxxx, ER_SROM)
 *   region 2  state           OCRAM  0x20204000  192 KB   (0x2xxxxxxx, ER_IRAM)
 *   region 3  large buffers   SDRAM  0x81321000  6.8 MB   (0x3xxxxxxx, ER_SRAM)
 *
 * All modules of a preset share the pools; they are emptied before the chain is loaded again.
 * A pool has a device address (what the module sees) and a pointer the loader writes through; both are
 * the same on the pedal, the simulator backs them with host memory.
 *
 * Each pool fills from both ends (waveod's, for the spillover): side 0 from the start up, as the original, side
 * 1 from the end down. A preset is loaded into one side while the other keeps the previous preset, whose delays
 * and reverbs still ring out (engine/audio_engine.h, engine_spill).
 */
#pragma once

#include <stdint.h>

#define POOL_COUNT 4

enum pool_id { POOL_CODE, POOL_CONST, POOL_STATE, POOL_BUFFER };

struct pool {
    uint32_t addr;          /* device address */
    uint32_t size;
    uint32_t used;          /* side 0: bytes from the start */
    uint8_t *mem;           /* where the loader writes */
    uint32_t top;           /* side 1: bytes from the end */
};

extern struct pool pools[POOL_COUNT];

void pools_init(void);

/* Both sides emptied; allocations go to side 0. */
void pools_reset(void);

/* Side `side` (0 / 1) emptied, the other kept; allocations go to `side`. */
void pools_use(int side);

/* Reserve `size` bytes (8-byte aligned start) in the side in use; returns the device address or 0 when the
   pool is full (up to the other side). */
uint32_t pool_alloc(enum pool_id id, uint32_t size);

/* Pointer for a device address inside any pool (0 if outside). */
uint8_t *pool_ptr(uint32_t addr, uint32_t len);

/* What pool_alloc can still give: the bytes between the two sides. */
uint32_t pool_room(enum pool_id id);

/* Whether the side not in use holds something (the previous preset, ringing out). */
int pool_shared(enum pool_id id);
