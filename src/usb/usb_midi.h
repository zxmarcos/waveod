/*
 * USB MIDI 1.0 streaming (6-In 4-Out configuration, interface 3): bulk OUT 0x03 from the computer, bulk IN
 * 0x83 to it, 4-byte event packets (cable, code index, up to 3 MIDI bytes). The editor protocol rides on it
 * as SysEx (editor/link.c), MIDI control too (midi/midi.c).
 *
 * Received MIDI bytes queue in a ring for the main loop; the OUT endpoint is re-armed only while the ring has
 * room for a whole transfer, so a busy main loop slows the computer down (NAKs) instead of losing data.
 * Timing clocks from the clock interrupt go out ahead of the queued messages.
 */
#pragma once

#include <stdint.h>

void usb_midi_init(void);

/* USB interrupt: the configuration came up / went away, a transfer finished, the end of every interrupt */
void usb_midi_start(void);
void usb_midi_stop(void);
void usb_midi_xfer_done(uint8_t ep, const uint8_t *data, uint32_t len, int ok);
void usb_midi_service(void);

/* main loop */
int usb_midi_ready(void);
int usb_midi_read(uint8_t *buf, int max);           /* MIDI bytes received since the last call */
int usb_midi_write(const uint8_t *msg, int len);    /* one complete message; 0 = queued, -1 = no room */
int usb_midi_tx_free(void);                         /* message bytes that fit now */
/* How long the IN transfer queued last has waited for the host, 0 when none waits. The host polls the IN
   endpoint only while an application has the MIDI input open: a long wait means nobody is listening. */
uint32_t usb_midi_tx_stalled_ms(void);

/* an interrupt below the USB one: one timing clock (0xF8) */
void usb_midi_clock(void);
