/*
 * The cost of one effect on the pedal's CPU, in cycles (docs/bench.md): an item of the catalog (a built-in, an
 * original module, a block of waveod's library) or a module ELF sent over USB into RAM (never the flash), loaded on
 * its own into the side of the pools the running chain does not use, fed a test signal made here, every block's
 * process timed with the cycle counter (DWT CYCCNT), interrupts off around it. The audio is suspended while it runs
 * (silence) and the preset playing comes back as it was (its pools' side is not touched).
 *
 * The test signal is the same on the pedal and in the emulator: the output's CRC tells that both computed the same.
 */
#pragma once

#include "engine/chain.h"
#include <stdint.h>

#define BENCH_ELF_MAX    0x10000u        /* the largest module ELF sent over USB (stock: 23 KB, waveod's: 33 KB) */
#define BENCH_MAX_BLOCKS 2000            /* blocks measured at most (2.9 s of audio) */

enum bench_source { BENCH_CATALOG = 0, BENCH_ELF = 1 };

enum bench_flags {
    BENCH_COLD = 1,                     /* the data cache cleaned and invalidated before every block */
    BENCH_BYPASS = 2,                   /* the slot off (the cost of its bypass) */
};

enum bench_error {
    BENCH_BAD_REQUEST = -10,
    BENCH_NO_ELF = -11,                 /* source BENCH_ELF without a complete upload */
};

struct bench_request {
    uint32_t source;                    /* enum bench_source */
    int32_t menu;                       /* enum lib_menu */
    int32_t index;                      /* catalog index; with an ELF, the menu index its header gets */
    uint32_t blocks;                    /* measured, 1..BENCH_MAX_BLOCKS */
    uint32_t skip;                      /* blocks run before them, not measured (lines filling, glides settling) */
    uint32_t flags;                     /* enum bench_flags */
    float level;                        /* the test signal's peak (full scale 1.0) */
    uint32_t seed;                      /* its notes and noise */
    uint16_t bpm;
    float knobs[PRESET_KNOBS];          /* NaN: the catalog item's default (then 0) */
};

struct bench_result {
    int32_t status;                     /* enum module_error of the load, or enum bench_error */
    uint32_t load_cycles;               /* load, relocation and init */
    uint32_t overhead;                  /* an empty measurement, taken off every block */
    uint32_t blocks;
    uint32_t min, median, p99, max;
    float mean;
    uint32_t crc;                       /* CRC-32 of every block's output and FX send (skipped ones too) */
    float peak;                         /* the largest output sample */
    uint32_t pool_used[4];              /* bytes of each pool (code, constants, state, buffers) the slot took */
    uint32_t core_hz;
    float knobs[PRESET_KNOBS];          /* what the slot got */
    uint32_t *cycles;                   /* per measured block (bench_run's own array) */
};

/* The ELF upload (sources BENCH_ELF): bytes at `offset` of a module of `total` bytes. 0, or BENCH_BAD_REQUEST. */
int bench_elf_write(uint32_t offset, uint32_t total, const uint8_t *data, uint32_t n);

/* Main loop: one measurement; `running` = the chain playing (its side of the pools is kept). */
void bench_run(const struct bench_request *rq, const struct catalog *cat, const struct chain *running,
               uint32_t core_hz, struct bench_result *out);
