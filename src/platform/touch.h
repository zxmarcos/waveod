/*
 * Touch panel over the display: one point, in display pixels.
 */
#pragma once

#include <stdint.h>

struct touch_point {
    uint8_t pressed;
    int16_t x, y;                   /* last position; kept after the release */
};

/* 1 when a touch controller answered at start-up. */
int touch_ready(void);

/* Current state (cheap to call often: the controller is only polled when it has news). */
void touch_read(struct touch_point *out);

/* Short description for the diagnostics, e.g. "GT911 0x14 480x272". */
const char *touch_info(void);
