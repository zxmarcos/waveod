/*
 * Footswitches A-D and BANK -/+ with the preset's control table (fw/preset_format.h, model/control.c): each key has
 * links for a press (run when it is released), a long press and while it is down (momentary), per footswitch
 * mode; two neighbouring keys pressed together (A+B, B+C, C+D, BANK-+BANK+) run the pair's links when released.
 * Bank and preset steps repeat while held. The functions themselves are the original's (FUN_0002693C).
 *
 * Factory presets: stomp mode A-D = CTRL 1-4 (D = tap tempo on some), preset mode A-D = presets A-D of the
 * bank, BANK -/+ = bank down / up, A+B = tuner, C+D = looper, BANK-+BANK+ = the next mode.
 *
 * waveod's scene mode (CENAS tab), converted presets: A-D = scenes 1-4, BANK -/+ = previous / next preset (held:
 * repeat), the pairs as in preset mode. The modes go round preset -> stomp -> scenes.
 */
#pragma once

#include "model/control.h"
#include "ui/ui_internal.h"

/* 1 when the event belonged to a footswitch. */
int footswitch_input(struct ui_ctx *ui, const struct input_event *ev);

/* Run what pressing key k (0..3 = A..D) does in the current mode, e.g. from a touch on its card. */
void footswitch_press(struct ui_ctx *ui, int key);

/* Repeats; call from the UI tick. */
void footswitch_tick(struct ui_ctx *ui);

/* Tap tempo (footswitch function, MIDI CC 70): the tempo from the time since the last tap. */
void footswitch_tap(struct ui_ctx *ui);

/* The current footswitch mode as an enum control_mode bit (what the preset's links are looked up with). */
int footswitch_mode_bit(const struct ui_ctx *ui);

/* enum fs_mode: what the footswitches do from now on, kept as the mode the pedal starts in (preset / stomp:
   global +0x2F). The screen does not change: ui_set_mode shows the mode's tab when a mode tab is shown. */
void footswitch_set_mode(struct ui_ctx *ui, int mode);
int footswitch_mode(const struct ui_ctx *ui);

/* Run one of the original's footswitch functions (enum fs_function); CTRL 1-4 = stomp mode's key A-D (the
   external footswitches, MIDI CC 71-74). */
void footswitch_run(struct ui_ctx *ui, int fn);

/* A function or scene link of a pressed key (control_run_fn, ctx = the ui_ctx): footswitches, the toe switch. */
void footswitch_link(void *ctx, const struct control *link);

/* Recall scene n (footswitch, touch, MIDI). */
void footswitch_scene(struct ui_ctx *ui, int n);
