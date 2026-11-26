#include "model/route.h"
#include <string.h>

int route_lanes_end(const struct preset_route *r)
{
    return r->mode == ROUTE_DUAL ? ROUTE_COLS : r->merge;
}

int route_lane(const struct preset_route *r, int cell)
{
    if (cell < 0 || cell >= ROUTE_CELLS)
        return ROUTE_HIDDEN;
    if (r->mode == ROUTE_SERIAL)
        return ROUTE_PRE;
    int col = cell % ROUTE_COLS;
    if (cell >= ROUTE_COLS)
        return ROUTE_B;
    return col < r->split ? ROUTE_PRE : col < route_lanes_end(r) ? ROUTE_A : ROUTE_POST;
}

int route_path(const struct preset_route *r, int8_t out[ROUTE_CELLS])
{
    int n = 0;
    if (r->mode == ROUTE_SERIAL) {
        for (int c = 0; c < ROUTE_CELLS; c++)
            out[n++] = (int8_t)c;
        return n;
    }
    int end = route_lanes_end(r);
    for (int c = 0; c < end; c++)                       /* PRE and lane A */
        out[n++] = (int8_t)c;
    for (int c = 0; c < ROUTE_COLS; c++)                /* lane B: the second row */
        out[n++] = (int8_t)(ROUTE_COLS + c);
    for (int c = end; c < ROUTE_COLS; c++)              /* POST */
        out[n++] = (int8_t)c;
    return n;
}

int route_slot_at(const struct preset *p, int cell)
{
    if (cell < 0)
        return -1;
    for (int s = 0; s < PRESET_SLOTS; s++)
        if (p->route.cell[s] == cell && p->slot[s].menu >= 0)
            return s;
    return -1;
}

void route_counts(const struct preset *p, uint8_t counts[ROUTE_LANES])
{
    memset(counts, 0, ROUTE_LANES);
    for (int pos = 0; pos < p->count; pos++) {
        int slot = p->order[pos];
        int lane = slot >= 0 && slot < PRESET_SLOTS ? route_lane(&p->route, p->route.cell[slot]) : ROUTE_HIDDEN;
        counts[lane < 0 ? ROUTE_PRE : lane]++;
    }
}

void route_apply_order(struct preset *p)
{
    int8_t path[ROUTE_CELLS];
    int n = route_path(&p->route, path), count = 0;
    for (int i = 0; i < n; i++) {
        int slot = route_slot_at(p, path[i]);
        if (slot >= 0)
            p->order[count++] = (int8_t)slot;
    }
    for (int i = count; i < PRESET_SLOTS; i++)
        p->order[i] = -1;
    p->count = (uint8_t)count;
}

/* the blocks keep their lanes (a block without a usable cell joins the lane of the one before it) and are
   packed from the start of each lane in chain order; when a lane overflows the chain goes back to series */
static void repack(struct preset *p, const uint8_t valid[PRESET_SLOTS])
{
    struct preset_route *r = &p->route;
    int8_t path[ROUTE_CELLS], lane_cells[ROUTE_LANES][ROUTE_CELLS];
    int n = route_path(r, path), lane_n[ROUTE_LANES] = { 0 }, used[ROUTE_LANES] = { 0 };
    for (int i = 0; i < n; i++) {
        int l = route_lane(r, path[i]);
        lane_cells[l][lane_n[l]++] = path[i];
    }
    int8_t lane_of[PRESET_SLOTS];
    int prev = r->mode == ROUTE_SERIAL || r->split > 0 ? ROUTE_PRE : ROUTE_A;
    for (int pos = 0; pos < p->count; pos++) {
        int slot = p->order[pos];
        int l = valid[slot] ? route_lane(r, r->cell[slot]) : prev;
        if (l < prev)
            l = prev;
        lane_of[pos] = (int8_t)l;
        prev = l;
    }
    for (int pos = 0; pos < p->count; pos++) {
        int l = lane_of[pos];
        if (used[l] >= lane_n[l]) {                     /* does not fit: series, in order */
            struct preset_route keep = *r;
            preset_route_default(p, r);
            r->stored = keep.stored;
            r->split = keep.split;
            r->merge = keep.merge;
            memcpy(r->joint, keep.joint, sizeof r->joint);
            return;
        }
        r->cell[p->order[pos]] = lane_cells[l][used[l]++];
    }
}

void route_fit(struct preset *p)
{
    struct preset_route *r = &p->route;
    if (r->mode >= ROUTE_MODES || r->split >= ROUTE_COLS || r->merge <= r->split || r->merge > ROUTE_COLS) {
        preset_route_default(p, r);
        return;
    }
    uint8_t in_chain[PRESET_SLOTS] = { 0 }, valid[PRESET_SLOTS] = { 0 };
    for (int pos = 0; pos < p->count; pos++)
        if (p->order[pos] >= 0 && p->order[pos] < PRESET_SLOTS)
            in_chain[p->order[pos]] = 1;
    uint32_t seen = 0;
    int ok = 1;
    for (int s = 0; s < PRESET_SLOTS; s++) {
        if (!in_chain[s]) {
            r->cell[s] = -1;
            continue;
        }
        int c = r->cell[s];
        if (route_lane(r, c) == ROUTE_HIDDEN || (seen & (1u << c))) {
            ok = 0;
            continue;
        }
        seen |= 1u << c;
        valid[s] = 1;
    }
    if (ok) {
        int8_t order[PRESET_SLOTS];
        int count = p->count;
        memcpy(order, p->order, sizeof order);
        route_apply_order(p);
        if (p->count == count && memcmp(order, p->order, (size_t)count) == 0)
            return;
        memcpy(p->order, order, sizeof order);          /* changed by position: keep that order */
        p->count = (uint8_t)count;
    }
    repack(p, valid);
}

void route_split_range(const struct preset *p, int *lo, int *hi)
{
    *lo = 0;
    *hi = p->route.mode == ROUTE_DUAL ? ROUTE_COLS - 1 : p->route.merge - 1;
}

void route_merge_range(const struct preset *p, int *lo, int *hi)
{
    *lo = p->route.split + 1;
    *hi = ROUTE_COLS;
}

int route_set(struct preset *p, int mode, int split, int merge)
{
    struct preset_route *r = &p->route;
    if (mode < 0 || mode >= ROUTE_MODES)
        return 0;
    if (mode == ROUTE_SERIAL) {
        split = r->split;
        merge = r->merge;
    } else {
        if (mode == ROUTE_PARALLEL) {
            if (merge > ROUTE_COLS)
                merge = ROUTE_COLS;
            if (merge < split + 1)
                merge = split + 1;
            if (merge > ROUTE_COLS)
                merge = ROUTE_COLS;
            if (split >= merge)
                split = merge - 1;
        } else if (merge <= split) {
            merge = split + 1;                          /* not drawn in dual mode, kept for going back */
        }
        if (split > ROUTE_COLS - 1)
            split = ROUTE_COLS - 1;
        if (split < 0)
            split = 0;
    }
    if (r->mode == mode && r->split == split && r->merge == merge)
        return 0;
    r->mode = (uint8_t)mode;
    r->split = (uint8_t)split;
    r->merge = (uint8_t)merge;
    route_apply_order(p);
    return 1;
}
