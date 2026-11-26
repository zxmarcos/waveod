/*
 * Platform interface: everything the application needs from the hardware.
 *
 * Two implementations exist:
 *   - src/board/qme200: the Matribox II Pro (QME-200) pedal, i.MX RT1062 Cortex-M7;
 *   - src/host:         a PC simulator that renders the UI to PNG files and replays scripted input.
 *
 * The rest of the code base only includes the headers of this directory.
 */
#pragma once

#include <stdint.h>

/* Bring up everything except audio (clocks, pins, display, controls). */
void plat_init(void);

/* Human readable platform name, shown in the diagnostics. */
const char *plat_name(void);

/* Core clock measured at start-up, in Hz (0 when unknown), and the one the bootloader left (the firmware sets
   600 MHz like the original when it was less). */
uint32_t plat_core_hz(void);
uint32_t plat_boot_core_hz(void);

/* The CPU die's temperature in tenths of a degree C (the on-chip sensor with its factory calibration), or
   PLAT_TEMP_NONE: no sensor (the PC), an uncalibrated part, or the first measurement not in yet. */
#define PLAT_TEMP_NONE (-32768)
int plat_cpu_temp(void);

/* Restart in the bootloader's update mode after `delay_ms` (the PC editor's firmware update); no return. */
void plat_enter_bootloader(uint32_t delay_ms);

/* The drum machine's sounds and patterns (engine/drums.h), as the firmware file brought them; 0 = none. */
const void *plat_drum_pack(void);
