/*
 * Diagnostics on top of the screen (opened from the settings): CPU clock, touch controller, flash controller
 * set-up and the last flash error.
 */
#pragma once

void diag_toggle(void);
int diag_shown(void);
void diag_tick(void);
