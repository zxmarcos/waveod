/*
 * Start-up after a crash (platform/safety.h): which preset not to load, and what to tell the user.
 *
 * The power-up preset is skipped (safe mode: an empty chain at its place) when one of its modules crashed soon
 * after the start-up, when the pedal crashed twice in a row with it loaded, or when EXIT is held at power-up.
 */
#pragma once

#include "model/rig.h"

/* RIG_AVOID_x or a preset index, for rig_init */
int recovery_avoid(void);

/* After the UI is up: explain a crash reset or the safe mode (`started_safe` = rig_init returned 1). */
void recovery_report(const struct rig *r, int started_safe);
