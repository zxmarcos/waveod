/*
 * Application wiring: platform -> effect engine -> rig (model) -> UI.
 */
#pragma once

/* Bring everything up (audio included). */
void app_init(void);

/* One iteration of the main loop: controls, UI, housekeeping. */
void app_step(void);
