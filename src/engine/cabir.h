/*
 * waveod's own Cab / User IR engine, in place of the original's (library entries 5.0 / 6.0, CabIrInit /
 * CabIrProcess), which only runs in blocks of 64 frames. Same sound, any block size up to CABIR_BLOCK.
 *
 * What the original does (tools/check_cab.py compares the two):
 *   - IR: 1024 int32 (24-bit, scale 2^-23) read from the flash (factory cabs 0x700000 + block, user IRs in
 *     their file system), left channel of the input only, the result on both outputs;
 *   - convolution by FFT, 16 partitions of 64 (CMSIS arm_rfft 128);
 *   - level = 0.27 x VOLUME (knob 1) / 100, moving towards the knob by 0.002 per block (plus 2.27e-4 of the
 *     distance), constant over a block;
 *   - the level limited to +-2.75441, then HIGH CUT (knob 6, 2000..20001 Hz) and LOW CUT (knob 5, 19..2000 Hz):
 *     Cookbook low / high pass with w = f x 1.4247585e-4 and alpha = sin w x 0.70721358, direct form I,
 *     recomputed when the knob changes (then below 20 Hz the low cut is 19, above 20 kHz the high cut 20001).
 * Here: uniformly partitioned overlap-save with partitions of the block size (dsp/rfft.h), the same filters.
 *
 * And a virtual mic (engine/cabmic.h, knobs 8-11): the IR is recomputed in the main loop (cabir_poll) into the
 * spare set of spectra, which the interrupt crossfades to over one block. At its neutral settings (0, as older
 * presets have) the IR is the flash data as it is.
 */
#pragma once

#include <stdint.h>

#define CABIR_LEN       1024                    /* IR samples */
#define CABIR_BLOCK     64                      /* largest block (= partition) */
#define CABIR_MIN_BLOCK 16
#define CABIR_SPEC      (CABIR_LEN / CABIR_MIN_BLOCK * (CABIR_MIN_BLOCK + 1) * 2)   /* floats of all partitions */

enum { CABIR_KNOB_VOLUME = 1, CABIR_KNOB_LOW_CUT = 5, CABIR_KNOB_HIGH_CUT = 6 };

struct cabir {
    const int32_t *ir;                          /* the flash data (0 = silence, as the original with no data) */
    int block, parts, head;                     /* partition size, count, newest input spectrum */
    volatile uint8_t cur;                       /* the spectra in use (h[cur]) */
    volatile uint8_t pending;                   /* main loop -> interrupt: h[!cur] is ready, switch to it */
    volatile uint8_t fading;                    /* interrupt: crossfading from h[!cur] this block */
    float mic[4];                               /* mic knobs the IR was made for (the type as its index) */
    float gain;                                 /* level now (0.27 x volume / 100 at the target) */
    float seen_low, seen_high;                  /* cut knobs the filters were designed for */
    float lo[6], hi[6];                         /* b0 b1 b2 a0 a1 a2 */
    float lo_s[4], hi_s[4];                     /* x1 x2 y1 y2 */
    float in[2 * CABIR_BLOCK];                  /* the last two blocks of input */
    float irf[CABIR_LEN];                       /* the IR in use (x 2^-23, through the mic) */
    float h[2][CABIR_SPEC];                     /* IR spectra, partition p at p x (block + 1) x 2 */
    float x[CABIR_SPEC];                        /* input spectra, a ring of `parts` */
};

/* Cab and User IR blocks on this engine (1, the default) or on the original's module (0), from the next
   chain_build (engine/chain.c). */
void cabir_set_in_use(int on);
int cabir_in_use(void);

/* Set up (Init): knobs = the slot's knob words, ir = 1024 int32 or 0. The partitions follow `block`. */
void cabir_init(struct cabir *c, const float *knobs, const int32_t *ir, int block);

/* Main loop: when the mic knobs moved, make the new IR and hand it to the interrupt. 1 = done (0: unchanged, or
   the last change is still being taken). */
int cabir_poll(struct cabir *c, const float *knobs);

/* One block in place (interleaved stereo io): the left input through the IR, on both outputs. A block size
   other than the partitions' rebuilds them (the history is lost). Nothing happens when !enabled. */
void cabir_process(struct cabir *c, float *io, int frames, const float *knobs, int enabled);
