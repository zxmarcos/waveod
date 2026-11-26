/*
 * The looper, after the original's (state 0x20242D20, block processing FUN_0002B2A4 with the state machine
 * FUN_0002B98C, record FUN_0002C13C, play FUN_0002C014, overdub FUN_0002C2EC, level trigger FUN_0002B318):
 *
 *   - 16-bit samples in a 0x792000-sample SDRAM buffer: 180 s mono / 90 s stereo, half of it with undo (the
 *     other half holds the last overdub, so it can be taken back and put back);
 *   - PRE: the guitar before the chain, mono, played back into the chain's input; POST: the stereo output after
 *     the chain and the preset level, played back before the global EQ;
 *   - record gain = record level x 32768 / 300, playback gain = play level / (32768 x 100 / 300): 100 / 100
 *     plays back what went in;
 *   - auto record: armed, recording starts when the input's envelope (attack 0.995, release 0.999 per sample)
 *     passes 0.008.
 * Commands come from the main loop and are taken at the start of the next block. The position (PRE / POST)
 * and undo settings take effect while the looper is empty, as in the original.
 *
 * Differences: overdubs saturate instead of wrapping around at 16 bits; tempo sync is not done yet.
 */
#pragma once

#include <stdint.h>

#define LOOPER_SAMPLES 0x792000u

enum looper_state { LOOP_EMPTY, LOOP_STOPPED, LOOP_PLAY = 3, LOOP_REC, LOOP_DUB };
enum looper_place { LOOP_PRE, LOOP_POST };
enum looper_cmd { LOOP_CMD_CLEAR = 1, LOOP_CMD_STOP = 2, LOOP_CMD_PLAY = 8, LOOP_CMD_REC = 0x10, LOOP_CMD_DUB = 0x20 };

struct looper_status {
    uint8_t state;                  /* enum looper_state */
    uint8_t place;                  /* enum looper_place in effect */
    uint8_t armed;                  /* auto record waiting for the input */
    uint8_t layer;                  /* an overdub can be undone (1) or redone (2), 0 = none */
    uint32_t pos, len, max;         /* frames */
};

void looper_init(void);

/* Settings: levels at once, place and undo when the looper is empty. */
void looper_set(int rec_level, int play_level, int post, int undo);

/* LOOP_CMD_x, taken at the next block. */
void looper_command(int cmd);

/* Arm / disarm: recording starts when the input passes the threshold. */
void looper_arm(int on);

/* The auto record setting (off at every start): recording from empty arms instead of starting at once. */
void looper_set_auto(int on);
int looper_auto(void);

/* Take the last overdub back, or put it back. */
void looper_undo(void);

void looper_status(struct looper_status *out);

/* The loop's waveform for the screen: out[x] = the peak (0..255) of column x of `columns` spread over the first
   `span` frames; what is not recorded yet is 0. The overdub shows while it is not taken back. */
void looper_wave(uint8_t *out, int columns, uint32_t span);

/* --- audio interrupt --- */

/* Start of a block: commands and the state machine; `in` (interleaved stereo, pre-chain guitar) feeds the
   auto record trigger. */
void looper_block(const float *in, int frames);

/* Run at `place`: `io` is recorded from and the loop added to it. Nothing happens at the other place. */
void looper_process(float *io, int frames, int place);
