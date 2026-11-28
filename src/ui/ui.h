/*
 * User interface (LVGL 9): screens of the "Display Multiefeitos" design driven by the pedal controls.
 */
#pragma once

#include "input/input.h"
#include "model/globals.h"
#include "model/rig.h"
#include "model/settings.h"

void ui_init(struct rig *rig, struct settings *settings, struct globals *globals);

/* Handle one control event. */
void ui_input(const struct input_event *ev);

/* Short message at the bottom of the screen. */
void ui_notice(const char *text);

/* A warning that stays until a tap or a button press (shown once the start-up animation is over). */
void ui_alert(const char *title, const char *text);

/* Run LVGL and refresh what changed; call from the main loop. */
void ui_tick(void);

/* The picture on the display: DISPLAY_W x DISPLAY_H pixels, 0x00RRGGBB (the frame buffer last shown). */
const uint32_t *ui_screen(void);

/* MIDI control (ui/midi_control.c, midi/midi.h): a program change (preset 0..239), a control change, a tempo
   measured on the MIDI clock. */
void ui_midi_program(int preset);
void ui_midi_control(int cc, int value);
void ui_midi_tempo(int bpm);
