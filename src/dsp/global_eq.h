/*
 * The original's global EQ (FUN_00029634), after the effect chain: low cut, low shelf, four peaking bands,
 * high shelf and high cut in series, each with its own switch, then the level (50 = unity, ramped by 1e-4 per
 * sample). The parameters are the 31 floats of the settings record (fw/globals_format.h, enum eq_param).
 *
 * Coefficients are recomputed when a stage's frequency, Q or gain changes, whether the EQ is on or not; the
 * filters and the level only run while it is on.
 */
#pragma once

#include "dsp/biquad.h"
#include "fw/globals_format.h"

#define GEQ_STAGES 8

struct global_eq {
    float seen[EQ_PARAMS];          /* parameters the coefficients were computed for */
    int valid;
    struct biquad_coef coef[GEQ_STAGES];
    struct biquad_state state[GEQ_STAGES][2];
    float level;                    /* current gain (target = EQ_LEVEL / 100) */
};

void global_eq_init(struct global_eq *eq);

/* Process `frames` interleaved stereo frames in place (nothing changes while `on` is 0). */
void global_eq_process(struct global_eq *eq, float *io, int frames, const float params[EQ_PARAMS], int on);

/* Magnitude in dB of the stages that are switched on, at `hz` (for drawing the curve). */
float global_eq_response_db(const float params[EQ_PARAMS], float hz);
