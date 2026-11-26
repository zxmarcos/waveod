/*
 * PC simulator back end: controls driven by a script, frame buffer saved as PNG, flash built from a
 * firmware update file (.bin). Time is virtual and only moves when the script says so, which makes the
 * screenshots reproducible.
 */
#pragma once

#include <stdint.h>

/* flash */
int host_flash_load_firmware(const char *path);
uint8_t *host_flash_image(void);                /* the whole 8 MB image (created erased when needed) */
extern uint32_t host_flash_erases, host_flash_programs;

/* the PC editor asked for the bootloader (plat_enter_bootloader) */
extern int host_bootloader_requests;

/* time */
void host_time_advance(uint32_t ms);

/* controls */
void host_buttons_set(uint32_t mask, int pressed);
void host_encoder_turn(int enc, int detents);
void host_pot_set(int pot, uint16_t value);
void host_touch_set(int pressed, int x, int y);

/* display */
int host_display_save_png(const char *path);

/* audio: run the registered callback for `blocks` silent blocks */
void host_audio_run(int blocks);
void host_audio_tone(double hz, double level);         /* 0 Hz = silence */
/* a cable from output word out_word (0 = L, 1 = FX send, 2 = R; -1 = none) to the guitar input (the FX return
   for the send), extra samples beyond the 128 of the buffers */
void host_audio_loop(int out_word, int extra);

/* USB: the test plays the host (host_usb.c) */
void host_usb_reset(void);
int host_usb_connected(void);
uint8_t host_usb_address(void);
/* a whole control transfer; IN: bytes received into in, OUT: 0; -1 = stalled */
int host_usb_control(const uint8_t setup[8], const uint8_t *out, uint8_t *in, int max);
int host_usb_in(uint8_t ep, uint8_t *buf, int max);           /* the oldest queued IN transfer, -1 = none */
int host_usb_out(uint8_t ep, const uint8_t *data, int len);   /* into the oldest queued OUT transfer */

/* MIDI: bytes into the TRS input, what the TRS output sent, one run of the clock timer (its next interval) */
void host_midi_in(const uint8_t *data, int len);
int host_midi_out(uint8_t *buf, int max);
uint32_t host_midi_clock_tick(void);

/* crash protection: how the previous run ended (safety_last_crash), deliberate test failures asked for */
struct crash_info;
void host_crash_set(const struct crash_info *c);
extern int host_safety_tests;

/* footswitch LEDs: "A=green B=off ..." */
void host_leds_text(char *out, unsigned size);

/* PNG encoder (RGB, uncompressed deflate) */
int png_write_xrgb(const char *path, const uint32_t *pixels, int w, int h);
