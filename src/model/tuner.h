/*
 * Tuner: pitch of the guitar input as note, octave and cents against the A4 reference.
 *
 * While running, the engine hands over the decimated input (and mutes the output when asked); every 256
 * new samples (~12 ms) the last 1024 are analysed. The cents are smoothed while the note holds; a new note
 * must be seen twice before it is shown; the display goes blank 400 ms after the last clear pitch.
 */
#pragma once

#include <stdint.h>

struct tuner_reading {
    int valid;                              /* a note is shown */
    int note;                               /* 0 = C .. 11 = B */
    int octave;                             /* scientific pitch notation: A4 = 440 Hz */
    float cents;                            /* -50 .. +50 */
    float hz;
};

void tuner_start(uint16_t a4_hz, int mute);
void tuner_stop(void);
int tuner_running(void);

/* Collect input and analyse when enough arrived; returns 1 when the reading changed. */
int tuner_poll(void);
void tuner_get(struct tuner_reading *out);

/* "C", "C#", ... */
const char *tuner_note_name(int note);
