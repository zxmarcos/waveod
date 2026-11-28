/*
 * The controls driven from the computer (protocol 15, 0x1F000020): buttons held, encoder detents, a touch and the
 * pedals' positions laid over the physical ones, so a script can play the pedal the way a hand would (the release
 * video's recordings, tests on the pedal). The computer sends the whole state each time; buttons, the touch and
 * the pedals go back to the physical ones when nothing came for REMOTE_TIMEOUT_MS.
 */
#pragma once

#include "platform/controls.h"
#include "platform/touch.h"
#include <stdint.h>

#define REMOTE_TIMEOUT_MS 2000
#define REMOTE_POT_NONE   0xFFFFu       /* the physical pedal */

enum remote_touch { REMOTE_TOUCH_NONE = -1, REMOTE_TOUCH_UP, REMOTE_TOUCH_DOWN };

/* buttons: BTN_x held from now on; turn: detents to add to each encoder (ENC_x); touch: enum remote_touch at x, y
   (NONE = the panel's own); pot: 0..4095 or REMOTE_POT_NONE */
void remote_set(uint32_t buttons, const int32_t turn[ENC_COUNT], int touch, int x, int y,
                const uint16_t pot[POT_COUNT]);

/* the raw state as the platform read it, with the computer's laid over */
void remote_apply(struct controls_raw *raw);

/* the touch as the panel read it, replaced by the computer's while it drives one */
void remote_touch(struct touch_point *p);
