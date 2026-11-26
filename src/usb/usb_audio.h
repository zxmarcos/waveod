/*
 * USB audio (UAC 2.0, 44.1 kHz): recording to the computer (IN 0x81) and playback from it (OUT 0x01), after
 * the original (FUN_0003880C class requests, FUN_000369C0 / FUN_000364B8 transfers, FUN_0003678C /
 * FUN_00036284 the audio loop's side).
 *
 * Channels (6-In 4-Out; the 2-In 2-Out configuration carries the first two of each):
 *   recording  1/2 dry guitar or processed (per side, global setting), x record level (+ AUX IN)
 *              3/4 processed, before AUX IN and the computer's playback (+ AUX IN)
 *              5/6 the final mix, as it goes to the outputs before MASTER
 *   playback   1/2 mixed into the output after the effects, x monitor level
 *              3   into the effects chain with the guitar (re-amping); 4 unused
 *
 * Formats: alternate setting 1 = 16-bit (6-In 4-Out) or 24-bit in 4 bytes (2-In 2-Out), 2 = 24-bit in 4
 * bytes. The recording endpoint paces itself on the audio clock (5 or 6 frames per microframe, kept around
 * a fill target of the ring), and the host derives the playback rate from it (implicit feedback).
 *
 * Samples cross in "converter units": floats on the 32-bit scale of the converters.
 */
#pragma once

#include "usb/usb_device.h"
#include <stdint.h>

#define USB_REC_CHANNELS   6
#define USB_PLAY_CHANNELS  4

void usb_audio_init(void);
void usb_audio_reset(void);                         /* bus reset / configuration change: streams stopped */
void usb_audio_set_interface(int iface, int alt);
/* a class request to the audio function: IN fills data, returns its length; OUT gets the data stage;
   -1 = stall */
int usb_audio_request(const struct usb_setup *s, uint8_t *data, int len);
void usb_audio_xfer_done(uint8_t ep, const uint8_t *data, uint32_t len, int ok);

/* audio interrupt: `n` frames of the 6 recording channels (interleaved) */
void usb_audio_record(const float *frames, int n);
/* audio interrupt: `n` frames of the 4 playback channels (interleaved, feature unit gains applied);
   0 = nothing is streaming (frames zeroed) */
int usb_audio_play(float *frames, int n);

/* diagnostics */
struct usb_audio_stats {
    uint8_t rec_alt, play_alt;
    uint16_t rec_fill, play_fill;           /* frames waiting in the rings */
    uint32_t rec_packets, play_packets;
    uint32_t rec_short, play_under, play_over;
};
void usb_audio_stats(struct usb_audio_stats *out);
