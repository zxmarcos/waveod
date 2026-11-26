/*
 * 480 x 272 XRGB8888 display with two frame buffers.
 */
#pragma once

#include <stdint.h>

#define DISPLAY_W 480
#define DISPLAY_H 272

/* Frame buffer 0 or 1 (DISPLAY_W * DISPLAY_H pixels, 0x00RRGGBB). */
uint32_t *display_fb(int index);

/* Scan out frame buffer `index` from the next frame on; returns once the controller has switched. */
void display_show(int index);

/* Backlight, 0..100 %. */
void display_backlight(unsigned percent);
