/*
 * Audio I/O: 44.1 kHz, blocks of 64 frames, processed in the DMA interrupt.
 *
 * Frame layouts (32-bit words, left-justified 24-bit data), as wired on the QME-200:
 *   input  (6 words): ADC0 L, ADC1 L, ADC2 L, ADC0 R, ADC1 R, ADC2 R
 *          guitar = ADC0 R - ADC0 L (differential), FX return = ADC1 R - ADC1 L, AUX IN = -ADC2 L/R
 *   output (4 words): main L, FX send, main R, unused
 */
#pragma once

#include <stdint.h>

#define AUDIO_RATE        44100
#define AUDIO_FRAMES      64
#define AUDIO_IN_WORDS    6
#define AUDIO_OUT_WORDS   4

typedef void (*audio_callback)(const int32_t *in, int32_t *out, int frames);

/* Configure the converters and start streaming; `cb` runs once per block in interrupt context. */
int audio_start(audio_callback cb);

/* Analog input stage for the global input mode (0 acoustic, 1 electric, 2 line, 3): two switches on the
   shift register, set like the original's FUN_00022AE0. */
void audio_set_input_mode(int mode);

/* 1 when the callback runs on its own (an interrupt), so other code has to synchronise with it. */
int audio_streaming(void);

/* Blocks processed so far and blocks that finished after the next one was due. */
uint32_t audio_blocks(void);
uint32_t audio_late_blocks(void);
