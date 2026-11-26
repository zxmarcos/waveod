#include "engine/overload.h"

int overload_block(struct overload_guard *g, float ms)
{
    if (ms <= OVERLOAD_MS) {
        g->slow = 0;
        return 0;
    }
    if (++g->slow < OVERLOAD_BLOCKS)
        return 0;
    g->slow = 0;
    return 1;
}
