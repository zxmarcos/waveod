/*
 * The chain's routing grid (fw/preset_format.h, struct preset_route): 2 rows x ROUTE_COLS cells, one per slot,
 * all of them in use in every mode. The signal passes the cells along a path that depends on the mode:
 *
 *   series    row 1 left to right, then row 2 left to right
 *   parallel  row 1 before the split (PRE), then lane A = row 1 from the split to the merge and lane B = the
 *             whole row 2, mixed at the merge, then row 1 after the merge (POST)
 *   dual L/R  PRE as above, then lane A = row 1 from the split to the end (left output) and lane B = row 2
 *             (right output)
 *
 * The chain order (preset.order) is always the blocks in path order; the routing only adds where the lanes
 * start and end, and which cells are empty.
 */
#pragma once

#include "fw/preset_format.h"

enum route_lane { ROUTE_PRE, ROUTE_A, ROUTE_B, ROUTE_POST, ROUTE_LANES };

#define ROUTE_HIDDEN (-1)

/* Lane of a cell (ROUTE_PRE for every cell in series), ROUTE_HIDDEN for no cell. */
int route_lane(const struct preset_route *r, int cell);

/* The cells in the order the signal passes them; returns how many. */
int route_path(const struct preset_route *r, int8_t out[ROUTE_CELLS]);

/* Column after lane A: the merge (ROUTE_COLS in dual mode). */
int route_lanes_end(const struct preset_route *r);

/* Slot in a cell, -1 = empty. */
int route_slot_at(const struct preset *p, int cell);

/* Blocks of each lane in chain order (for the audio engine). */
void route_counts(const struct preset *p, uint8_t counts[ROUTE_LANES]);

/* preset.order / count from the cells. */
void route_apply_order(struct preset *p);

/* Make the routing agree with the preset: a missing or broken one is laid out from `order`; when `order` was
   changed by position (the PC editor) the blocks keep their lanes and are packed in order. */
void route_fit(struct preset *p);

/* Where the split / merge may go now. */
void route_split_range(const struct preset *p, int *lo, int *hi);
void route_merge_range(const struct preset *p, int *lo, int *hi);

/* Change mode and joints, kept in range; then the order follows. 1 = something changed. */
int route_set(struct preset *p, int mode, int split, int merge);
