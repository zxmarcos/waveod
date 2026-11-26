#include "engine/pools.h"

struct pool pools[POOL_COUNT] = {
    [POOL_CODE]   = { 0x00058000u, 0x00020000u, 0, 0 },
    [POOL_CONST]  = { 0x81301000u, 0x00020000u, 0, 0 },
    [POOL_STATE]  = { 0x20204000u, 0x00030000u, 0, 0 },
    [POOL_BUFFER] = { 0x81321000u, 0x006CF000u, 0, 0 },
};

#ifdef WAVEOD_HOST
#include <stdlib.h>

void pools_init(void)
{
    for (int i = 0; i < POOL_COUNT; i++)
        if (!pools[i].mem)
            pools[i].mem = calloc(1, pools[i].size);
}
#else
void pools_init(void)
{
    for (int i = 0; i < POOL_COUNT; i++)
        pools[i].mem = (uint8_t *)(uintptr_t)pools[i].addr;
}
#endif

static int side_in_use;

void pools_reset(void)
{
    for (int i = 0; i < POOL_COUNT; i++)
        pools[i].used = pools[i].top = 0;
    side_in_use = 0;
}

void pools_use(int side)
{
    side_in_use = side != 0;
    for (int i = 0; i < POOL_COUNT; i++) {
        if (side_in_use)
            pools[i].top = 0;
        else
            pools[i].used = 0;
    }
}

uint32_t pool_alloc(enum pool_id id, uint32_t size)
{
    struct pool *p = &pools[id];
    uint32_t low = (p->used + 7u) & ~7u;                /* the first free byte of the bottom side */
    uint32_t high = p->size - p->top;                   /* the end of the free space */
    if (low > high || size > high - low)
        return 0;
    if (!side_in_use) {
        p->used = low + size;
        return p->addr + low;
    }
    uint32_t start = (high - size) & ~7u;
    if (start < low)
        return 0;
    p->top = p->size - start;
    return p->addr + start;
}

uint32_t pool_room(enum pool_id id)
{
    const struct pool *p = &pools[id];
    const uint32_t low = (p->used + 7u) & ~7u, high = p->size - p->top;
    return low < high ? high - low : 0;
}

int pool_shared(enum pool_id id)
{
    return (side_in_use ? pools[id].used : pools[id].top) != 0;
}

uint8_t *pool_ptr(uint32_t addr, uint32_t len)
{
    for (int i = 0; i < POOL_COUNT; i++) {
        struct pool *p = &pools[i];
        if (addr >= p->addr && addr - p->addr <= p->size && len <= p->size - (addr - p->addr))
            return p->mem + (addr - p->addr);
    }
    return 0;
}
