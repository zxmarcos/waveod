/*
 * Words of the original app's RAM that effect modules read or write. Modules built for the stock firmware
 * find them at fixed addresses, so this firmware keeps them there (the linker script reserves the window
 * 0x20234000..0x20242000 of the OCRAM for them):
 *
 *   0x20234025  u8      flipped at the start of every audio block (Split / Path B / Merge use it to know
 *                       that the Split ran earlier in the same block)
 *   0x20238198  float   duration of the last audio block in ms (read by the NAM Clone CPU meter)
 *   0x202382A0  u32     CPU meter word published by the NAM Clone module (field << 28 | microseconds)
 *   0x20241F44  u32[12] header address of the module loaded in each slot (0 = none)
 */
#pragma once

#include <stdint.h>

#ifdef WAVEOD_HOST
struct host_abi_mirror {
    uint8_t block_toggle;
    float block_ms;
    uint32_t cpu_word;
    uint32_t module_header[12];
};
extern struct host_abi_mirror host_abi;
#define ABI_BLOCK_TOGGLE   (host_abi.block_toggle)
#define ABI_BLOCK_MS       (host_abi.block_ms)
#define ABI_CPU_WORD       (host_abi.cpu_word)
#define ABI_MODULE_HEADER  (host_abi.module_header)
#else
#define ABI_BLOCK_TOGGLE   (*(volatile uint8_t *)0x20234025u)
#define ABI_BLOCK_MS       (*(volatile float *)0x20238198u)
#define ABI_CPU_WORD       (*(volatile uint32_t *)0x202382A0u)
#define ABI_MODULE_HEADER  ((volatile uint32_t *)0x20241F44u)
#endif
