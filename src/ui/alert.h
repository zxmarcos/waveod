/*
 * A warning on top of every screen (the original's "CAUTION!" screen 0x0F): a title, a message and the hint
 * to dismiss it. While it is shown it takes every control event; any button press or a tap closes it.
 */
#pragma once

#include "input/input.h"

void alert_show(const char *title, const char *text);
int alert_shown(void);

/* Something else was put on the top layer (a notice): the alert stays above it. */
void alert_keep_on_top(void);

/* Give a control event to the alert; 1 = taken (always while it is shown). */
int alert_input(const struct input_event *ev);
