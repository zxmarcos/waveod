/*
 * CPU overload guard of the original audio loop (FUN_00021D28): a block that takes longer than 1.25 ms (of the
 * 1.451 ms a 64-frame block lasts at 44.1 kHz) counts, any faster block clears the count, and the third slow
 * block in a row trips the guard. The original then bypasses all 12 slots (FUN_00022514) and shows its
 * "System overload" screen.
 */
#pragma once

#include <stdint.h>

#define OVERLOAD_MS     1.25f
#define OVERLOAD_BLOCKS 3

struct overload_guard {
    uint8_t slow;                   /* slow blocks in a row */
};

/* Account one block that took `ms`; 1 when it trips the guard (the count then starts again). */
int overload_block(struct overload_guard *g, float ms);
