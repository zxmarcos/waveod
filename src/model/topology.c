#include "model/topology.h"
#include <string.h>

void topology_build(const struct rig *r, struct topology *t)
{
    const struct preset *p = &r->preset;
    const struct preset_route *rt = &p->route;
    memset(t, 0, sizeof *t);
    t->parallel = rt->mode != ROUTE_SERIAL;
    t->dual = rt->mode == ROUTE_DUAL;
    for (int pos = 0; pos < p->count; pos++) {
        struct topo_node *n = &t->node[t->count++];
        n->pos = (int8_t)pos;
        n->slot = p->order[pos];
        n->cell = n->slot >= 0 && n->slot < PRESET_SLOTS ? rt->cell[n->slot] : -1;
        n->kind = NODE_EFFECT;
        int lane = route_lane(rt, n->cell);
        n->lane = (uint8_t)(lane < 0 ? LANE_PRE : lane);
        n->col = (uint8_t)t->cols[n->lane]++;
    }
}
