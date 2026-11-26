/*
 * Module loader: every module of the library must load into empty pools with a valid header, and the
 * loaded bytes must match an independent implementation (tools/check_loader.py reads the --dump file).
 */
#include "engine/module_loader.h"
#include "engine/pools.h"
#include "fw/catalog.h"
#include "test.h"
#include <stdio.h>

static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

void test_loader(const char *dump_path)
{
    struct catalog cat;
    if (catalog_open(&cat) != 0)
        return;
    pools_init();
    FILE *dump = dump_path ? fopen(dump_path, "w") : 0;
    int loaded = 0;
    for (int m = 0; m < cat.lib.menus; m++) {
        int n = lib_count(&cat.lib, m);
        for (int i = 0; i < n; i++) {
            struct lib_entry e;
            struct module mod;
            lib_entry(&cat.lib, m, i, &e);
            pools_reset();
            int err = module_load(e.elf, e.elf_size, (uint16_t)i, &mod);
            CHECK(err == MODULE_OK, "%d.%d %s: error %d", m, i, e.name, err);
            if (err != MODULE_OK)
                continue;
            loaded++;
            /* the header id is not checked: several factory modules are copies that kept another id */
            uint32_t code = pools[POOL_CODE].addr, end = code + mod.region_size[POOL_CODE];
            CHECK((mod.init & 1) && mod.init > code && mod.init < end, "%d.%d Init %#x", m, i, mod.init);
            CHECK((mod.process & 1) && mod.process > code && mod.process < end, "%d.%d Process %#x", m, i,
                  mod.process);
            if (dump) {
                fprintf(dump, "%d.%d %08x", m, i, e.id);
                for (int r = 0; r < POOL_COUNT; r++) {
                    uint32_t size = mod.region_size[r];
                    const uint8_t *p = size ? pool_ptr(mod.region[r], size) : 0;
                    fprintf(dump, " %x:%08x", size, p ? fnv1a(p, size) : 0);
                }
                fprintf(dump, "\n");
            }
        }
    }
    if (dump)
        fclose(dump);

    /* the pools fill up like the original's: a chain of big modules eventually reports MODULE_NO_MEMORY */
    pools_reset();
    struct lib_entry cab;
    struct module mod;
    int fitted = 0, err = MODULE_OK;
    lib_entry(&cat.lib, MENU_CAB, 0, &cab);
    while (fitted < 12 && (err = module_load(cab.elf, cab.elf_size, 0, &mod)) == MODULE_OK)
        fitted++;
    CHECK(err == MODULE_NO_MEMORY && fitted == 2, "cab engines that fit the state pool: %d (error %d)", fitted, err);
    printf("loader: %d modules loaded and relocated\n", loaded);
}
