/*
 * Audio block processing (runs in the audio interrupt), modelled on the original loop FUN_00031E18:
 *
 *   guitar = (ADC0 R / 2 - ADC0 L / 2) x input level / K     K = 980586112 (the effects' full scale)
 *   FX return = (ADC1 R / 2 - ADC1 L / 2) / K,  AUX IN = -ADC2 L / R x AUX level / K
 *   {guitar, FX return} -> fixed input filter H(z)
 *   io = guitar on both channels, side chain = guitar + FX return -> chain (in place; Cab / IR moved to the
 *   end for the "no cab" outputs) -> preset volume / pan -> global EQ -> + AUX IN -> output filter 1 / H(z)
 *   DAC = { L x MASTER^2 x K, FX send x K (through 1 / H(z) too), R x MASTER^2 x K, 0 }
 *
 * Effect modules were tuned for this scaling and these filters, so they must stay exactly as in the original.
 * The looper (engine/looper.c) adds its loop before the chain (PRE) or before the global EQ (POST), the drum
 * machine (engine/drums.c) after the global EQ. USB audio (usb/usb_audio.h): playback channel 3 joins the
 * guitar before the chain, 1 / 2 the output after AUX IN; recording taps the dry / processed signal after the
 * preset level and the final mix.
 */
#pragma once

#include "engine/chain.h"
#include "fw/globals_format.h"
#include <stdint.h>

struct engine_stats {
    float in_peak, out_peak;        /* since the last read, 0..1 of the converters' full scale (the ADC after
                                       the input level, the DAC) */
    float block_ms, worst_ms;       /* processing time of one block (budget 1.45 ms at 44.1 kHz / 64) */
    float max_ms;                   /* slowest block since start-up (never reset) */
    uint32_t blocks;
    uint32_t overruns;              /* blocks over the 1.25 ms the original allows */
    uint32_t overloads;             /* times the overload guard switched the chain off */
    uint32_t spills;                /* tails handed over (engine_spill) */
    uint32_t spill_cuts;            /* tails cut for a slow block */
};

void engine_init(struct chain *chain, uint32_t core_hz);

/* audio_callback for audio_start() */
void engine_audio(const int32_t *in, int32_t *out, int frames);

/* Stop touching the chain (after a short fade) / start again with a fade in. A suspend also cuts the
   spillover's tails (within a block). */
void engine_suspend(void);
void engine_resume(void);

/* Around a write to the flash (presets, settings): the audio keeps playing, since nothing in it reads the flash
   (the original IR / Clone modules get a copy of their data, chain.c), unless a slot of the chain or of a tail
   could not have its copy: then as engine_suspend / engine_resume. begin returns what end takes. */
int engine_flash_begin(void);
void engine_flash_end(int suspended);

/*
 * Spillover: the delays and reverbs of the preset that was playing ring out while the next one loads and
 * plays (waveod's; the original cuts the sound at every preset change).
 *
 * engine_spill, instead of engine_suspend before loading another preset: `tail` (chain_tail of the running
 * chain, kept untouched until it ends) takes the chain's place in the interrupt from the next block, so the
 * chain is free to load as after a suspend, into the other side of the pools (chain_build_side). The guitar
 * leaves it over 6 ms; once the slots before the delays and reverbs are silent (or after 300 ms) and the new
 * chain runs, only the slots of the tails keep running, with silence in, at the old preset's level, added after
 * the new preset's level. A tail ends after the time set,
 * after 2.5 s of silence, at the next change (faded over 6 ms; engine_spill waits for that, its side of the
 * pools being the one to load), at a suspend, the tuner's mute, or blocks over 1.05 ms (the first one ends the
 * drain early, the next cuts the tail; the chain itself goes on). Returns 1 when handed over, 0 when off or the audio is not running (suspend as before).
 */
void engine_set_spill(uint32_t ms);             /* how long a tail may ring; 0 = no spillover */
int engine_spill(struct chain *tail);
int engine_spilling(void);                      /* a tail is ringing */

void engine_set_master(float linear);           /* 0..1, already shaped */

/* Global settings (fw/globals_format.h), applied from the next block. */
void engine_set_input_level(int db);            /* -20..20, ramped by 0.01 per block */
void engine_set_aux_level(int percent);         /* 0..100 */
void engine_set_no_cab(int left, int right);    /* outputs without the Cab / User IR blocks */
/* USB audio: record level and monitor level (dB, -20..20), AUX IN into the recording, recording 1 / 2 processed */
void engine_set_usb(int rec_db, int mon_db, int rec_aux, int wet_l, int wet_r);
void engine_set_global_eq(int on, const float params[EQ_PARAMS]);
void engine_set_preset_level(uint8_t volume, int8_t pan);

/* Silence the output and stop running the chain (tuner); fades out and back in. */
void engine_set_mute(int on);

/* Tuner input: the guitar signal decimated to 22.05 kHz, collected while feeding is on. engine_tuner_read
   copies the samples that arrived since the last call (the newest TUNER_RING at most). */
void engine_tuner_feed(int on);
int engine_tuner_read(float *dst, int max);

/* Copy and reset the peak meters. */
void engine_stats(struct engine_stats *out);

/* Copy without resetting anything (diagnostics). */
void engine_stats_peek(struct engine_stats *out);

/* Where the interrupt's block time goes besides the slots (protocol 11 profile, tools/bench.py --profile): the
   mean cycles of each section of engine_audio since the last reset. */
enum engine_section {
    ESEC_CONVERT,                   /* the converters' words (DMA buffer) -> floats */
    ESEC_IN_FILTER,                 /* the input filter H(z) */
    ESEC_USB_PLAY,                  /* USB playback taken */
    ESEC_SPREAD,                    /* guitar / FX return / send buffers, the tuner */
    ESEC_PRE,                       /* looper, side chain, spillover hand-over, modulators */
    ESEC_CHAIN,                     /* chain_process: the slots and what runs between them */
    ESEC_MID,                       /* analyser, fades, the spillover's tails */
    ESEC_LEVEL,                     /* preset volume / pan, USB recording 1-4 */
    ESEC_EQ,                        /* looper POST, global EQ */
    ESEC_DRUMS,                     /* the drum machine */
    ESEC_MIX,                       /* AUX IN, USB playback 1 / 2, USB recording */
    ESEC_OUT_FILTER,                /* the main outputs' filter 1 / H(z) */
    ESEC_SEND_FILTER,               /* the FX send's */
    ESEC_DAC,                       /* floats -> converters */
    ESEC_STATS,                     /* latency test, statistics */
    ENGINE_SECTIONS
};
uint32_t engine_sections(uint32_t mean[ENGINE_SECTIONS]);      /* returns the blocks counted */
void engine_sections_reset(void);                              /* counted afresh from the next block */

/* 1 once after the overload guard (engine/overload.h) has switched every slot of the chain off in the audio
   interrupt; the caller brings the preset in line and tells the user. */
int engine_overload_take(void);
