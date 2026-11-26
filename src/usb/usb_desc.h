/*
 * The USB identity of the pedal: the original's device, qualifier, configuration and string descriptors.
 *
 * Two configurations, picked by the global "USB mode" (record 0 +0x230, read by FUN_000383xx):
 *   0  "6-In 4-Out" (factory): audio control, 6-channel recording (IN 0x81), 4-channel playback (OUT 0x01),
 *      MIDI streaming (bulk 0x03 / 0x83, the editor's port);
 *   1  "2-In 2-Out": stereo recording and playback only (no MIDI, so no editor).
 * Audio is UAC 2.0 at 44.1 kHz, high speed, asynchronous isochronous endpoints with implicit feedback.
 */
#pragma once

#include <stdint.h>

#define USB_DESC_CONFIG_2X2_LEN   0xF7
#define USB_DESC_CONFIG_6X4_LEN   0x1B5
#define USB_STRINGS               7

/* interfaces and endpoints of the configurations */
#define USB_IF_AUDIO_CONTROL      0
#define USB_IF_AUDIO_REC          1         /* to the computer */
#define USB_IF_AUDIO_PLAY         2         /* from the computer */
#define USB_IF_MIDI               3
#define USB_EP_AUDIO_REC          0x81
#define USB_EP_AUDIO_PLAY         0x01
#define USB_EP_MIDI_OUT           0x03      /* host -> pedal */
#define USB_EP_MIDI_IN            0x83

/* audio entities (UAC2 requests address them) */
#define USB_ENT_REC_FEATURE       0x09
#define USB_ENT_PLAY_FEATURE      0x0C
#define USB_ENT_CLOCK             0x11

extern const uint8_t usb_desc_device[18];
extern const uint8_t usb_desc_qualifier[10];
extern const uint8_t usb_desc_config_2x2[USB_DESC_CONFIG_2X2_LEN];
extern const uint8_t usb_desc_config_6x4[USB_DESC_CONFIG_6X4_LEN];

/* string descriptor `index` into out; its length, -1 = no such string */
int usb_desc_string(int index, uint8_t *out, int max);
