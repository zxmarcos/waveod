/*
 * The drum machine, after the original's (state 0x20234080; sequencer FUN_00040D94, voices FUN_0004104C,
 * volume FUN_00040D08; banks set up by FUN_00040CE0 / FUN_00040EA0):
 *   - a pattern is up to 200 u16 events: below 0x8000 a wait in ticks (120 per beat), 0x8000 | sample << 8 |
 *     velocity a hit, 0xFFFF back to the start;
 *   - 16 voices play the hits' 16-bit samples 64 at a time (a hit is heard from the next block), mixed at
 *     sample x velocity into 32 bits held within +-0x7FFFF8;
 *   - the volume (0..100) moves by 1 per sample towards its setting; the result is added to both outputs after
 *     the global EQ, x 2.6107e-7 like the original.
 * The sounds and patterns come with the firmware file (tools/drumpack.py), in the pack format below; without
 * them the drum machine stays silent and drums_available() says so.
 *
 * waveod's own patterns (model/drum_user.c, imported from MIDI files by waveod Studio) use the same events, up to
 * DRUM_USER_EVENTS of them, and may end with DRUM_END (stop there) instead of 0xFFFF (start over). One is copied
 * in with drums_set_events and plays instead of the bank's pattern until drums_use_bank.
 */
#pragma once

#include <stdint.h>

#define DRUM_PACK_MAGIC 0x4D524457u        /* "WDRM" */

struct drum_pack {
    uint32_t magic, version;
    uint32_t samples, samples_size;         /* offsets from the pack: the original's sample bank */
    uint32_t patterns, patterns_size;       /* and its pattern bank */
};

/* Set the pack up (0 = none); 0 when it is usable. */
int drums_init(const struct drum_pack *pack);
int drums_available(void);

#define DRUM_TICKS       120                /* per beat */
#define DRUM_HIT         0x8000u            /* | sample << 8 | velocity (1..127) */
#define DRUM_LOOP        0xFFFFu            /* back to the first event */
#define DRUM_END         0xFFFEu            /* the end: the drums stop (drums_playing() turns 0) */
#define DRUM_USER_EVENTS 2000

void drums_set_pattern(int pattern);        /* the bank's 0..99, restarts it when it is the one playing */
/* A pattern of count events (copied) plays from now on, from its start; 0 = it was too long or empty. */
int drums_set_events(const uint16_t *events, int count);
void drums_use_bank(void);                  /* the bank's pattern again */
int drums_samples(void);                    /* sounds in the bank (a hit's sample number is below it) */
void drums_set_bpm(int bpm);                /* 40..300 */
void drums_set_volume(int volume);          /* 0..100 */
void drums_play(int on);                    /* on: from the pattern's start */
int drums_playing(void);                    /* started and not at a DRUM_END */

/* A hit now (sample << 8 | velocity), and one block of the voices before the volume, 32-bit like the
   original's FUN_0004104C (tools/check_drums.py compares the two); drums_mix returns 0 when no voice sounded (acc
   all zeros). */
void drums_hit(uint16_t note);
int drums_mix(int32_t acc[64]);

/* Audio interrupt: add one block to io (interleaved stereo). */
void drums_process(float *io, int frames);