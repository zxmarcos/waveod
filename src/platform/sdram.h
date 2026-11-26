/*
 * Zeroed buffers that do not need the OCRAM's speed (main-loop only; the OCRAM is kept for what the interrupts
 * use): SDRAM_BSS puts a static array in
 * the SDRAM (.sdram_bss in the linker script, cleared at start-up like .bss). Nothing on the PC builds.
 */
#pragma once

#if defined(__arm__)
#define SDRAM_BSS __attribute__((section(".sdram_bss")))
#else
#define SDRAM_BSS
#endif

/* One function of a file whose other code the audio runs (its set-up, say) in the SDRAM, like the files that
   include platform/sdram_code.h. */
#if defined(__arm__)
#define SDRAM_FUNC __attribute__((section(".sdram_text.func"), noinline))
#else
#define SDRAM_FUNC
#endif
