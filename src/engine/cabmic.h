/*
 * The virtual mic of waveod's Cab / IR engine (engine/cabir.h): where a mic in front of the speaker would be,
 * applied to an IR captured at one place.
 *
 * The curves (cabmic_tables.c, tools/gen_cabmic.py) are how the response changes when the mic moves, measured
 * on banks of real cab IR captures (medians over 46 cabs x 12 mics; how well they predict real captures:
 * docs/cab.md): 4 positions from the dust cap to the edge of the cone, 6 distances from the grille outwards, on axis and 45 degrees. A factory IR is
 * taken to be captured at the second position and the second distance, on axis.
 *
 * Knobs (in the slot's knob array, free in the original's Cab / IR entries; 0 = the IR as captured, so older
 * presets sound as before):
 *   POSITION  -100 (centre of the dust cap) .. 0 .. +100 (edge of the cone)
 *   DISTANCE  -100 (on the grille) .. 0 .. +100 (far)
 *   ANGLE     0 .. 45 degrees
 *   TYPE      0 = the mic the IR was captured with, 1.. = cabmic_type_names[TYPE - 1]
 * The change is turned into a minimum-phase filter (real cepstrum, 2048 points) and convolved into the IR.
 *
 * A mic type is that mic's response against the median of the 12 (the mic of a factory IR is not known;
 * the factory IRs look like that median, not like any one mic: docs/cab.md), and with a type the
 * position / distance / angle curves are that mic's own.
 */
#pragma once

#include <stdint.h>

#define CABMIC_BANDS        29                  /* third octaves, 25 Hz .. 16 kHz */
#define CABMIC_POSITIONS    4
#define CABMIC_DISTANCES    6
#define CABMIC_REF          1                   /* position / distance step of a factory IR */
#define CABMIC_TYPES        1                   /* mic types besides the captured one (tools/gen_cabmic.py --mics) */
#define CABMIC_PARAMS       4                   /* knobs of the virtual mic */

enum { CABMIC_KNOB_POSITION = 8, CABMIC_KNOB_DISTANCE = 9, CABMIC_KNOB_ANGLE = 10, CABMIC_KNOB_TYPE = 11 };

extern const float cabmic_bands[CABMIC_BANDS];
extern const float cabmic_position[CABMIC_POSITIONS][CABMIC_BANDS];
extern const float cabmic_distance[CABMIC_DISTANCES][CABMIC_BANDS];
extern const float cabmic_angle[2][CABMIC_BANDS];
extern const char *const cabmic_type_names[CABMIC_TYPES];
extern const float cabmic_type_curve[CABMIC_TYPES][CABMIC_BANDS];
extern const float cabmic_type_position[CABMIC_TYPES][CABMIC_POSITIONS][CABMIC_BANDS];
extern const float cabmic_type_distance[CABMIC_TYPES][CABMIC_DISTANCES][CABMIC_BANDS];
extern const float cabmic_type_angle[CABMIC_TYPES][2][CABMIC_BANDS];

/* The mic type of the knobs: 0 = as captured, 1..CABMIC_TYPES. */
int cabmic_type(const float *knobs);

/* The step (0..steps - 1, fractional) a POSITION / DISTANCE knob stands for, and the knob of a step. */
float cabmic_step(float knob, int steps);
float cabmic_knob(float step, int steps);

/* 1 when the knobs leave the IR as it is */
int cabmic_neutral(const float *knobs);

/* The change the knobs make, dB per band (relative to the IR as captured). */
void cabmic_curve(const float *knobs, float db[CABMIC_BANDS]);

/* The change at frequency hz (the bands interpolated on a log scale), dB. */
float cabmic_db_at(const float db[CABMIC_BANDS], float hz);

/* The IR (n int32, 24-bit) with the change applied, as floats of the same scale (x 2^-23 applied): out gets
   `n` samples. Main loop only (shared scratch). */
void cabmic_apply(const int32_t *ir, int n, const float db[CABMIC_BANDS], float *out);
