/* The routing grid (model/route.c) and where it is stored in the preset record. */
#include "fw/presets.h"
#include "model/route.h"
#include "test.h"
#include <string.h>

/* blocks (menu 1) in the given cells, slot i in cells[i] */
static void grid(struct preset *p, const int8_t *cells, int n, int mode, int split, int merge)
{
    preset_clear(p, "R");
    for (int i = 0; i < n; i++) {
        p->slot[i].menu = 1;
        p->route.cell[i] = cells[i];
    }
    p->route.mode = (uint8_t)mode;
    p->route.split = (uint8_t)split;
    p->route.merge = (uint8_t)merge;
    route_apply_order(p);
}

static int order_is(const struct preset *p, const int8_t *want, int n)
{
    return p->count == n && memcmp(p->order, want, (size_t)n) == 0;
}

void test_route(void)
{
    static struct preset_source src;
    static struct preset p, q;
    static uint8_t rec[PRESET_RECORD_SIZE];

    /* a stored preset without routing: the series of its order, and its record untouched */
    presets_open(&src);
    CHECK(presets_load(&src, 0, &p) == PRESET_FACTORY, "factory preset 01A");
    int n = p.count;
    CHECK(!p.route.stored && p.route.mode == ROUTE_SERIAL && p.route.cell[p.order[0]] == 0 &&
          p.route.cell[p.order[n - 1]] == n - 1, "01A: series, order filling the grid");
    int8_t before[PRESET_SLOTS];
    memcpy(before, p.order, sizeof before);
    route_fit(&p);
    CHECK(order_is(&p, before, n), "route_fit keeps a series preset as it is");
    preset_encode(&p, 0, rec);
    CHECK(memcmp(rec + 0x3d4, p.raw + 0x3d4, 5 * 0x18) == 0, "a series preset keeps the original's CTRL bytes");

    /* parallel: PRE | A over B | POST */
    static const int8_t cells[] = { 0, 2, 3, ROUTE_COLS + 2, 4, 5 };
    grid(&p, cells, 6, ROUTE_PARALLEL, 2, 4);
    static const int8_t par[] = { 0, 1, 2, 3, 4, 5 };
    CHECK(order_is(&p, par, 6), "parallel order: PRE, lane A, lane B, POST");
    uint8_t lanes[ROUTE_LANES];
    route_counts(&p, lanes);
    CHECK(lanes[0] == 1 && lanes[1] == 2 && lanes[2] == 1 && lanes[3] == 2, "parallel lanes %d %d %d %d", lanes[0],
          lanes[1], lanes[2], lanes[3]);
    CHECK(route_lane(&p.route, ROUTE_COLS) == ROUTE_B && route_lane(&p.route, ROUTE_CELLS - 1) == ROUTE_B,
          "lane B is the whole second row");
    int lo, hi;
    route_split_range(&p, &lo, &hi);
    CHECK(lo == 0 && hi == 3, "the split stays before the merge (%d..%d)", lo, hi);
    route_merge_range(&p, &lo, &hi);
    CHECK(lo == 3 && hi == ROUTE_COLS, "the merge stays after the split (%d..%d)", lo, hi);

    /* dual: the lanes run to the end */
    CHECK(route_set(&p, ROUTE_DUAL, 2, 4), "to dual");
    static const int8_t dual[] = { 0, 1, 2, 4, 5, 3 };
    route_counts(&p, lanes);
    CHECK(order_is(&p, dual, 6) && lanes[0] == 1 && lanes[1] == 4 && lanes[2] == 1 && lanes[3] == 0,
          "dual: row 1 after the split to the left output, row 2 to the right");

    /* series: row 1 then row 2 */
    CHECK(route_set(&p, ROUTE_SERIAL, 0, 0), "to series");
    static const int8_t ser[] = { 0, 1, 2, 4, 5, 3 };
    CHECK(order_is(&p, ser, 6) && p.route.split == 2 && p.route.merge == 4, "series, the joints kept for later");

    /* the merge always after the split */
    route_set(&p, ROUTE_PARALLEL, 5, 3);
    CHECK(p.route.split == 5 && p.route.merge == 6, "split %d merge %d", p.route.split, p.route.merge);

    /* stored in the record and read back */
    grid(&p, cells, 6, ROUTE_PARALLEL, 2, 4);
    p.route.joint[JOINT_PAN_A] = 20;
    preset_encode(&p, 5, rec);
    CHECK(preset_decode(rec, &q) == 0 && q.route.stored && q.route.mode == ROUTE_PARALLEL && q.route.split == 2 &&
              q.route.merge == 4 && q.route.joint[JOINT_PAN_A] == 20 && memcmp(q.route.cell, p.route.cell, 6) == 0,
          "routing stored in the CTRL display bytes");
    rec[0x3d4 + 12] ^= 1;
    CHECK(preset_decode(rec, &q) == 0 && !q.route.stored && q.route.mode == ROUTE_SERIAL,
          "a changed byte (the original rewrote them): series of the order");

    /* changed by position (the PC editor): the blocks keep their lanes, packed in the new order */
    grid(&p, cells, 6, ROUTE_PARALLEL, 2, 4);
    p.order[1] = 2;
    p.order[2] = 1;
    route_fit(&p);
    CHECK(p.route.cell[2] == 2 && p.route.cell[1] == 3 && p.order[1] == 2, "moved inside lane A");

    grid(&p, cells, 6, ROUTE_PARALLEL, 2, 4);
    p.slot[6].menu = 1;
    p.route.cell[6] = -1;
    memmove(p.order + 5, p.order + 4, 2);       /* new block after lane B's */
    p.order[4] = 6;
    p.count = 7;
    route_fit(&p);
    CHECK(p.route.cell[6] == ROUTE_COLS + 1 && route_lane(&p.route, p.route.cell[6]) == ROUTE_B,
          "a block inserted by position joins the lane before it");

    static const int8_t tight[] = { 0, 2, ROUTE_COLS + 2 };
    grid(&p, tight, 3, ROUTE_PARALLEL, 2, 3);
    p.slot[3].menu = 1;
    p.route.cell[3] = -1;
    p.order[3] = p.order[2];                    /* new block after lane A's only one */
    p.order[2] = 3;
    p.count = 4;
    route_fit(&p);
    CHECK(p.route.mode == ROUTE_SERIAL && p.count == 4 && p.route.cell[3] == 2, "a full lane: back to series");

    /* a routing that makes no sense (zeroed) is laid out again */
    grid(&p, cells, 6, ROUTE_PARALLEL, 2, 4);
    memset(&p.route, 0, sizeof p.route);
    route_fit(&p);
    CHECK(p.route.mode == ROUTE_SERIAL && p.route.joint[JOINT_LEVEL_A] == 100 && p.route.cell[p.order[5]] == 5,
          "zeroed routing: series with the default joints");
}
