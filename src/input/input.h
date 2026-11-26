/*
 * Control events: button presses / releases / long presses and encoder detents, derived from the raw
 * state polled from the platform.
 */
#pragma once

#include "platform/controls.h"
#include <stdint.h>

enum input_type { IN_PRESS, IN_RELEASE, IN_HOLD, IN_TURN };

struct input_event {
    uint8_t type;
    uint8_t id;             /* button bit index (BTN_x = 1 << id) or encoder (ENC_x) */
    int16_t delta;          /* IN_TURN: detents, + = clockwise */
    int16_t fast;           /* IN_TURN: detents with acceleration, for long ranges */
};

#define INPUT_HOLD_MS 700

void input_init(void);

/* Poll the controls; returns how many events were written (up to max). */
int input_poll(struct input_event *ev, int max);

/* Smoothed potentiometer / pedal position, 0..1. */
float input_pot(int pot);

/* Button id of a BTN_x mask. */
static inline uint8_t input_button_id(uint32_t mask)
{
    uint8_t id = 0;
    while (mask > 1) {
        mask >>= 1;
        id++;
    }
    return id;
}
