/*
 * Expression pedals, as the original reads them:
 *   - change detection of the ADC (FUN_0001AB10): the 12-bit value, averaged 1:3, is reported when it moved by
 *     64 or more, or by 16 or more for 21 polls in a row (every 10 ms); the pedal value is that >> 4 (0..255);
 *   - FUN_000200D0: the value is limited to the calibrated range (global settings); EXP 2 moves its targets
 *     linearly, EXP 1 with atan(tan(0.3) x) / 0.3 (each link adds its own curve, model/control.c), and pressing
 *     EXP 1 past the toe switch threshold presses the preset's toe key (CK_TOE: by default EXP A/B, flipping the
 *     preset between groups A and B), released below (max + 2 threshold) / 3.
 */
#pragma once

#include "fw/globals_format.h"
#include "model/control.h"

struct expression {
    uint32_t polled_at;
    int16_t filtered[2], reported[2];
    uint8_t drift[2];
    uint8_t toe;                    /* EXP 1 toe switch down */
    float pos[2];                   /* last position, applied again to a new preset */
    int16_t base[2], learn_v[2];    /* learning: the values it started from, where it last moved (0..255) */
    int8_t learn_key;               /* learning: the pedal moved far enough, -1 = none yet */
    uint32_t learn_at;              /* ... when it last moved */
};

void expression_init(struct expression *x);

/* Poll with the pedals' 12-bit values; 1 when the preset's parameters or blocks changed. The toe key's functions
   go to run. */
int expression_poll(struct expression *x, struct rig *r, const struct globals *g, const int raw[2],
                    control_run_fn run, void *ctx);

/* Learning which pedal the player uses (a link's key): from expression_learn_start on, expression_learn is
   polled instead of expression_poll and nothing acts. It returns CK_TOE when the toe switch is pressed, CK_EXP1A /
   CK_EXP1B (the preset's group) or CK_EXP2 when that pedal moved a quarter of its range and then rested
   LEARN_REST_MS (a stomp on the toe switch passes through the range first), else -1. */
#define LEARN_REST_MS 300
void expression_learn_start(struct expression *x);
int expression_learn(struct expression *x, const struct rig *r, const struct globals *g, const int raw[2]);

/* The pedal's current value, 0..255 (for calibrating). */
int expression_value(const struct expression *x, int n);

/* A preset was loaded: its targets take the pedals' current positions (the original's load does the same). */
void expression_apply(const struct expression *x, struct rig *r);
