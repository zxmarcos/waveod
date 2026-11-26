/*
 * MIDI control, after the original (receive: FUN_00030640 for the TRS port, FUN_00030CE0 for USB, the handlers
 * FUN_00030814 registers; clock output FUN_0002CB60 on GPT2):
 *
 *   - every message received on either port, channel or system, is sent on to both outputs (MIDI thru, USB
 *     included: what the computer sends comes back to it, as with the original); SysEx is not (USB SysEx is
 *     the editor's);
 *   - program and control changes count when the port is a MIDI source (global +0x78: 0 TRS, 1 USB, 2 both)
 *     and the channel is the port's input channel (+0x70 TRS, +0x72 USB; 0 = omni);
 *   - program change n: preset n (0..119) with bank select CC 0 = 0, preset 120 + n (up to 239) with CC 0 = 1;
 *   - CC 68 holds the high 7 bits of a tempo CC 69 completes; every other CC goes to the UI (ui/midi_control.c);
 *   - timing clocks measure a tempo per port: every BPM / 4 clocks, (clocks x 2500 / ms) is taken when the
 *     clock source (+0x79: 0 internal, 1 TRS, 2 USB, 3 external, 4 mixed) allows the port and it differs from
 *     the running tempo;
 *   - clock output (+0x7A: bit 0 TRS, bit 1 USB): 0xF8 every 2 500 000 / BPM microseconds from the clock timer,
 *     the fraction carried to the next.
 * Running status is kept; a real-time byte may come anywhere. The editor protocol gets the USB port's bytes.
 *
 * Main loop, except the clock timer's function.
 */
#pragma once

#include <stdint.h>

enum midi_port { MIDI_TRS, MIDI_USB, MIDI_PORTS };

struct midi_config {
    int16_t in_channel[MIDI_PORTS];     /* 0 = omni, 1..16 */
    uint8_t source;
    uint8_t clock_source;
    uint8_t clock_out;
};

struct midi_handlers {
    void (*program)(int preset);                    /* 0..239 */
    void (*control)(int cc, int value);             /* CC 69: the 14-bit tempo (CC 68 << 7 | CC 69) */
    void (*tempo)(int bpm);                         /* 40..300, measured on the MIDI clock */
    void (*usb_bytes)(const uint8_t *buf, int n);   /* what the USB port received, in order */
};

struct midi_stats {
    uint32_t in[MIDI_PORTS];                        /* messages received */
    uint32_t clocks_out;
};

void midi_init(const struct midi_handlers *h);
void midi_configure(const struct midi_config *c);

/* the tempo the effects follow: the clock output's rate, and what a measured tempo is compared with */
void midi_set_tempo(int bpm);

/* A message of waveod's own (MIDI out links, model/control.c) to both outputs, like the thru. */
void midi_send(const uint8_t *msg, int len);

void midi_poll(void);
void midi_stats(struct midi_stats *out);

/* the clock timer's function: one clock out if enabled; returns the microseconds to the next */
uint32_t midi_clock_tick(void);
