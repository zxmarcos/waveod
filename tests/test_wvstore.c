/*
 * waveod's own preset store (fw/wv_store.c) and format (fw/wv_preset.c), and fw/presets.c on top of them:
 * appending, reopening, clean-up under heavy rewriting, power cuts, leftovers of earlier firmware in the area,
 * a full store; every factory preset through the format; saving, moving and copying presets without touching
 * the original's record store.
 */
#include "fw/flash_map.h"
#include "fw/preset_format.h"
#include "fw/presets.h"
#include "fw/wv_preset.h"
#include "fw/wv_store.h"
#include "host/host.h"
#include "platform/flash.h"
#include "test.h"
#include <string.h>

static uint8_t *area(void)
{
    return host_flash_image() + FLASH_WV_STORE;
}

static void fill(uint8_t *p, unsigned n, unsigned seed)
{
    for (unsigned i = 0; i < n; i++)
        p[i] = (uint8_t)(seed * 131 + i * 7 + (i >> 8));
}

static int holds(int key, unsigned n, unsigned seed)
{
    static uint8_t want[WVS_PAYLOAD_MAX];
    unsigned len;
    const uint8_t *d = wvs_get(WVS_PRESET, key, &len);
    fill(want, n, seed);
    return d && len == n && memcmp(d, want, n) == 0;
}

static void test_mechanics(void)
{
    static uint8_t buf[WVS_PAYLOAD_MAX];
    memset(area(), 0xff, FLASH_WV_STORE_SIZE);
    fill(area() + 0x2000, 0x1800, 99);              /* leftovers of an earlier firmware in sectors 2 and 3 */
    wvs_open();
    unsigned len;
    CHECK(wvs_get(WVS_PRESET, 3, &len) == 0, "empty store");
    fill(buf, 700, 1);
    CHECK(wvs_put(WVS_PRESET, 3, buf, 700) == 0, "first record");
    CHECK(holds(3, 700, 1), "read back");
    wvs_open();
    CHECK(holds(3, 700, 1), "read back after a restart");
    CHECK(wvs_put(WVS_PRESET, WVS_KEYS, buf, 10) == -1 && wvs_put(WVS_PRESET, 1, buf, WVS_PAYLOAD_MAX + 1) == -1,
          "bad key / length refused");

    /* 3000 rewrites of 60 keys (about 2.3 MB through a 412 KB area): the clean-up keeps the newest of each */
    unsigned last[60] = { 0 };
    int ok = 1;
    for (unsigned i = 0; i < 3000 && ok; i++) {
        int key = (int)(i * 7 % 60);
        unsigned n = 400 + i % 900;
        fill(buf, n, i + 10);
        ok = wvs_put(WVS_PRESET, key, buf, n) == 0;
        last[key] = i + 1;
    }
    CHECK(ok, "rewrites");
    struct wvs_stats s;
    wvs_stats(&s);
    CHECK(s.free >= 1, "a free sector stays for the clean-up (%u)", s.free);
    int good = 0;
    wvs_open();
    for (int key = 0; key < 60; key++) {
        unsigned i = last[key] - 1;
        good += last[key] && holds(key, 400 + i % 900, i + 10);
    }
    CHECK(good == 60, "%d of 60 keys hold their newest data after a restart", good);
    CHECK(holds(3, 700, 1) || last[3], "key 3 kept or rewritten");
    CHECK(area()[0x2000] == 0xff || (area()[0x2000] == 'W' && area()[0x2001] == 'V'), "leftovers erased before use");

    /* a power cut while a record was programmed: the half-written one is ignored, the one before it stays */
    fill(buf, 500, 7777);
    CHECK(wvs_put(WVS_PRESET, 5, buf, 500) == 0, "record before the cut");
    fill(buf, 500, 8888);
    CHECK(wvs_put(WVS_PRESET, 5, buf, 500) == 0, "record cut short");
    const uint8_t *d = wvs_get(WVS_PRESET, 5, &len);
    uint8_t *cut = (uint8_t *)d + 300;
    memset(cut, 0xff, 200);                         /* the last 200 bytes never reached the flash */
    wvs_open();
    CHECK(holds(5, 500, 7777), "the complete record before it wins");
    fill(buf, 300, 4242);
    CHECK(wvs_put(WVS_PRESET, 6, buf, 300) == 0 && holds(6, 300, 4242), "writing goes on after the damage");
    wvs_open();
    CHECK(holds(6, 300, 4242) && holds(5, 500, 7777), "and after a restart");

    /* forgotten records go at the clean-up; a full store refuses instead of losing data */
    for (int key = 0; key < 60; key++)
        wvs_forget(WVS_PRESET, key);
    int full = 0, written = 0;
    for (int key = 100; key < WVS_KEYS && !full; key++) {
        fill(buf, 4000, (unsigned)key);
        int err = wvs_put(WVS_PRESET, key, buf, 4000);
        if (err == -2)
            full = 1;
        else
            written += err == 0;
    }
    CHECK(full, "a full store says so");
    good = 0;
    for (int key = 100; key < 100 + written; key++)
        good += holds(key, 4000, (unsigned)key);
    CHECK(written > 90 && good == written, "%d of %d records intact when full", good, written);
    memset(area(), 0xff, FLASH_WV_STORE_SIZE);
}

/* ---------------------------------------------------------------- with a firmware: format and presets */

static int same_preset(const struct preset *a, const struct preset *b, char *why)
{
#define DIFF(cond, what)                                                                                    \
    if (cond) {                                                                                             \
        strcpy(why, what);                                                                                  \
        return 0;                                                                                           \
    }
    DIFF(strcmp(a->name, b->name) || strcmp(a->style, b->style), "name / style")
    DIFF(a->volume != b->volume || a->pan != b->pan || a->bpm != b->bpm, "volume / pan / BPM")
    DIFF(a->count != b->count || memcmp(a->order, b->order, sizeof a->order), "order")
    for (int i = 0; i < PRESET_SLOTS; i++) {
        const struct preset_slot *x = &a->slot[i], *y = &b->slot[i];
        DIFF((x->menu < 0) != (y->menu < 0), "slot used")
        if (x->menu < 0)
            continue;
        DIFF(x->on != y->on || x->menu != y->menu || x->index != y->index || x->id != y->id ||
             memcmp(x->knobs, y->knobs, sizeof x->knobs), "slot")
    }
    DIFF(memcmp(a->qk_target, b->qk_target, 3) || memcmp(a->qk_param, b->qk_param, 3), "quick knobs")
    DIFF(a->fx_loop != b->fx_loop || a->exp_group != b->exp_group, "FX loop / EXP group")
    DIFF(memcmp(a->exp, b->exp, sizeof a->exp), "EXP")
    DIFF(memcmp(a->ctrl, b->ctrl, sizeof a->ctrl) || memcmp(a->fs, b->fs, sizeof a->fs), "CTRL / footswitches")
    DIFF(memcmp(&a->route, &b->route, sizeof a->route), "routing")
#undef DIFF
    return 1;
}

static void test_format(void)
{
    static struct preset p, q;
    static uint8_t buf[WVP_MAX];
    int same = 0, largest = 0;
    char why[32] = "";
    for (int r = 0; r < FLASH_FACTORY_COUNT; r++) {
        const uint8_t *rec = flash_ptr(FLASH_FACTORY_BANK + r * 0x500u + r / 3 * 0x100u, PRESET_RECORD_SIZE);
        if (preset_decode(rec, &p) != 0)
            continue;
        int n = wvp_encode(&p, buf, sizeof buf);
        largest = n > largest ? n : largest;
        if (n > 0 && wvp_decode(buf, (unsigned)n, &q) == 0 && same_preset(&p, &q, why))
            same++;
        else
            printf("  factory preset %d: %s\n", r, n < 0 ? "does not encode" : why);
    }
    CHECK(same == FLASH_FACTORY_COUNT, "%d of %d factory presets through waveod's format", same, FLASH_FACTORY_COUNT);
    printf("  waveod preset format: largest factory preset %d bytes\n", largest);

    /* a later version's chunk is skipped; damaged data is refused */
    int n = wvp_encode(&p, buf, sizeof buf - 8);
    buf[n] = 0x77;
    buf[n + 1] = 3;
    buf[n + 2] = 0;
    buf[n + 3] = buf[n + 4] = buf[n + 5] = 1;
    CHECK(wvp_decode(buf, (unsigned)n + 6, &q) == 0 && same_preset(&p, &q, why), "unknown chunk skipped");
    char name[PRESET_NAME_LEN + 1];
    CHECK(wvp_name(buf, (unsigned)n, name) == 0 && strcmp(name, p.name) == 0, "name only");
    buf[0] = 'X';
    CHECK(wvp_decode(buf, (unsigned)n, &q) != 0 && q.count == 0, "not this format");
    CHECK(wvp_encode(&p, buf, 40) < 0, "no room");
}

static void test_presets(void)
{
    static struct preset p, q;
    static uint8_t rec[PRESET_RECORD_SIZE];
    uint8_t *stock = host_flash_image() + FLASH_RECORD_STORE;
    memset(area(), 0xff, FLASH_WV_STORE_SIZE);
    memset(stock, 0xff, FLASH_RECORD_STORE_SIZE);
    static struct preset_source src;
    presets_open(&src);
    CHECK(presets_load(&src, 0, &p) == PRESET_FACTORY && presets_origin(&src, 7) == PRESET_FACTORY,
          "the original's presets until waveod saves");

    /* save: waveod's store only */
    strcpy(p.name, "WAVEOD 5");
    p.volume = 77;
    p.route.mode = ROUTE_PARALLEL;
    CHECK(presets_store(&src, 5, &p) == 0, "store preset 5");
    CHECK(stock[0] == 0xff && stock[1] == 0xff, "record store untouched");
    presets_open(&src);
    CHECK(presets_load(&src, 5, &q) == PRESET_USER && strcmp(q.name, "WAVEOD 5") == 0 && q.volume == 77 &&
          q.route.mode == ROUTE_PARALLEL, "preset 5 after a restart: \"%s\"", q.name);
    char name[PRESET_NAME_LEN + 1];
    presets_name(&src, 5, name);
    CHECK(strcmp(name, "WAVEOD 5") == 0, "name \"%s\"", name);
    presets_read(&src, 5, rec);
    CHECK(rec[8] == 5 && memcmp(rec + 0x0a, "WAVEOD 5", 8) == 0, "as an original record for the editor");
    presets_load(&src, 6, &p);
    char six[PRESET_NAME_LEN + 1];
    strcpy(six, p.name);

    /* move 5 to 9: 6..9 shift down; copy 9 over 20; an editor import over 3 */
    CHECK(presets_move(&src, 5, 9) == 0, "move");
    presets_open(&src);
    presets_load(&src, 9, &q);
    presets_load(&src, 5, &p);
    CHECK(strcmp(q.name, "WAVEOD 5") == 0 && strcmp(p.name, six) == 0, "moved: 9 \"%s\", 5 \"%s\"", q.name, p.name);
    CHECK(presets_copy(&src, 9, 20) == 0, "copy");
    CHECK(presets_load(&src, 20, &q) == PRESET_USER && q.route.mode == ROUTE_PARALLEL, "copy keeps the routing");
    preset_encode(&q, 3, rec);
    memcpy(rec + 0x0a, "IMPORT\0", 7);
    CHECK(presets_save(&src, 3, rec) == 0, "editor import");
    presets_open(&src);
    presets_name(&src, 3, name);
    CHECK(strcmp(name, "IMPORT") == 0, "imported \"%s\"", name);
    CHECK(presets_map_value(&src, 4) == 4 + 2, "the original's record number for the editor's list");

    /* all 240 places saved and rewritten: still fits, still reads back */
    int err = 0;
    for (int round = 0; round < 3 && !err; round++)
        for (int i = 0; i < PRESET_COUNT && !err; i++) {
            presets_load(&src, i % FLASH_FACTORY_COUNT, &p);
            p.volume = (uint8_t)(round * 10 + i % 7);
            err = presets_store(&src, i, &p);
        }
    CHECK(!err, "240 presets, three times (error %d)", err);
    presets_open(&src);
    int good = 0;
    for (int i = 0; i < PRESET_COUNT; i++)
        good += presets_load(&src, i, &q) == PRESET_USER && q.volume == 20 + i % 7;
    CHECK(good == PRESET_COUNT, "%d of %d presets read back", good, PRESET_COUNT);
    struct wvs_stats s;
    wvs_stats(&s);
    printf("  waveod store with 240 presets: %u KB live, %u of %u sectors free\n", s.live_bytes / 1024, s.free,
           s.sectors);
    CHECK(stock[0] == 0xff, "record store still untouched");
    memset(area(), 0xff, FLASH_WV_STORE_SIZE);
}

void test_wvstore(void)
{
    test_mechanics();
}

void test_wvpresets(void)
{
    test_format();
    test_presets();
}
