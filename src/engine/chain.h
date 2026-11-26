/*
 * The running effect chain: up to 12 slots with a loaded module each, processed in chain order on one
 * shared stereo buffer, exactly like the original audio loop (FUN_00031e18).
 *
 * Loading follows the original re-init (FUN_000204F4 mode 3): the pools are emptied, the modules are loaded
 * in slot order (not chain order) and each Init receives the slot knobs and its context word.
 */
#pragma once

#include "engine/module_loader.h"
#include "fw/catalog.h"
#include "fw/preset_format.h"

struct wvfx_desc;

struct chain_slot {
    int8_t menu;                    /* -1 = empty */
    int16_t index;                  /* catalog index in the menu */
    uint32_t id;
    int status;                     /* enum module_error of the last load */
    struct module mod;
    const struct wvfx_desc *block;  /* a block of waveod's (sdk/wvfx.h) instead of mod, with its memory: */
    void *state, *buffer;           /* in the state and buffer pools */
    uint32_t ctx;                   /* Init context word */
    float knobs[PRESET_KNOBS];      /* read by the module every block */
    uint8_t enabled;
    /* its process's cycles in the interrupt (the cycle counter; 0 on the PC), since the last chain_profile_reset */
    uint32_t cyc_blocks, cyc_last, cyc_max;
    uint64_t cyc_sum;
};

/* Gains of the split and the merge (from the preset's joint parameters) */
struct chain_joints {
    float in_a, in_b;                       /* split: balance x level */
    float a_l, a_r, b_l, b_r;               /* merge: mix x pan */
};

/* A knob word moving to `target` over `left` audio blocks (expression pedal moves without steps). The main loop
   writes `left` last; the interrupt steps it down. */
#define CHAIN_RAMPS 8
struct chain_ramp {
    int8_t slot;
    uint8_t knob;
    volatile uint8_t left;
    float target, step;
};

struct chain {
    struct chain_slot slot[PRESET_SLOTS];
    struct chain_ramp ramp[CHAIN_RAMPS];
    int8_t order[PRESET_SLOTS];
    uint8_t count;
    uint32_t beat_ms;
    /* routing (waveod's, model/route.h): blocks of PRE, lane A, lane B, POST in order */
    uint8_t mode;                           /* enum route_mode */
    uint8_t lane_n[4];
    struct chain_joints joints;
    uint8_t side;                           /* the pools' side its modules are in (engine/pools.h) */
    uint16_t skip;                          /* slots not run (bit per slot; a spillover's blocks before its tails) */
    uint16_t watch;                         /* slots whose output level is measured (the ones a tail will skip) */
    uint16_t flash_data;                    /* slots whose module reads its data from the flash itself (no room
                                               for a copy): the flash may not be written while they run */
    float watch_peak;                       /* the largest sample out of them, this block */
    volatile uint8_t profile_reset;         /* the slots' cycle counts cleared at the next block (main loop sets) */
};

/*
 * Load every slot of `p` (resolved menu/index/id/knobs already set by the caller in p) and run Init.
 * Must not run while the audio interrupt processes the chain (see engine_suspend).
 * `bpm` is the tempo the modules follow (40..300). Returns the number of slots that failed to load.
 */
int chain_build(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm);

/* The same into one side of the pools, the other side kept (a spillover runs from it). The chain has
   MODULE_NO_MEMORY in some slot when the preset did not fit beside it (chain_out_of_memory). */
int chain_build_side(struct chain *c, const struct catalog *cat, const struct preset *p, uint16_t bpm, int side);
int chain_out_of_memory(const struct chain *c);

/*
 * A spillover of `c` (the running chain) into `tail`: a copy that runs the same loaded modules, so it must take
 * their place in the interrupt at a block boundary (engine_spill). Its `skip` holds the slots before the tails:
 * a slot keeps running when a delay or reverb that is on comes before it on its path, or is it (the parallel
 * lanes each from their split; after the merge, when either lane had one). Returns 0, and nothing is to
 * spill, when no delay or reverb is on.
 */
int chain_tail(const struct chain *c, struct chain *tail);


/* Change the tempo: a single word the modules read every block (the original recomputes it every block). */
void chain_set_tempo(struct chain *c, uint16_t bpm);

/* Cabinets left out of an output (the original's "no cab" settings, FUN_00020F30): bit 0 = L, bit 1 = R. */
enum { CAB_OFF_L = 1, CAB_OFF_R = 2 };

/* The routing: mode, blocks of each lane in chain order (PRE, A, B, POST) and the joint parameters (0..100,
   enum route_joint). Takes effect at the next audio block; call with the audio suspended when the order
   changes too. */
void chain_set_route(struct chain *c, int mode, const uint8_t lane_n[4], const uint8_t joint[JOINT_PARAMS]);

/* Process one block in chain order. With a `no_cab` mask the Cab and User IR slots run after the whole chain,
   on the side that keeps them (the original's audio loop): the other side gets the signal from before the
   cabinet; with both bits set they do not run at all. With parallel lanes or dual outputs each Cab / IR runs
   in its place and the side without cabinets keeps the signal from before it. */
void chain_process(struct chain *c, float *io, float *aux, float **extra, int frames, int no_cab);

/* Knob `knob` of `slot` to `value` over `blocks` audio blocks (0 = at once; a ramp on that knob is dropped).
   From the main loop, while the audio runs. */
void chain_ramp(struct chain *c, int slot, int knob, float value, int blocks);

/* Main loop: work the audio interrupt leaves to the blocks (their poll; the Cab / IR engine's mic changes). */
void chain_poll(struct chain *c);

/* Switch every slot off (the modules stay loaded), as the original does on a CPU overload. */
void chain_bypass_all(struct chain *c);

/*
 * One slot on its own, for a measurement (engine/bench.h): `c` cleared, its slot 0 = `ps` (menu, catalog index,
 * on, knobs) loaded into side `side` of the pools (the other side, the running chain's, kept) from the catalog, or
 * from `elf` when given (the original's module or a block of waveod's built as one; `ps->index` is the menu index
 * its header gets). Writes ABI_MODULE_HEADER[0]: the caller keeps the running chain's. Returns the slot's status.
 * chain_run_slot runs slot i's process as chain_process would.
 */
int chain_load_one(struct chain *c, const struct catalog *cat, const struct preset_slot *ps, const uint8_t *elf,
                   uint32_t elf_size, uint16_t bpm, int side);
void chain_run_slot(struct chain *c, int i, float *io, float *aux, float **extra, int frames);

/* The slots' cycle counts (cyc_*) cleared from the next block the interrupt runs (main loop). */
void chain_profile_reset(struct chain *c);
