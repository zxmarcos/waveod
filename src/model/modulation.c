#include "model/modulation.h"
#include "model/control.h"
#include "platform/sdram.h"
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

#define FEW_STEPS 20                    /* a parameter with fewer steps than this jumps from step to step */

int modulation_routes(const struct rig *r, struct mod_route *out, int max)
{
    const struct preset_controls *c = &r->preset.controls;
    int n = 0;
    for (int i = 0; i < c->count && n < max; i++) {
        const struct control *l = &c->c[i];
        if (!CK_IS_MOD(l->key) || l->action != CA_PARAM)
            continue;
        int knob = control_knob(r, l);
        struct rig_param p;
        if (knob < 0 || control_param_index(r, l, &p) < 0)
            continue;
        if (r->preset.mods.m[l->key - CK_MOD1].type == MOD_OFF)
            continue;
        struct mod_route *m = &out[n++];
        m->mod = (uint8_t)(l->key - CK_MOD1);
        m->slot = (uint8_t)l->slot;
        m->knob = (uint8_t)knob;
        m->curve = l->curve;
        m->off = l->off;
        m->on = l->on;
        m->min = p.def.min < p.def.max ? p.def.min : p.def.max;
        m->max = p.def.min < p.def.max ? p.def.max : p.def.min;
        float step = p.def.step > 0.0f ? p.def.step : 1.0f;
        m->step = (m->max - m->min) / step < (float)FEW_STEPS ? step : 0.0f;
    }
    return n;
}

static uint32_t seen_gen, seen_loads;

void modulation_update(const struct rig *r, int restart)
{
    static struct mod_route routes[MOD_ROUTES] SDRAM_BSS;
    int n = modulation_routes(r, routes, MOD_ROUTES);
    mods_set(&r->preset.mods, routes, n);
    if (restart)
        mods_restart();
    seen_gen = r->gen;
    seen_loads = r->loads;
}

void modulation_poll(const struct rig *r)
{
    if (r->gen != seen_gen || r->loads != seen_loads)
        modulation_update(r, r->loads != seen_loads);
}
