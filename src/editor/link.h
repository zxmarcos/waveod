/*
 * The editor protocol's link layer: SysEx packets on the USB MIDI port, as the original runs them
 * (FUN_00031A08 receive, FUN_00031BF4 send, FUN_00030B38 / FUN_00030A0C messages; tools/htmidi.py is the
 * PC side).
 *
 *   packet   F0 21 25 4D 50 00 00 cs flag len_lo len_hi off_lo off_hi <2 nibbles per byte> F7
 *            len = the whole transfer's length, off = this chunk's offset (7-bit halves), cs = sum of the
 *            nibbles & 0x7F (not checked when below 6), flag 0x11 request / 0x12 data / 0x13 NAK
 *   transfer one or more messages {u32 cmd, u32 n, n bytes}, the payload LZO1X compressed
 *
 * A packet out of sequence or with a bad checksum is answered with a NAK carrying its offset. Packets go out
 * with up to 0xB9 bytes each, one per 3 ms like the original's sender (a main loop that was busy for longer
 * sends up to 8 at once). Main loop only.
 */
#pragma once

#include <stdint.h>

#define LINK_REQUEST   0x11
#define LINK_DATA      0x12
#define LINK_NAK       0x13
#define LINK_MAX       16383                /* a transfer's length is 14 bits */

/* a complete transfer: its bytes and flag (the server walks the messages in it) */
typedef void (*link_handler)(const uint8_t *data, int len, int flag);

void link_init(link_handler handler);

/* bytes the USB MIDI port received (midi/midi.c reads the port): SysEx packets are taken, the rest skipped */
void link_feed(const uint8_t *buf, int n);

/* hand complete transfers to the handler, send what is queued */
void link_poll(void);

/* queue a message with its payload compressed (FUN_00030B38); 0 = queued, -1 = no room */
int link_send(uint32_t cmd, const void *payload, uint32_t n);

/* queue a message as it is: {cmd, n, data} already laid out (FUN_00030A0C) */
int link_send_raw(const void *msg, uint32_t len, int flag);

/* 1 while the USB MIDI port is up */
int link_up(void);
