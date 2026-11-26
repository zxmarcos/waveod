/*
 * Record store: reading (synthetic sectors), writing (the original's algorithm, including power cuts), the
 * write guard, and saving presets. Also --store-apply, used by tools/check_store.py.
 */
#include "fw/flash_map.h"
#include "fw/globals_format.h"
#include "fw/presets.h"
#include "fw/record_store.h"
#include "host/host.h"
#include "platform/flash.h"
#include "test.h"
#include <stdlib.h>
#include <string.h>

#define BASE FLASH_RECORD_STORE
#define SIZE FLASH_RECORD_STORE_SIZE

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static uint8_t *area(void)
{
    return host_flash_image() + BASE;
}

/* a store with the original's geometry: 0x1000 sectors, 3 records of 0x54C per sector, header 0x1C */
static uint8_t *make_store(void)
{
    uint8_t *s = area();
    memset(s, 0xff, SIZE);
    put16(s, 0x8F9A);
    put32(s + 8, SIZE);
    put16(s + 0x0c, 0x1000);
    put16(s + 0x10, 0x80);
    put16(s + 0x12, 3);
    put16(s + 0x14, 0x1C);
    put16(s + 0x16, 0x54C);
    return s;
}

static void make_sector(uint8_t *store, int sector, int group, int seq, uint8_t written, uint8_t freed)
{
    uint8_t *h = store + sector * 0x1000;
    put16(h, 0x8E8A);
    h[4] = (uint8_t)group;
    h[5] = (uint8_t)seq;
    h[6] = freed;
    h[7] = written;
    for (int k = 0; k < 3; k++)
        memset(h + 0x1C + k * 0x54C, sector * 16 + k, 0x54C);
}

static void test_reading(void)
{
    uint8_t *s = make_store();
    make_sector(s, 5, 0, 10, 0x03, 0x00);       /* records 0, 1 */
    make_sector(s, 9, 0, 11, 0x07, 0x00);       /* newer copy of group 0 */
    make_sector(s, 7, 1, 250, 0x01, 0x00);      /* group 1 (records 3..5), sequence about to wrap */
    make_sector(s, 3, 1, 2, 0x01, 0x00);        /* wrapped: newer than 250 */
    make_sector(s, 12, 2, 1, 0x02, 0x02);       /* record 7 written then freed */
    struct record_store st;
    CHECK(store_open(&st, BASE, SIZE) == 0, "store not recognised");
    const uint8_t *r = store_record(&st, 2);
    CHECK(r && r[0] == 9 * 16 + 2, "record 2 should come from the newest sector 9");
    r = store_record(&st, 3);
    CHECK(r && r[0] == 3 * 16, "record 3 should come from sector 3 (sequence wrapped)");
    CHECK(store_record(&st, 4) == 0, "record 4 was never written");
    CHECK(store_record(&st, 7) == 0, "record 7 was freed");
    CHECK(store_record(&st, 100) == 0, "group 33 does not exist");

    /* mounting for writes erases what the original erases at start-up */
    CHECK(store_mount(&st) == 0, "mount");
    CHECK(get16(s + 5 * 0x1000) == 0xffff && get16(s + 7 * 0x1000) == 0xffff, "superseded copies erased");
    CHECK(get16(s + 12 * 0x1000) == 0xffff, "sector without a valid record erased");
    CHECK(get16(s + 9 * 0x1000) == 0x8E8A && get16(s + 3 * 0x1000) == 0x8E8A, "current copies kept");

    s[0] = 0;
    CHECK(store_open(&st, BASE, SIZE) != 0, "unformatted store accepted");
}

static void fill(uint8_t *buf, unsigned len, int seed)
{
    for (unsigned i = 0; i < len; i++)
        buf[i] = (uint8_t)(seed * 31 + i * 7);
}

/* the sector holding record n's group */
static int sector_of(const struct record_store *st, unsigned n)
{
    return st->group_sector[n / st->per_sector];
}

static void test_writing(void)
{
    uint8_t *s = area();
    memset(s, 0xff, SIZE);
    struct record_store st;
    CHECK(store_format(&st, BASE, SIZE) == 0, "format");
    CHECK(get16(s) == 0x8F9A && get16(s + 0x16) == 0x54C && get16(s + 0x10) == 0x80, "store header");
    CHECK(get16(s + 0x1000) == 0x8E80 && get16(s + 0x7f000) == 0x8E80, "data sectors marked free");

    static uint8_t a[0x54C], b[0x54C], g[0x238];
    fill(a, sizeof a, 1);
    fill(b, sizeof b, 2);
    fill(g, sizeof g, 3);
    CHECK(store_write(&st, 2, a, 0x48C) == 0, "write record 2");
    int first = sector_of(&st, 2);
    const uint8_t *r = store_record(&st, 2);
    CHECK(r && memcmp(r, a, 0x48C) == 0 && r[0x48C] == 0xff, "record 2 as written, the rest erased");
    const uint8_t *h = s + first * 0x1000;
    CHECK(get16(h) == 0x8E8A && h[4] == 0 && h[5] == 1 && h[6] == 0xfb && h[7] == 0xff, "first header %02x %02x %02x",
          h[5], h[6], h[7]);

    CHECK(store_write(&st, 0, g, sizeof g) == 0, "write record 0");
    int second = sector_of(&st, 0);
    CHECK(second != first && get16(s + first * 0x1000) == 0x8E80, "group moved, old copy superseded");
    CHECK((r = store_record(&st, 2)) && memcmp(r, a, 0x48C) == 0, "record 2 copied along");
    CHECK((r = store_record(&st, 0)) && memcmp(r, g, sizeof g) == 0, "record 0");
    CHECK(store_record(&st, 1) == 0, "record 1 still absent");
    CHECK(s[second * 0x1000 + 5] == 2, "sequence 2");

    CHECK(store_write(&st, 2, b, 0x48C) == 0, "rewrite record 2");
    CHECK((r = store_record(&st, 2)) && memcmp(r, b, 0x48C) == 0, "record 2 replaced");
    CHECK((r = store_record(&st, 0)) && memcmp(r, g, sizeof g) == 0, "record 0 kept");

    /* the same view after a restart, through the read-only scan */
    struct record_store again;
    CHECK(store_open(&again, BASE, SIZE) == 0 && (r = store_record(&again, 2)) && memcmp(r, b, 0x48C) == 0,
          "reopened");

    /* wear: rewriting one record walks through the whole store */
    uint8_t used[256] = { 0 };
    int distinct = 0;
    for (int i = 0; i < 300; i++) {
        fill(a, 0x48C, i);
        if (store_write(&st, 7, a, 0x48C) != 0) {
            CHECK(0, "write %d failed", i);
            break;
        }
        int sec = sector_of(&st, 7);
        distinct += !used[sec];
        used[sec] = 1;
    }
    CHECK(distinct >= 120, "only %d sectors used for one record", distinct);
    CHECK((r = store_record(&st, 7)) && memcmp(r, a, 0x48C) == 0, "last of the 300 writes");
    CHECK((r = store_record(&st, 2)) && memcmp(r, b, 0x48C) == 0, "other groups untouched");

    /* power cut before the old copy was marked superseded: the newer sequence wins */
    int cur = sector_of(&st, 2);
    fill(a, 0x48C, 77);
    CHECK(store_write(&st, 2, a, 0x48C) == 0, "write before the cut");
    put16(s + cur * 0x1000, 0x8E8A);                    /* as if the 0x8E80 mark never happened */
    CHECK(store_open(&again, BASE, SIZE) == 0 && (r = store_record(&again, 2)) && memcmp(r, a, 0x48C) == 0,
          "newer copy wins");
    CHECK(store_mount(&again) == 0 && get16(s + cur * 0x1000) == 0xffff, "older copy erased on mount");

    /* power cut before the commit mark: the old copy stays */
    struct record_store w;
    store_open(&w, BASE, SIZE);
    cur = sector_of(&w, 2);
    fill(b, 0x48C, 78);
    CHECK(store_write(&w, 2, b, 0x48C) == 0, "write to be cut");
    int fresh = sector_of(&w, 2);
    put16(s + cur * 0x1000, 0x8E8A);
    put16(s + fresh * 0x1000, 0xffff);
    CHECK(store_open(&again, BASE, SIZE) == 0 && (r = store_record(&again, 2)) && memcmp(r, a, 0x48C) == 0,
          "uncommitted copy ignored");

    /* refusals */
    CHECK(store_write(&st, 300, a, 4) != 0, "record 300 refused like the original");
    CHECK(store_write(&st, 2, a, 0x54D) != 0, "longer than a record");
    CHECK(flash_erase_sector(0x700000) != 0, "erase outside the record store refused");
    CHECK(flash_program(FLASH_WRITABLE_START - 1, a, 2) != 0, "program across the guard refused");
}

void test_store(void)
{
    test_reading();
    test_writing();
}

/* needs the firmware: factory presets */
void test_preset_save(void)
{
    /* decode + encode gives back every factory record byte for byte */
    int same = 0;
    for (int r = 0; r < FLASH_FACTORY_COUNT; r++) {
        const uint8_t *rec = flash_ptr(FLASH_FACTORY_BANK + r * 0x500u + r / 3 * 0x100u, PRESET_RECORD_SIZE);
        struct preset p;
        static uint8_t out[PRESET_RECORD_SIZE];
        if (preset_decode(rec, &p) != 0)
            continue;
        preset_encode(&p, r, out);
        if (memcmp(out, rec, PRESET_RECORD_SIZE) == 0)
            same++;
        else {
            int at = 0;
            while (out[at] == rec[at])
                at++;
            printf("  factory preset %d differs at +%#x (%02x, stored %02x)\n", r, at, out[at], rec[at]);
        }
    }
    CHECK(same == FLASH_FACTORY_COUNT, "%d of %d factory presets encode back unchanged", same, FLASH_FACTORY_COUNT);

    memset(area(), 0xff, SIZE);                         /* a pedal that never saved */
    memset(host_flash_image() + FLASH_WV_STORE, 0xff, FLASH_WV_STORE_SIZE);
    struct preset_source src;
    presets_open(&src);
    struct preset p;
    CHECK(presets_load(&src, 0, &p) == PRESET_FACTORY, "preset 0");
    strcpy(p.name, "TESTE SALVO");
    p.slot[p.order[0]].knobs[0] = 33.0f;
    p.volume = 61;
    static uint8_t rec[PRESET_RECORD_SIZE];
    preset_encode(&p, 5, rec);
    uint32_t programs = host_flash_programs;
    CHECK(presets_save(&src, 5, rec) == 0, "save as preset 5");
    CHECK(host_flash_programs > programs, "the flash was written");
    CHECK(get16(area()) == 0xffff, "the original's record store is not written (waveod's store is)");

    struct preset_source again;
    presets_open(&again);
    struct preset q;
    CHECK(presets_load(&again, 5, &q) == PRESET_USER, "preset 5 is now a user preset");
    CHECK(strcmp(q.name, "TESTE SALVO") == 0 && q.volume == 61, "name \"%s\", volume %d", q.name, q.volume);
    CHECK(q.slot[q.order[0]].knobs[0] == 33.0f, "knob");
    presets_read(&again, 5, rec);
    CHECK(rec[8] == 5 && rec[9] == 0 && rec[0x1e] == 61, "the editor's record: number, volume");
    CHECK(presets_load(&again, 4, &q) == PRESET_FACTORY, "preset 4 still from the factory bank");
    CHECK(presets_load(&again, 200, &q) == PRESET_EMPTY, "preset 200 still empty");

    /* power-up preset: the settings record is created with the original's factory values when missing, and
       the rest of an existing one is kept */
    CHECK(presets_set_startup(&again, 5) == 0, "settings record created");
    const uint8_t *r0 = store_record(&again.store, 0);
    CHECK(r0 && r0[0x0c] == 5 && memcmp(r0 + 0x0e, globals_factory + 0x0e, 0x238 - 0x0e) == 0, "factory settings");
    static uint8_t g[0x238];
    memset(g, 0x11, sizeof g);
    put16(g, 0x5AA5);
    put16(g + 0x0c, 0);
    CHECK(store_write(&again.store, 0, g, sizeof g) == 0, "settings record");
    CHECK(presets_set_startup(&again, 5) == 0, "power-up preset");
    presets_open(&again);
    const uint8_t *sr = store_record(&again.store, 0);
    CHECK(again.last_index == 5 && sr && sr[0x20] == 0x11, "power-up preset %d", again.last_index);
    CHECK(presets_load(&again, 5, &q) == PRESET_USER, "preset 5 kept when the settings moved");
    memset(area(), 0xff, SIZE);
    memset(host_flash_image() + FLASH_WV_STORE, 0xff, FLASH_WV_STORE_SIZE);
}

/* --store-apply IN OPS OUT: load a store image, apply the writes listed in OPS ({u16 record, u16 length,
   data} each), save the image. tools/check_store.py runs the same writes through the original's code. */
int store_apply(const char *in_path, const char *ops_path, const char *out_path)
{
    FILE *f = fopen(in_path, "rb");
    if (!f || fread(area(), 1, SIZE, f) != SIZE)
        return printf("cannot read %s\n", in_path), 1;
    fclose(f);
    f = fopen(ops_path, "rb");
    if (!f)
        return printf("cannot read %s\n", ops_path), 1;
    struct record_store st;
    if (store_open(&st, BASE, SIZE) != 0)
        return printf("no store in %s\n", in_path), 1;
    static uint8_t buf[0x1000];
    uint8_t hdr[4];
    int n = 0;
    while (fread(hdr, 1, 4, f) == 4) {
        unsigned rec = get16(hdr), len = get16(hdr + 2);
        if (len > sizeof buf || fread(buf, 1, len, f) != len)
            return printf("bad ops file\n"), 1;
        if (store_write(&st, rec, buf, len) != 0)
            printf("write %d (record %u) refused\n", n, rec);
        n++;
    }
    fclose(f);
    f = fopen(out_path, "wb");
    if (!f || fwrite(area(), 1, SIZE, f) != SIZE)
        return printf("cannot write %s\n", out_path), 1;
    fclose(f);
    printf("%d writes applied\n", n);
    return 0;
}
