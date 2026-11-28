/*
 * Application wiring: platform -> effect engine -> rig (model) -> UI.
 */
#pragma once

/* Bring everything up (audio included). */
void app_init(void);

/* One iteration of the main loop: controls, UI, housekeeping. */
void app_step(void);

/* waveod's settings in use (model/settings.h) and the rig (model/rig.h), for the simulator's --lang and preset: */
struct settings *app_settings(void);
struct rig *app_rig(void);
