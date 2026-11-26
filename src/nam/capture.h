/*
 * A NAM capture as a Clone slot holds it (written by waveod Studio, studio/waveod_studio/nam.py, or by
 * matribox Studio: the same format). A slot keeps 0x2000 bytes (fw/ir_fs.h IRFS_CLONE_DATA). Little-endian:
 *   0x00 u32 magic "NAM2"
 *   0x04 u16 version (1)
 *   0x06 u16 number of weights (A2_NUM_WEIGHTS)
 *   0x08 f32 loudness of the model in dB (-18 when the .nam has none)
 *   0x0c f32 sample rate the model was trained at
 *   0x10 u32 CRC-32 (IEEE) of the weight bytes
 *   0x14 u32 flags: bit 0 = run at the pedal's 44.1 kHz even if the model is 48 kHz (no resampling)
 *   0x18 zeros up to 0x88: the original's Clone module reads its IR lengths at +0x7c and +0x84 and leaves the
 *        slot inactive when they are 0, so the original firmware passes the signal through such a slot
 *   0x88 f32 weights[A2_NUM_WEIGHTS], packed as nam/a2.h takes them
 */
#pragma once

#include "nam/a2.h"
#include <stdint.h>

#define NAM_CAPTURE_MAGIC   0x324D414Eu         /* "NAM2" */
#define NAM_CAPTURE_VERSION 1
#define NAM_CAPTURE_HEADER  0x88
#define NAM_CAPTURE_BYTES   (NAM_CAPTURE_HEADER + A2_NUM_WEIGHTS * 4)
#define NAM_CAPTURE_NATIVE_RATE 1u

struct nam_capture {
    uint32_t magic;
    uint16_t version, nweights;
    float loudness, sample_rate;
    uint32_t crc;
    uint32_t flags;
    uint32_t reserved[28];
};

_Static_assert(sizeof(struct nam_capture) == NAM_CAPTURE_HEADER, "the header of a capture");

/* whether `data` (a Clone slot's, `size` bytes) starts like a capture this engine runs (the weights' CRC is
   checked when it loads) */
static inline int nam_capture_is(const void *data, uint32_t size)
{
    const struct nam_capture *h = (const struct nam_capture *)data;
    return h && size >= NAM_CAPTURE_BYTES && h->magic == NAM_CAPTURE_MAGIC && h->version == NAM_CAPTURE_VERSION &&
           h->nweights == A2_NUM_WEIGHTS;
}

/* whether a capture runs at 48 kHz through the resampler (trained at 48 kHz, not flagged to run at 44.1) */
static inline int nam_capture_resampled(const struct nam_capture *h)
{
    return h->sample_rate > 47000.0f && h->sample_rate < 49000.0f && !(h->flags & NAM_CAPTURE_NATIVE_RATE);
}
