/*
 * Calling into a loaded module (ABI confirmed in the original dispatchers 0x1B5DC / 0x1B654):
 *
 *   void Init(const float *knobs, uint32_t *ctx);      ctx[0] = 60000 / BPM, or the flash address of the
 *                                                      IR / capture for the Cab, User IR and Clone engines
 *   void Process(float *io, uint16_t frames, const float *knobs, float *aux, const uint8_t *enabled,
 *                const uint32_t *beat_ms, float **extra);
 *
 * io is interleaved stereo, processed in place; the module implements its own bypass.
 * The simulator cannot run ARM code, so there the calls do nothing.
 */
#pragma once

#include <stdint.h>

typedef void (*module_init_fn)(const float *knobs, uint32_t *ctx);
typedef void (*module_process_fn)(float *io, uint16_t frames, const float *knobs, float *aux,
                                  const uint8_t *enabled, const uint32_t *beat_ms, float **extra);

#ifdef WAVEOD_HOST
static inline void module_call_init(uint32_t fn, const float *knobs, uint32_t *ctx)
{
    (void)fn; (void)knobs; (void)ctx;
}

static inline void module_call_process(uint32_t fn, float *io, uint16_t frames, const float *knobs,
                                       float *aux, const uint8_t *enabled, const uint32_t *beat_ms,
                                       float **extra)
{
    (void)fn; (void)io; (void)frames; (void)knobs; (void)aux; (void)enabled; (void)beat_ms; (void)extra;
}
#else
static inline void module_call_init(uint32_t fn, const float *knobs, uint32_t *ctx)
{
    ((module_init_fn)(uintptr_t)fn)(knobs, ctx);
}

static inline void module_call_process(uint32_t fn, float *io, uint16_t frames, const float *knobs,
                                       float *aux, const uint8_t *enabled, const uint32_t *beat_ms,
                                       float **extra)
{
    ((module_process_fn)(uintptr_t)fn)(io, frames, knobs, aux, enabled, beat_ms, extra);
}
#endif
