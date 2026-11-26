/*
 * Crash protection.
 *
 * A fault (HardFault, MemManage, BusFault, UsageFault, NMI or an interrupt nobody handles) or a main loop that
 * stops running silences the outputs, writes a short record of what happened where a warm reset keeps it, and
 * restarts the pedal. The next start-up reads the record (safety_last_crash) and can avoid the preset that
 * crashed.
 *
 * Two watchdogs, both armed at the end of the start-up (safety_arm):
 *   - a software one in the 1 ms tick: the main loop must call safety_feed at least every SAFETY_FEED_MS; it
 *     records the code that was running (the audio interrupt, a module...) before it resets;
 *   - the chip's RTWDOG as a backstop (SAFETY_HW_MS), for when interrupts no longer run at all.
 * Long operations of the main loop (flash erases, a chain rebuild) feed it on the way.
 */
#pragma once

#include <stdint.h>

#define SAFETY_FEED_MS 1500
#define SAFETY_HW_MS   3000
#define SAFETY_NONE    0xff         /* no preset / slot known */

enum crash_cause {
    CRASH_NONE,
    CRASH_HARDFAULT,
    CRASH_MEMMANAGE,
    CRASH_BUSFAULT,
    CRASH_USAGEFAULT,
    CRASH_NMI,
    CRASH_IRQ,                      /* an interrupt without a handler */
    CRASH_WATCHDOG,                 /* the main loop stopped (software watchdog) */
    CRASH_HW_WATCHDOG,              /* the RTWDOG reset the chip (seen at start-up, nothing recorded) */
};

struct crash_info {
    uint8_t cause;                  /* enum crash_cause */
    uint8_t count;                  /* crashes in a row, each soon after the start-up before it */
    uint8_t preset;                 /* preset loaded then (0..239), SAFETY_NONE */
    uint8_t slot;                   /* chain slot running then (0..11), SAFETY_NONE */
    uint8_t in_audio;               /* the audio interrupt was running */
    uint32_t pc;                    /* code address, 0 when unknown */
    uint32_t detail;                /* CFSR for faults, the interrupt number for CRASH_IRQ */
    uint32_t uptime_s;              /* seconds since that start-up */
};

/* Read the record left by the previous run and clear it; 1 when that run ended in a crash. */
int safety_last_crash(struct crash_info *out);

/* Start the watchdogs (end of the start-up). */
void safety_arm(void);

/* The main loop is alive. */
void safety_feed(void);

/* Before a deliberate reset into the bootloader: the watchdogs off. */
void safety_disarm(void);

/* Context the record keeps: the loaded preset, and the slot whose module code runs (main loop: Init; audio
   interrupt: Process). SAFETY_NONE when none. */
void safety_note_preset(int index);
extern volatile uint8_t safety_slot_main, safety_slot_audio;

/* Watchdog state for the diagnostics: 0 = off, else the RTWDOG clock measured at start-up in Hz. */
uint32_t safety_watchdog_hz(void);

/* Deliberate failures, to check the protection on the pedal: */
enum safety_test {
    SAFETY_TEST_FAULT,              /* an undefined instruction in the main loop */
    SAFETY_TEST_HANG,               /* the main loop stops, interrupts keep running */
    SAFETY_TEST_HANG_IRQ_OFF,       /* the main loop stops with interrupts off: only the RTWDOG helps */
};
void safety_test(enum safety_test kind);
