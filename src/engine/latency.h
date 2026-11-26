/*
 * Latency test: how long an input sample takes to come out, measured by the pedal itself.
 *
 *   cable routes (OUT L / OUT R -> INPUT, FX SEND -> FX RETURN): a short chirp goes out of the output while the
 *   chain's sound is held silent, comes back through a cable, and the input is searched for it (the peak of the
 *   cross-correlation, interpolated below a sample). The test reads and writes the converters' words directly
 *   (engine_audio, before the input filter and after the output filter), so it behaves like an effect with no
 *   latency and the lag found is the whole input -> output time: the 2 x 64-frame DMA buffers, the SAI FIFO
 *   and the converters' filters.
 *
 *   PRESET (no cable): the chirp goes into the chain in place of the guitar and is searched for in what the
 *   chain gives back (both sides, the better match wins); nothing is played. That is what the preset adds
 *   (resampling, pitch, look-ahead); with reverbs or delays the first strong arrival counts.
 *
 * LAT_RUNS measurements, LAT_GAP blocks apart; a cable route starts quiet (-30 dBFS) and goes up while nothing
 * comes back, down when the input clips. The search runs in the main loop (latency_poll), the interrupt only
 * plays and records.
 */
#pragma once

#include <stdint.h>

enum latency_route { LAT_OUT_L, LAT_OUT_R, LAT_SEND, LAT_PRESET, LAT_ROUTES };

enum latency_status {
    LAT_IDLE,           /* never run */
    LAT_BUSY,
    LAT_OK,
    LAT_UNSTABLE,       /* measured, but the runs differ by more than LAT_SPREAD samples */
    LAT_NO_SIGNAL,      /* nothing came back (no cable, or the preset lets nothing through) */
    LAT_CLIPPED,        /* the input clipped even at the lowest level */
    LAT_CANCELLED,
};

#define LAT_RUNS        8
#define LAT_LEVELS      3                       /* -30, -20, -10 dBFS on the cable routes */
#define LAT_SPREAD      2.0f
#define LAT_BUFFERS     128                     /* 2 x AUDIO_FRAMES: what the buffers alone give */
#define LAT_STIMULUS    512                     /* chirp length (samples) */
#define LAT_CAPTURE     4096                    /* recorded from the chirp's start: the longest lag found is
                                                   LAT_CAPTURE - LAT_STIMULUS (81 ms) */

struct latency_result {
    uint32_t seq;               /* bumped on every change */
    uint8_t status;             /* enum latency_status */
    uint8_t route;
    uint8_t runs;               /* measurements done */
    int8_t level_db;            /* chirp level (dBFS of the DAC; 0 on PRESET) */
    float min, max, mean;       /* samples */
    float match;                /* how alike the weakest run was to the chirp, 0..1 */
};

/* Start (cancels a test running), cancel. */
void latency_start(int route);
void latency_cancel(void);

/* Main loop: search what was recorded, go on with the next run; 1 when the result changed. */
int latency_poll(void);

void latency_get(struct latency_result *out);
const char *latency_route_name(int route);      /* "SAÍDA L » INPUT" */
const char *latency_status_name(int status);    /* "SEM SINAL" */

/* ---- audio interrupt (engine_audio) */

/* 1 while a test runs: the outputs fade to silence (the test then writes them, engine_audio's words) */
int latency_quiet(void);

/* PRESET route: the chain's input (io, stereo interleaved) replaced, its output recorded */
int latency_preset(void);
void latency_chain_in(float *io, int frames);
void latency_chain_out(const float *io, int frames);

/* the converters' words of the block (platform/audio.h), after engine_audio wrote the outputs */
void latency_io(const int32_t *in, int32_t *out, int frames);

/* ---- the search, for the tests: lag (samples, fractional) of ref in x, match 0..1; -1 when nothing */
float latency_find(const float *ref, int nref, const float *x, int nx, float *match);
