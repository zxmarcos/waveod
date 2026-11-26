/*
 * The bar at the top of every screen, one set of objects on the top layer shared by all screens: a row of buttons a
 * finger's height and under it a 4 px strip with the meters (IN, OUT, the DSP load). Main screens: ≡ (the MENU
 * screen), the footswitch mode (tap = a chooser of PRESET / STOMP / CENAS), the preset (number, name, BPM / VOL),
 * the scene (C1..C4: on the chain tab, and on the others while the scenes are in play) and SAVE; the other screens:
 * "‹", the screen's title and, on the block screens, the scene.
 *
 * Touch: ≡ = MENU, the mode = the chooser (a mode = its tab), the preset: swipe = next / previous, tap = the presets
 * list; BPM / VOL = tempo and volume on the chain screen (again = back to the block); the scene: tap = the scene
 * menu (a scene = play and edit it, COPIAR PARA, RENOMEAR, DESFAZER), swipe = previous / next scene; SAVE (lit while
 * the preset has unsaved changes) = the SAVE button, "‹" = the EXIT button.
 */
#pragma once

#include "ui/ui_internal.h"

void top_bar_init(void);

/* Main screens (PRESET, STOMP, CENAS, CHAIN, TUNER): ≡, the mode (framed when `active` is its tab), the preset. */
void top_bar_tabs(enum screen_id active);

/* After top_bar_tabs: BPM / VOL lit (the chain screen is showing tempo and volume). */
void top_bar_tempo(int lit);

/* Other screens: back button and title, like "EDIT · PÓS". */
void top_bar_sub(const char *title);

/* After top_bar_sub: the scene button at the right end (the screens that edit a block). */
void top_bar_scene(void);

/* The mode chooser and the scene menu: MENU turns / press pick, EXIT closes (or leaves COPIAR PARA); 1 = consumed.
   Closed on a screen change. */
int top_bar_input(const struct input_event *ev);
void top_bar_close(void);

/* Peak levels since the last call, 0..1 of full scale, and the slowest audio block (ms); call at a steady
   rate (ballistics are per call). */
void top_bar_meters(float in, float out, float block_ms);

/* The meters' scale (0..1 for -48..0 dBFS) and the DSP load in %, for the MENU screen. */
float top_bar_meter_pos(float peak);
int top_bar_dsp_percent(float block_ms);
