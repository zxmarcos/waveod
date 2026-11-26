/* The spillover's copy of a chain (chain.h, chain_tail): no hardware, no modules run, so the host tests use it. */
#include "engine/chain.h"

static int is_tail(const struct chain_slot *s)
{
    return (s->menu == MENU_DLY || s->menu == MENU_RVB) && s->enabled && s->status == MODULE_OK;
}

int chain_tail(const struct chain *c, struct chain *tail)
{
    *tail = *c;
    uint16_t keep = 0;
    int pos = 0, seen = 0, any = 0;
    /* serial: one path; otherwise PRE, then lanes A and B each from what PRE had, then POST after either */
    int lanes = c->mode == ROUTE_SERIAL ? 1 : 4;
    int seen_pre = 0, seen_lane = 0;
    for (int lane = 0; lane < lanes; lane++) {
        int n = lanes == 1 ? c->count : c->lane_n[lane];
        if (lane == 1 || lane == 2)
            seen = seen_pre;
        else if (lane == 3)
            seen = seen_lane;
        for (int end = pos + n; pos < end && pos < c->count; pos++) {
            int i = c->order[pos];
            if (i < 0 || i >= PRESET_SLOTS)
                continue;
            if (is_tail(&c->slot[i]))
                seen = any = 1;
            if (seen)
                keep |= (uint16_t)(1u << i);
        }
        if (lane == 0)
            seen_pre = seen;
        else if (lane == 1 || lane == 2)
            seen_lane |= seen;
    }
    tail->skip = (uint16_t)~keep;
    for (int i = 0; i < CHAIN_RAMPS; i++)
        if (tail->ramp[i].left)                 /* a knob still moving ends where it was going */
            tail->slot[tail->ramp[i].slot].knobs[tail->ramp[i].knob] = tail->ramp[i].target;
    for (int i = 0; i < CHAIN_RAMPS; i++)
        tail->ramp[i].left = 0;
    return any;
}
