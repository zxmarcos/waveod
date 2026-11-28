/*
 * The editor protocol's pieces that run on the host: LZO1X, the IR / Clone file systems.
 *
 * `--irfs-apply IN OPS OUT` (for tools/check_irfs.py): IN / OUT are 8 MB flash images; OPS holds records
 * {u8 op (0 write, 1 rename, 2 delete Clone), u8 kind (0 user IR, 1 Clone), u8 slot, char name[17], u32 n,
 * n bytes of data}, applied after mounting like the pedal does at start-up.
 */
#include "editor/lzo.h"
#include "fw/catalog.h"
#include "fw/library.h"
#include "fw/flash_map.h"
#include "fw/ir_fs.h"
#include "fw/ir_store.h"
#include "host/host.h"
#include "platform/flash.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

static void test_lzo(void)
{
    static uint8_t src[4000], z[4100], out[4000];
    for (int i = 0; i < (int)sizeof src; i++)
        src[i] = (uint8_t)(i * 7 + (i >> 5));
    static const uint32_t sizes[] = { 0, 1, 3, 4, 17, 238, 239, 255, 272, 273, 274, 528, 529, 1000, 4000 };
    int ok = 1;
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        uint32_t n = lzo1x_store(src, sizes[k], z), got = sizeof out;
        if (lzo1x_decompress(z, n, out, &got) != 0 || got != sizes[k] || memcmp(out, src, got) != 0) {
            ok = 0;
            printf("  lzo: %u bytes do not come back\n", (unsigned)sizes[k]);
        }
    }
    CHECK(ok, "literal streams of every length class decompress");
    /* a real compressed stream: "abcabcabc..." as LZO1X-1 writes it (a literal run, then one long match) */
    static const uint8_t rle[] = { 0x14, 'a', 'b', 'c', 0x3e, 0x08, 0x00, 0x11, 0x00, 0x00 };
    uint32_t got = sizeof out;
    int r = lzo1x_decompress(rle, sizeof rle, out, &got);
    int same = r == 0 && got == 3 + 30 + 2;
    for (uint32_t i = 0; same && i < got; i++)
        same = out[i] == "abc"[i % 3];
    CHECK(same, "a match copies the earlier bytes (%d, %u)", r, (unsigned)got);
    uint32_t tiny = 10;
    CHECK(lzo1x_decompress(z, lzo1x_store(src, 100, z), out, &tiny) != 0, "too small an output is refused");
    CHECK(lzo1x_decompress(z, 5, out, &got) != 0, "a cut stream is refused");
}

static void test_fs(void)
{
    uint8_t *flash = host_flash_image();
    memset(flash + FLASH_USER_IR_FS, 0xff, FLASH_USER_IR_FS_SIZE);
    memset(flash + FLASH_CLONE_FS, 0xff, FLASH_CLONE_FS_SIZE);
    irfs_mount();
    static uint8_t ir[IRFS_USER_DATA], cap[IRFS_CLONE_DATA];
    for (unsigned i = 0; i < sizeof ir; i++)
        ir[i] = (uint8_t)(i * 13);
    for (unsigned i = 0; i < sizeof cap; i++)
        cap[i] = (uint8_t)(i * 5 + 1);
    char name[18];
    CHECK(irfs_write(IRFS_USER, 3, "Mesa 4x12", ir, sizeof ir) == 0, "a user IR is written on a blank area");
    uint32_t at = ir_user_data(3);
    CHECK(at && memcmp(flash + at, ir, sizeof ir) == 0, "its data reads back");
    CHECK(ir_fs_name(0, 3, name) == 0 && strcmp(name, "Mesa 4x12") == 0, "its name reads back (%s)", name);
    CHECK(ir_user_data(4) == 0 && ir_fs_name(0, 4, name) != 0, "the other slots stay empty");
    ir[0] ^= 0xff;
    CHECK(irfs_write(IRFS_USER, 3, "Mesa 2", ir, sizeof ir) == 0 && ir_user_data(3) != at, "rewriting moves it");
    CHECK(memcmp(flash + ir_user_data(3), ir, sizeof ir) == 0 && ir_fs_name(0, 3, name) == 0 &&
          strcmp(name, "Mesa 2") == 0, "the new data and name read back");
    uint32_t moved = ir_user_data(3);
    CHECK(irfs_rename(IRFS_USER, 3, "Renamed") == 0 && ir_fs_name(0, 3, name) == 0 && strcmp(name, "Renamed") == 0
          && ir_user_data(3) == moved, "a rename keeps the data");
    CHECK(irfs_write(IRFS_CLONE, 2, "Tweed", cap, sizeof cap) == 0, "a Clone capture is written");
    struct ir_dir d;
    memset(&d, 0, sizeof d);
    at = ir_clone_data(&d, 2);
    CHECK(at && memcmp(flash + at, cap, sizeof cap) == 0, "its data reads back");
    CHECK(irfs_delete_clone(2) == 0 && ir_fs_name(1, 2, name) != 0, "a deleted capture is gone");
    CHECK(irfs_write(IRFS_USER, 60, "x", ir, 16) != 0, "a slot beyond the directory (50) is refused");

    /* waveod's Clone slots past the original's 10 (fw/ir_store.h IR_CLONE_SLOTS) */
    cap[0] = 0x5a;
    CHECK(irfs_write(IRFS_CLONE, 15, "Plexi 16", cap, sizeof cap) == 0, "Clone slot 16 is written");
    at = ir_clone_data(&d, 15);
    CHECK(at && memcmp(flash + at, cap, sizeof cap) == 0 && ir_fs_name(1, 15, name) == 0 &&
          strcmp(name, "Plexi 16") == 0, "its data and name read back (%s)", name);
    CHECK(ir_clone_data(&d, 16) == 0 && ir_fs_name(1, 16, name) != 0, "slot 17 stays empty");
    CHECK(irfs_write(IRFS_CLONE, 20, "x", cap, 16) != 0 && ir_clone_data(&d, 20) == 0, "slot 21 does not exist");
    CHECK(irfs_rename(IRFS_CLONE, 15, "Plexi B") == 0 && ir_fs_name(1, 15, name) == 0 && strcmp(name, "Plexi B") == 0
          && ir_clone_data(&d, 15) == at, "a rename keeps the data");
    for (int s = 0; s < IR_CLONE_SLOTS; s++) {             /* every slot at once: 20 of the 25 blocks */
        cap[1] = (uint8_t)s;
        irfs_write(IRFS_CLONE, s, "All", cap, sizeof cap);
    }
    for (int round = 0; round < 3; round++)                 /* rewriting them over and over: one spare block is enough */
        for (int s = 0; s < IR_CLONE_SLOTS; s++) {
            cap[1] = (uint8_t)(s + 100 * round);
            CHECK(irfs_write(IRFS_CLONE, s, "Again", cap, sizeof cap) == 0, "rewrite %d of slot %d", round, s + 1);
        }
    int same = 1;
    for (int s = 0; s < IR_CLONE_SLOTS; s++) {
        uint32_t a = ir_clone_data(&d, s);
        cap[1] = (uint8_t)(s + 200);
        same &= a && memcmp(flash + a, cap, sizeof cap) == 0;
    }
    CHECK(same, "all 20 hold their last capture");
    irfs_mount();                                           /* as after a restart */
    CHECK(irfs_write(IRFS_CLONE, 19, "After", cap, sizeof cap) == 0 && ir_fs_name(1, 19, name) == 0 &&
          strcmp(name, "After") == 0, "and after a fresh mount");
}

/* the catalog lists 20 Clone items: the directory's 10, then 10 with the directory's ids continued (with the
   firmware's library and IR directory: after host_flash_load_firmware) */
static void clone_catalog(void)
{
    struct catalog cat;
    if (catalog_open(&cat) != 0 || !cat.irs.base) {
        CHECK(0, "catalog_open with the IR directory");
        return;
    }
    static uint8_t cap[IRFS_CLONE_DATA];
    memset(cap, 0x33, sizeof cap);
    CHECK(irfs_write(IRFS_CLONE, 17, "Slot 18", cap, sizeof cap) == 0 &&
          irfs_write(IRFS_CLONE, 19, "After", cap, sizeof cap) == 0, "slots 18 and 20 written");
    CHECK(catalog_count(&cat, MENU_CLONE) == IR_CLONE_SLOTS, "%d Clone items", catalog_count(&cat, MENU_CLONE));
    struct catalog_item it;
    int ok = 1;
    for (int i = 0; i < IR_CLONE_SLOTS; i++)
        ok &= catalog_item(&cat, MENU_CLONE, i, &it) == 0 && it.id == IR_CLONE_ID(i);
    CHECK(ok, "their ids are 0x0F000000 + slot (the directory's for the first 10)");
    for (int i = IR_CLONE_COUNT; i < IR_CLONE_SLOTS; i++)
        ok &= ir_dir_find(&cat.irs, IR_CLONE_ID(i)) < 0;
    CHECK(ok, "no directory entry has the ids of slots 11..20");
    CHECK(catalog_item(&cat, MENU_CLONE, 12, &it) == 0 && strcmp(it.name, "Clone 13") == 0,
          "an empty slot past 10 is named like the directory's (%s)", it.name);
    CHECK(catalog_item(&cat, MENU_CLONE, 19, &it) == 0 && strcmp(it.name, "After") == 0,
          "a written one by its stored name (%s)", it.name);
    int menu = MENU_CLONE, index = -1;
    CHECK(catalog_resolve(&cat, &menu, IR_CLONE_ID(17), &index) == RESOLVE_EXACT && menu == MENU_CLONE && index == 17,
          "a preset's id 0x0F000011 finds slot 18 (%d)", index);
    CHECK(catalog_data(&cat, MENU_CLONE, 17) == ir_clone_data(&cat.irs, 17) && catalog_data(&cat, MENU_CLONE, 17),
          "and its data");
    CHECK(catalog_item(&cat, MENU_CLONE, IR_CLONE_SLOTS, &it) != 0, "no item 21");
}

/* the two areas as they were around fn, for the tests after it */
static void keeping_areas(void (*fn)(void))
{
    static uint8_t user[FLASH_USER_IR_FS_SIZE], clone[FLASH_CLONE_FS_SIZE];
    uint8_t *flash = host_flash_image();
    memcpy(user, flash + FLASH_USER_IR_FS, sizeof user);
    memcpy(clone, flash + FLASH_CLONE_FS, sizeof clone);
    irfs_mount();
    fn();
    memcpy(flash + FLASH_USER_IR_FS, user, sizeof user);
    memcpy(flash + FLASH_CLONE_FS, clone, sizeof clone);
    irfs_mount();
}

void test_clone_catalog(void)
{
    keeping_areas(clone_catalog);
}

void test_editor(void)
{
    test_lzo();
    keeping_areas(test_fs);
}

int irfs_apply(const char *in_path, const char *ops_path, const char *out_path)
{
    uint8_t *flash = host_flash_image();
    FILE *in = fopen(in_path, "rb"), *ops = fopen(ops_path, "rb");
    if (!in || !ops || fread(flash, 1, FLASH_SIZE, in) != FLASH_SIZE)
        return 1;
    fclose(in);
    irfs_mount();
    static uint8_t data[IRFS_CLONE_DATA];
    uint8_t h[20];
    while (fread(h, 1, sizeof h, ops) == sizeof h) {
        uint32_t n;
        char name[18];
        if (fread(&n, 4, 1, ops) != 1 || n > sizeof data || fread(data, 1, n, ops) != n)
            return 1;
        memcpy(name, h + 3, 17);
        name[17] = 0;
        enum irfs_kind kind = h[1] ? IRFS_CLONE : IRFS_USER;
        if (h[0] == 0)
            irfs_write(kind, h[2], name, data, n);
        else if (h[0] == 1)
            irfs_rename(kind, h[2], name);
        else
            irfs_delete_clone(h[2]);
    }
    fclose(ops);
    FILE *out = fopen(out_path, "wb");
    if (!out || fwrite(flash, 1, FLASH_SIZE, out) != FLASH_SIZE)
        return 1;
    fclose(out);
    return 0;
}
