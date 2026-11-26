/*
 * Spillover: the pools filled from both ends (engine/pools.c), the slots a tail keeps (engine/chain_tail.c),
 * and whether two factory presets fit side by side. The tails' sound is checked in the emulator
 * (tools/check_spill.py).
 */
#include "engine/cabir.h"
#include "engine/chain.h"
#include "sdk/wvfx.h"
#include "engine/module_loader.h"
#include "engine/pools.h"
#include "fw/catalog.h"
#include "fw/presets.h"
#include "test.h"
#include <stdio.h>
#include <string.h>

static void test_sides(void)
{
    pools_init();
    pools_reset();
    struct pool *p = &pools[POOL_STATE];
    uint32_t a = pool_alloc(POOL_STATE, 100);
    CHECK(a == p->addr, "side 0 starts at the start: %#x", a);
    pools_use(1);
    CHECK(p->used == 100, "side 0 kept: %u", p->used);
    uint32_t b = pool_alloc(POOL_STATE, 100), c = pool_alloc(POOL_STATE, 13);
    CHECK(b == p->addr + p->size - 104 && !(b & 7), "side 1 from the end down, 8-aligned: %#x", b - p->addr);
    CHECK(c + 13 <= b && !(c & 7), "next one below it: %#x", c - p->addr);
    /* side 1 full up to side 0 */
    uint32_t room = c - p->addr - 104;              /* above side 0's 100 bytes rounded up */
    CHECK(pool_alloc(POOL_STATE, room + 8) == 0, "no overlap with side 0");
    uint32_t d = pool_alloc(POOL_STATE, room);
    CHECK(d && d >= p->addr + 100, "exactly up to side 0: %#x", d ? d - p->addr : 0);
    /* side 0 again: side 1 kept, side 0 emptied, and it may grow only up to side 1 */
    pools_use(0);
    CHECK(p->used == 0 && p->top != 0, "pools_use(0) empties side 0 only");
    CHECK(pool_alloc(POOL_STATE, p->size - p->top + 8) == 0, "side 0 stops at side 1");
    CHECK(pool_alloc(POOL_STATE, 64) == p->addr, "side 0 from the start again");
    pools_reset();
    CHECK(p->used == 0 && p->top == 0, "pools_reset empties both");
}

/* a chain of `n` slots in path order with menus `menus` (on unless negative: -menu - 1 = off) */
static void chain_of(struct chain *c, const int *menus, int n, int mode, const uint8_t lanes[4])
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < PRESET_SLOTS; i++) {
        c->slot[i].menu = -1;
        c->order[i] = -1;
    }
    for (int k = 0; k < n; k++) {
        int slot = (k * 5) % PRESET_SLOTS;          /* slots not in path order */
        int m = menus[k] < 0 ? -menus[k] - 1 : menus[k];
        c->slot[slot].menu = (int8_t)m;
        c->slot[slot].enabled = menus[k] >= 0;
        c->slot[slot].status = MODULE_OK;
        c->order[k] = (int8_t)slot;
    }
    c->count = (uint8_t)n;
    c->mode = (uint8_t)mode;
    if (lanes)
        memcpy(c->lane_n, lanes, 4);
}

/* which path positions the tail keeps, as a string of 0 / 1 */
static void kept(const struct chain *t, char *out)
{
    for (int k = 0; k < t->count; k++)
        out[k] = (char)('0' + !(t->skip >> t->order[k] & 1));
    out[t->count] = 0;
}

static void test_mask(void)
{
    static struct chain c, t;
    char s[16];
    const int serial[] = { MENU_DYN, MENU_DRV, MENU_AMP, MENU_CAB, MENU_DLY, MENU_EQ, MENU_RVB, MENU_VOL };
    chain_of(&c, serial, 8, ROUTE_SERIAL, 0);
    CHECK(chain_tail(&c, &t) == 1, "a delay on: something to spill");
    kept(&t, s);
    CHECK(strcmp(s, "00001111") == 0, "serial: from the delay on (%s)", s);
    CHECK(c.skip == 0, "the running chain untouched");

    const int off[] = { MENU_AMP, -MENU_DLY - 1, MENU_CAB, MENU_RVB, MENU_EQ };
    chain_of(&c, off, 5, ROUTE_SERIAL, 0);
    chain_tail(&c, &t);
    kept(&t, s);
    CHECK(strcmp(s, "00011") == 0, "a delay off does not count (%s)", s);

    const int none[] = { MENU_AMP, MENU_CAB, -MENU_RVB - 1, MENU_EQ };
    chain_of(&c, none, 4, ROUTE_SERIAL, 0);
    CHECK(chain_tail(&c, &t) == 0, "no delay or reverb on: nothing to spill");

    /* parallel: PRE amp, lane A cab + delay, lane B cab, POST eq + reverb */
    const int par[] = { MENU_AMP, MENU_CAB, MENU_DLY, MENU_CAB, MENU_EQ, MENU_RVB };
    const uint8_t lanes[4] = { 1, 2, 1, 2 };
    chain_of(&c, par, 6, ROUTE_PARALLEL, lanes);
    chain_tail(&c, &t);
    kept(&t, s);
    CHECK(strcmp(s, "001011") == 0, "parallel: lane A from its delay, not lane B, POST after it (%s)", s);

    /* a delay in PRE: both lanes and POST */
    const int pre[] = { MENU_DLY, MENU_CAB, MENU_MOD, MENU_EQ };
    const uint8_t lanes2[4] = { 1, 1, 1, 1 };
    chain_of(&c, pre, 4, ROUTE_PARALLEL, lanes2);
    chain_tail(&c, &t);
    kept(&t, s);
    CHECK(strcmp(s, "1111") == 0, "parallel: a delay before the split keeps both lanes (%s)", s);

    /* dual: lane B's reverb only; lane A runs nothing */
    const int dual[] = { MENU_AMP, MENU_CAB, MENU_RVB, MENU_EQ };
    const uint8_t lanes3[4] = { 1, 1, 2, 0 };
    chain_of(&c, dual, 4, ROUTE_DUAL, lanes3);
    chain_tail(&c, &t);
    kept(&t, s);
    CHECK(strcmp(s, "0011") == 0, "dual: lane B from its reverb (%s)", s);

    /* a ramp that was running ends at its target in the copy, and does not run there */
    chain_of(&c, serial, 8, ROUTE_SERIAL, 0);
    c.slot[c.order[4]].knobs[2] = 10.0f;
    c.ramp[0].slot = c.order[4];
    c.ramp[0].knob = 2;
    c.ramp[0].target = 50.0f;
    c.ramp[0].left = 3;
    chain_tail(&c, &t);
    CHECK(t.slot[c.order[4]].knobs[2] == 50.0f && !t.ramp[0].left && c.ramp[0].left == 3,
          "a moving knob ends at its target in the tail only");
}

/* pool use of a preset: all of it, or its delays and reverbs */
static void use_of(const struct catalog *cat, const struct preset *p, uint32_t used[POOL_COUNT])
{
    pools_reset();
    for (int i = 0; i < PRESET_SLOTS; i++) {
        const struct preset_slot *s = &p->slot[i];
        int menu = s->menu, index = 0;
        if (menu < 0 || catalog_resolve(cat, &menu, s->id, &index) == RESOLVE_INVALID)
            continue;
        struct catalog_item item;
        struct module m;
        if (catalog_item(cat, menu, index, &item) != 0)
            continue;
        if (item.block) {                               /* a block of waveod's (the Cab / IR engine) */
            if (item.block->state_size)
                pool_alloc(POOL_STATE, item.block->state_size);
            if (item.block->buffer_size)
                pool_alloc(POOL_BUFFER, item.block->buffer_size);
        } else {
            module_load(item.engine.elf, item.engine.elf_size, (uint16_t)index, &m);
        }
    }
    for (int r = 0; r < POOL_COUNT; r++)
        used[r] = pools[r].used;
}

static void test_factory_pairs(void)
{
    static struct catalog cat;
    static struct preset_source src;
    static struct preset p;
    static uint32_t use[PRESET_COUNT][POOL_COUNT];
    if (catalog_open(&cat) != 0)
        return;
    presets_open(&src);
    uint32_t most[POOL_COUNT] = {0};
    for (int i = 0; i < PRESET_COUNT; i++) {
        presets_load(&src, i, &p);
        use_of(&cat, &p, use[i]);
        for (int r = 0; r < POOL_COUNT; r++)
            if (use[i][r] > most[r])
                most[r] = use[i][r];
    }
    int pairs = 0, fit = 0;
    for (int i = 0; i < PRESET_COUNT; i++)
        for (int j = 0; j < PRESET_COUNT; j++) {
            if (i == j)
                continue;
            int ok = 1;
            for (int r = 0; r < POOL_COUNT; r++)
                ok &= use[i][r] + use[j][r] + 64 <= pools[r].size;     /* alignment of both sides */
            pairs++;
            fit += ok;
        }
    CHECK(fit == pairs, "every pair of factory presets fits side by side: %d / %d", fit, pairs);
    printf("spill: the largest factory preset uses %u / %u / %u / %u KB of the pools (%u / %u / %u / %u)\n",
           (unsigned)(most[0] + 1023) / 1024, (unsigned)(most[1] + 1023) / 1024, (unsigned)(most[2] + 1023) / 1024,
           (unsigned)(most[3] + 1023) / 1024, (unsigned)pools[0].size / 1024, (unsigned)pools[1].size / 1024,
           (unsigned)pools[2].size / 1024, (unsigned)pools[3].size / 1024);

    /* loaded for real: the first preset on side 0, the next on side 1, both whole and apart */
    static struct preset q;
    presets_load(&src, 0, &p);
    presets_load(&src, 1, &q);
    pools_reset();
    use_of(&cat, &p, use[0]);                       /* side 0 */
    uint32_t low[POOL_COUNT];
    for (int r = 0; r < POOL_COUNT; r++)
        low[r] = pools[r].used;
    pools_use(1);
    int failed = 0;
    for (int i = 0; i < PRESET_SLOTS; i++) {
        const struct preset_slot *s = &q.slot[i];
        int menu = s->menu, index = 0;
        struct catalog_item item;
        struct module m;
        if (menu < 0 || menu == MENU_CAB || menu == MENU_IR ||
            catalog_resolve(&cat, &menu, s->id, &index) == RESOLVE_INVALID || catalog_item(&cat, menu, index, &item))
            continue;
        int err = module_load(item.engine.elf, item.engine.elf_size, (uint16_t)index, &m);
        failed += err != MODULE_OK;
        for (int r = 0; r < POOL_COUNT; r++)
            if (m.region[r])
                failed += m.region[r] < pools[r].addr + low[r];
    }
    CHECK(failed == 0, "preset 1B loads on side 1 above preset 1A: %d failures", failed);
    pools_reset();
}

void test_spill(void)
{
    test_sides();
    test_mask();
    test_factory_pairs();
}
