/*
 * MIDI hardware: the TRS port and the timer of the MIDI clock.
 *
 * TRS: LPUART1 at 31250 baud, 8N1, like the original (FUN_00030A70 / FUN_00038ED0). Received bytes queue in a
 * ring for the main loop; bytes to send queue in another ring the transmit interrupt drains.
 *
 * Clock timer: a periodic interrupt (GPT2 on the 24 MHz crystal, the timer the original's clock output uses).
 * Its function runs in the interrupt and returns the microseconds until its next call.
 *
 * The UART and timer interrupts share one priority (below USB, above audio), so the clock may insert its byte
 * into the transmit queue without a lock.
 */
#pragma once

#include <stdint.h>

void midi_hw_init(void);

/* main loop: bytes received since the last call */
int midi_uart_read(uint8_t *buf, int max);

/* main loop: queue a whole message; 0 = queued, -1 = no room (nothing queued) */
int midi_uart_write(const uint8_t *msg, int len);

/* clock interrupt: one timing clock (0xF8), sent ahead of the bytes waiting */
void midi_uart_clock(void);

/* start the timer: fn first runs after first_us microseconds */
void midi_timer_start(uint32_t first_us, uint32_t (*fn)(void));
