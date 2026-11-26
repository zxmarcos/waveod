/*
 * Signal-flow view of the chain for the screens, from the preset's routing (model/route.h): every block in
 * chain order with its lane (PRE, A, B, POST; a series chain is all PRE) and its column inside the lane.
 */
#pragma once

#include "model/rig.h"
#include "model/route.h"

enum topo_lane { LANE_PRE = ROUTE_PRE, LANE_A = ROUTE_A, LANE_B = ROUTE_B, LANE_POST = ROUTE_POST };
enum topo_kind { NODE_EFFECT };

struct topo_node {
    int8_t pos;         /* chain position */
    int8_t slot;
    int8_t cell;        /* grid cell (route.h) */
    uint8_t lane;
    uint8_t col;        /* column inside its lane, counting blocks */
    uint8_t kind;
};

struct topology {
    struct topo_node node[PRESET_SLOTS];
    int count;
    int parallel;                       /* 1 when lanes A / B exist (parallel or dual) */
    int dual;                           /* lanes A / B go to the left / right outputs, no POST */
    int cols[4];                        /* blocks per lane */
};

void topology_build(const struct rig *r, struct topology *t);
