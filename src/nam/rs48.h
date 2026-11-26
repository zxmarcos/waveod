/*
 * 44.1 kHz <-> 48 kHz resampler, so that 48 kHz NAM captures (all of NAM's trainings) run at their own rate.
 *
 * One rational polyphase stage each way (160/147 and 147/160) on Kaiser-windowed sinc prototypes at 7.056 MHz:
 *   up:   5120 taps, pass 20k / stop 28k, 90 dB: 32 taps per output
 *   down: 9408 taps, pass 20k / stop 24.1k, 86 dB: 64 taps per output
 * 6336 multiply-adds per block. A tone's image is 90 dB down or more over the audible band (tests/test_nam.c),
 * the pass band flat within 0.001 dB to 20 kHz; whatever is left between 24 and 28 kHz on the way up can only
 * land above 20 kHz. Latency 1.03 ms.
 * The prototypes are symmetric, so phase Pi - 1 - p is phase p read backwards: half the phases are stored
 * (29 KB, shared by every instance, in the OCRAM: on the pedal a 160/147 stage with 53 KB of tables in the
 * SDRAM cost 225 us per block, bandwidth, not arithmetic), and the next output's row is prefetched.
 * Blocks of RS_FRAMES: the up path makes 69 or 70 samples, the down path always returns RS_FRAMES.
 */
#pragma once

#define RS_FRAMES 64                            /* 44.1 kHz frames per call */
#define RS_MAX48  70                            /* 48 kHz samples per call, at most */
#define RS_UP_K   32                            /* taps per output */
#define RS_DN_K   64

struct rs48_stage {
    float *x;                                   /* K-1 inputs of history, then this call's */
    int b, p;                                   /* next output: newest input index (relative to this call's), phase */
};

struct rs48 {
    struct rs48_stage up, dn;
    float x_up[RS_UP_K - 1 + RS_FRAMES], x_dn[RS_DN_K - 1 + RS_MAX48];
    float fifo[RS_FRAMES + 8];
    int fifo_n;
};

/* Main loop: the shared tables (designed once, at the first call) and the histories cleared. */
void rs48_init(struct rs48 *r);

/* RS_FRAMES in -> returns the count (<= RS_MAX48) */
int rs48_up(struct rs48 *r, const float *in44, float *out48);

/* c in -> RS_FRAMES out */
void rs48_down(struct rs48 *r, const float *in48, int c, float *out44);
