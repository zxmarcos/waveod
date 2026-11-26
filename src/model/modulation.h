/*
 * The main loop's side of the modulators (engine/modulators.h, docs/modulators.md): the parameter links of the
 * modulator keys (CK_MOD1..) in the preset's control table become the engine's routes, with each parameter's range
 * and steps, whenever the preset, its blocks, its links or its modulators change.
 */
#pragma once

#include "model/rig.h"
#include "engine/modulators.h"

/* The routes of the edited preset (links of modulators that are on, to parameters of used slots); how many, at most
   max. */
int modulation_routes(const struct rig *r, struct mod_route *out, int max);

/* Hand the edited preset's modulators and routes to the engine; `restart` starts them from the beginning (a preset
   loaded). rig.c calls it when the chain is built, modulation_poll when anything else changed. */
void modulation_update(const struct rig *r, int restart);

/* Main loop: modulation_update after a change the rig told (r->gen moved). */
void modulation_poll(const struct rig *r);
