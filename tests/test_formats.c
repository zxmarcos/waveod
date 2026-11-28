/* Flash data formats: library, factory presets, IR directory, value text. */
#include "fw/catalog.h"
#include "fw/flash_map.h"
#include "fw/presets.h"
#include "fw/record_store.h"
#include "model/param_format.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

static void check_format(int f3, float min, float max, float v, const char *want)
{
    struct lib_param p = { "x", min, max, 1, 0, 0, 0xff, 0, (uint8_t)f3 };
    char got[32];
    param_format(&p, v, 0, got, sizeof got);
    CHECK(strcmp(got, want) == 0, "format %#x of %g: got \"%s\", want \"%s\"", f3, v, got, want);
}

void test_formats(void)
{
    struct catalog cat;
    CHECK(catalog_open(&cat) == 0, "effect library not found");
    CHECK(cat.lib.menus == 16, "menus %d", cat.lib.menus);
    int total = 0;
    for (int m = 0; m < cat.lib.menus; m++)
        total += lib_count(&cat.lib, m);
    CHECK(total >= 179, "library entries %d", total);
    CHECK(lib_count(&cat.lib, MENU_CAB) == 1 && lib_count(&cat.lib, MENU_CLONE) == 1, "engine menus");
    struct lib_entry e;
    CHECK(lib_entry(&cat.lib, MENU_DRV, 0, &e) == 0 && e.elf_size > 0 && e.elf[0] == 0x7f, "first drive");
    int menu, index;
    CHECK(lib_locate(&cat.lib, e.id, &menu, &index) == 0 && menu == MENU_DRV && index == 0, "locate by id");

    CHECK(cat.irs.count == 91, "IR directory entries %d", cat.irs.count);
    CHECK(catalog_count(&cat, MENU_CAB) == 61, "factory cabs %d", catalog_count(&cat, MENU_CAB));
    CHECK(catalog_count(&cat, MENU_IR) == 20 && catalog_count(&cat, MENU_CLONE) == 20,
          "IR / Clone slots (the directory's 10 Clone entries, waveod's 20)");
    CHECK(catalog_data(&cat, MENU_CAB, 0) >= FLASH_IR_LIBRARY + 0x2000, "cab 0 data %#x", catalog_data(&cat, MENU_CAB, 0));

    struct preset_source src;
    presets_open(&src);
    CHECK(!src.has_store, "erased store should not be recognised");
    struct preset p;
    CHECK(presets_load(&src, 0, &p) == PRESET_FACTORY, "preset 0 from the factory bank");
    CHECK(strcmp(p.name, "Matribox II PRO") == 0, "preset 0 name \"%s\"", p.name);
    CHECK(p.count == 11, "preset 0 chain length %d", p.count);
    CHECK(presets_load(&src, 119, &p) == PRESET_FACTORY, "preset 119 from the factory bank");
    CHECK(presets_load(&src, 120, &p) == PRESET_EMPTY, "preset 120 has no factory data");
    int resolved = 0, fallback = 0;
    for (int i = 0; i < 120; i++) {
        presets_load(&src, i, &p);
        for (int s = 0; s < PRESET_SLOTS; s++) {
            if (p.slot[s].menu < 0)
                continue;
            int m = p.slot[s].menu, idx;
            enum resolve_result res = catalog_resolve(&cat, &m, p.slot[s].id, &idx);
            resolved += res == RESOLVE_EXACT;
            fallback += res != RESOLVE_EXACT;
        }
    }
    /* factory preset 07B (index 25) has a Cab slot with id 0: the original loads cab 0 with defaults */
    CHECK(fallback == 1, "%d factory slots did not resolve (%d did)", fallback, resolved);

    char label[8];
    preset_label(0, label);
    CHECK(strcmp(label, "01A") == 0, "label %s", label);
    preset_label(239, label);
    CHECK(strcmp(label, "60D") == 0, "label %s", label);

    check_format(0x00, 0, 1, 1, "On");
    check_format(0x15, 0, 100, 37, "37");
    check_format(0x19, 0, 2000, 420, "420ms");
    check_format(0x17, 0, 20000, 0, "Off");
    check_format(0x17, 0, 20000, 120, "120Hz");
    check_format(0x20, 20, 20000, 800, "800Hz");
    check_format(0x20, 20, 20000, 4000, "4kHz");
    check_format(0x1e, 0, 100, 12.5f, "12.5%");
    check_format(0x75, -50, 50, 0, "Center");
    check_format(0x75, -50, 50, 30, "R30");
    check_format(0x75, -50, 50, -30, "L30");
    check_format(0x3b, 1, 20, 20, "Limit");
    check_format(0x3b, 1, 20, 4, "4.0:1");
    check_format(0x5d, -12, 12, -3, "-3");
    check_format(0xbf, 0, 3000, 1500, "1.5s");
    check_format(0xa4, 0, 360, 90, "90\xC2\xB0");
}
