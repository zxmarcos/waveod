/*
 * Names of the drum machine's genres and patterns. Patterns are numbered as in the pattern bank (0..99), the
 * genres taking them in turn: Rock 0-29, Blues 30-39, Funk 40-46, Jazz 47-53, Pop 54-61, Electronic 62-68,
 * World 69-89, Metronome 90-99.
 */
#pragma once

#include <stdint.h>

#define DRUM_GENRES   8
#define DRUM_PATTERNS 100

extern const char *const drum_genre_names[DRUM_GENRES];
extern const uint8_t drum_genre_count[DRUM_GENRES];
extern const char *const drum_pattern_names[DRUM_PATTERNS];

/* First pattern of a genre (FUN_8000F650), and the genre a pattern belongs to. */
static inline int drum_genre_first(int genre)
{
    int n = 0;
    for (int g = 0; g < genre && g < DRUM_GENRES; g++)
        n += drum_genre_count[g];
    return n;
}

static inline int drum_genre_of(int pattern)
{
    int g = 0;
    while (g + 1 < DRUM_GENRES && pattern >= drum_genre_first(g + 1))
        g++;
    return g;
}