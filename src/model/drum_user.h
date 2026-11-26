/*
 * waveod's own drum patterns (MEUS RITMOS): DRUM_USER_SLOTS places in waveod's store (fw/wv_store.h, type
 * WVS_DRUM, key = place), filled by waveod Studio from MIDI files (protocol 0x1F000011..13). A record:
 *
 *   u8 version (1), u8 flags (DRUM_USER_ONCE: plays once, the last event is DRUM_END), u16 BPM of the MIDI file
 *   (0 = none), char name[16] (zero padded), u16 count, u16 events[count] (engine/drums.h: waits in ticks of
 *   1/120 beat, hits DRUM_HIT | sample << 8 | velocity, DRUM_LOOP or DRUM_END last)
 *
 * An empty record (length 0) is an emptied place.
 */
#pragma once

#include <stdint.h>

#define DRUM_USER_SLOTS   20
#define DRUM_USER_NAME    16
#define DRUM_USER_VERSION 1
#define DRUM_USER_ONCE    0x01
#define DRUM_USER_HEADER  22                /* bytes before the events */

struct drum_user_info {
    uint8_t flags;
    uint16_t bpm, count;
    char name[DRUM_USER_NAME + 1];
};

/* Place slot's pattern: 0 = there is one (*out filled), -1 = empty. */
int drum_user_info(int slot, struct drum_user_info *out);

/* 0 when rec (n bytes) is a record this firmware plays. */
int drum_user_check(const uint8_t *rec, unsigned n);

/* Store rec in place slot (n = 0: empty it). 0 = written; -10 bad place, -12 not a record, then wvs_put's - 20.
   Blocks while the flash is written (suspend the audio first). */
int drum_user_store(int slot, const uint8_t *rec, unsigned n);

/* The stored record of place slot, or 0 (empty). */
const uint8_t *drum_user_record(int slot, unsigned *n);

/* Play place slot's pattern instead of the bank's (engine/drums.h). 0 = it plays, -1 = empty place. */
int drum_user_play(int slot);

/*
 * What the drum machine plays: the bank's pattern (the globals shared with the original) or a place of MEUS RITMOS
 * (settings->drum_user, waveod's). Everything that picks a pattern (drum screen, footswitches, MIDI, the editors)
 * goes through these.
 */
struct settings;
struct globals;
void drum_user_init(struct settings *s, struct globals *g);   /* plays the stored choice (an emptied place: the bank) */
int drum_user_selected(void);               /* the place playing, -1 = the bank's pattern */
/* Play place slot (the drum tempo becomes the MIDI file's while it does not follow the effects'). 0 = done, -1 =
   empty place (nothing changes). */
int drum_user_select(int slot);
void drum_user_deselect(void);              /* the bank's pattern again */
/* The used place d steps (+-) from slot, going round; -1 = none used. */
int drum_user_step(int slot, int d);
/* After drum_user_store: the place playing was rewritten (played again) or emptied (the bank's pattern). */
void drum_user_changed(int slot);
