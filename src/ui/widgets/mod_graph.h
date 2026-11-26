/*
 * A modulator drawn as it plays (fw/preset_format.h struct preset_mod; engine/modulators.h), for the modulators
 * screen (big, with a caption above) and the MODULADORES page of CONTROLES (small, one per card):
 *   LFO (sine, triangle, square, saws)   two cycles of its shape, the start phase marked, the point where it is
 *                                        now running along them
 *   LFO random / smooth, ENVELOPE        what it did over the last seconds, the newest on the right
 *   SEQUÊNCIA                            a bar per step, the one playing lit, the glides as ramps, one step outlined
 * Nothing for a modulator that is off. mod_graph_tick keeps the history and redraws; call it at a steady rate
 * (MOD_GRAPH_TICK_MS) while the drawing is shown.
 */
#pragma once

#include "ui/ui_internal.h"

#define MOD_GRAPH_HIST    150               /* values kept for the scrolling drawings */
#define MOD_GRAPH_TICK_MS 40                /* 25 a second: ~6 s of history */

struct mod_graph {
    lv_obj_t *box;
    struct ui_ctx *ui;
    int mod;                                /* 0..MOD_COUNT-1 */
    int top, pad;                           /* px above the curve (a caption), around it */
    int small;                              /* thinner lines, smaller point */
    int outline_step;                       /* SEQ: the step outlined, -1 = none */
    float hist[MOD_GRAPH_HIST];
    int hist_n, hist_at;
    uint8_t hist_type, hist_shape;          /* what the history is of (a change starts it again) */
};

/* The drawing in a box of its own (no border), on modulator `mod` of ui's preset. */
void mod_graph_create(struct mod_graph *g, lv_obj_t *parent, int x, int y, int w, int h, struct ui_ctx *ui, int mod,
                      int top, int small);

/* Another modulator: the history starts again. */
void mod_graph_set_mod(struct mod_graph *g, int mod);

/* One more value in the history (when the modulator is on) and a redraw. */
void mod_graph_tick(struct mod_graph *g);

/* SEQ: the step under display point x (-1 = outside), and the value 0..100 (5 % steps) at y. */
int mod_graph_step_at(const struct mod_graph *g, int x);
int mod_graph_value_at(const struct mod_graph *g, int y);
