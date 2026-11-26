/*
 * Effect module loader, compatible with the original app (FUN_0004029c / FUN_00040510 / FUN_000405c8).
 *
 * A module is an ARM ELF (ET_EXEC, built by Keil armlink or by the research SDK) with no imports. Its
 * sections live in four address regions selected by the top nibble of the address (see pools.h):
 *   - PROGBITS, alloc+exec (or alloc only) at 0x00000000 / 0x10000000: code + header / constants;
 *   - PROGBITS or NOBITS, alloc+write, named ER_IRAM* / ER_SRAM*: state / buffers (NOBITS are zeroed).
 * Each region is copied into its pool at the same offset it has in the ELF, then the SHT_REL relocations
 * are applied with the region base of the symbol (S) and of the place (P).
 *
 * The module header starts the code region:
 *   +0x00 written by the host: 0xFFAA0000 | menu index   +0x14 id   +0x1C Init   +0x20 Process
 *   +0x24 magic 0xFFAA55AA
 */
#pragma once

#include <stdint.h>

#define MODULE_MAGIC 0xFFAA55AAu

enum module_error {
    MODULE_OK = 0,
    MODULE_BAD_ELF = -1,
    MODULE_NO_MEMORY = -2,          /* the original app reports this as "System overload" (flag 2) */
    MODULE_BAD_RELOC = -3,
    MODULE_BAD_HEADER = -4,
    MODULE_INIT_FAILED = -5,        /* a block of waveod's refused its set-up (sdk/wvfx.h init) */
};

struct module {
    uint32_t header;                /* device addresses */
    uint32_t init;
    uint32_t process;
    uint32_t desc;                  /* a block of waveod's (header "WVFX", sdk/wvfx.h): its descriptor, else 0 */
    uint32_t id;
    uint32_t region[4];             /* base of each region, 0 when unused */
    uint32_t region_size[4];
    unsigned relocs;
};

int module_load(const uint8_t *elf, uint32_t elf_size, uint16_t menu_index, struct module *out);
