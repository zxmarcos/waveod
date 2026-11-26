/* The drum machine's genre and pattern names, from the V1.2.2 application (genre names 0x800C0748, counts
   0x800C0740, pattern names 0x2023408C). The application lists the names grouped in another order than its
   pattern bank; here they follow the bank (genres in the order of the counts). */
#include "model/drum_names.h"

const char *const drum_genre_names[DRUM_GENRES] = {
    "Rock",
    "Blues",
    "Funk",
    "Jazz",
    "Pop",
    "Electronic",
    "World",
    "Metronome",
};

const uint8_t drum_genre_count[DRUM_GENRES] = { 30, 10, 7, 7, 8, 7, 21, 10 };

const char *const drum_pattern_names[DRUM_PATTERNS] = {
    /* Rock */
    "Rock 1", "Rock 2", "Rock 3", "Rock 4", "Rock 5", "Classic Rock 1", "Classic Rock 2", "Classic Rock 3",
    "Hard Rock", "Garage Rock", "Prog Rock", "Surf Rock", "Rock 5/4", "Post Rock 1", "Post Rock 2", "Post Rock 3",
    "Punk 1", "Punk 2", "Punk 3", "Punk 4", "EMO", "Grunge", "New Wave", "Post Punk 1", "Post Punk 2", "Hardcore",
    "Metal 1", "Metal 2", "Nu-Metal 1", "Nu-Metal 2",
    /* Blues */
    "Blues 1", "Blues 2", "Blues 3", "Blues 4", "Swing", "Shuffle", "Shuffle 3/4", "Bluegrass", "Country", "Folk",
    /* Funk */
    "Funk 1", "Funk 2", "Funk 3", "Funk 4", "Jazz Funk 1", "Jazz Funk 2", "Jazz Funk 3",
    /* Jazz */
    "Jazz 1", "Jazz 2", "Jazz 3", "Jazz 4", "Bossanova 1", "Bossanova 2", "Fusion",
    /* Pop */
    "Pop 1", "Pop 2", "Pop 3", "Hip Hop 1", "Hip Hop 2", "Hip Hop 3", "Hip Hop Rock", "Pub",
    /* Electronic */
    "Electro1", "Electro2", "Electronic Pop", "Techno", "TripHop", "Break Beat", "Drum&Bass",
    /* World */
    "Latin 1", "Latin 2", "Latin 3", "Latin Pop 1", "Latin Pop 2", "Samba", "Tango", "Beguine", "Ska", "Polka",
    "Waltz", "Reggae 1", "Reggae 2", "Mazuke", "Musette", "March 1", "March 2", "March 3", "New Age 1", "New Age 2",
    "World",
    /* Metronome */
    "1/4", "2/4", "3/4", "4/4", "5/4", "6/4", "7/4", "6/8", "7/8", "9/8",
};
