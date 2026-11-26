#include "fw/presets.h"
#include "fw/bytes.h"
#include "fw/flash_map.h"
#include "fw/globals_format.h"
#include "fw/wv_preset.h"
#include "fw/wv_store.h"
#include "platform/flash.h"
#include "platform/sdram.h"
#include <string.h>

#define RECORD_GLOBALS    0
#define RECORD_MAP        1
#define GLOBALS_PRESET    0x0C          /* u16: active preset */

static const uint8_t *factory_record(const struct preset_source *src, int r)
{
    if (!src->factory || r < 0 || r >= FLASH_FACTORY_COUNT)
        return 0;
    return src->factory + (uint32_t)r * 0x500u + (uint32_t)(r / 3) * 0x100u;
}

/* ---------------------------------------------------------------- the original's presets (read only) */

static const uint8_t *stock_locate(const struct preset_source *src, int s, enum preset_origin *origin)
{
    if (s < 0 || s >= PRESET_COUNT) {
        *origin = PRESET_EMPTY;
        return 0;
    }
    int r = src->map[s];
    if (src->has_store && r >= 0) {
        const uint8_t *rec = store_record(&src->store, (unsigned)r);
        if (rec && rd16(rec) == 0x5AA5u) {
            *origin = PRESET_USER;
            return rec;
        }
    }
    const uint8_t *rec = factory_record(src, r - 2);
    if (rec && rd16(rec) == 0x5AA5u) {
        *origin = PRESET_FACTORY;
        return rec;
    }
    *origin = PRESET_EMPTY;
    return 0;
}

/* ---------------------------------------------------------------- waveod's map */

#define MAP_LEN (4 + 4 * PRESET_COUNT)      /* "WM" u8 version u8 0, the entries, the record numbers */

static struct preset conv SDRAM_BSS;        /* decoding / encoding on the way between the formats */
static uint8_t wbuf[WVP_MAX] SDRAM_BSS;

static int own(uint16_t v)
{
    return v < WVS_KEYS;
}

static int is_stock(uint16_t v)
{
    return v != WMAP_EMPTY && (v & WMAP_STOCK) && (v & ~WMAP_STOCK) < PRESET_COUNT;
}

static void load_map(struct preset_source *src)
{
    for (int i = 0; i < PRESET_COUNT; i++) {
        src->wmap[i] = (uint16_t)(WMAP_STOCK | i);
        src->wrec[i] = (int16_t)(i + 2);
    }
    unsigned len;
    const uint8_t *m = wvs_get(WVS_MAP, 0, &len);
    if (m && len >= MAP_LEN && m[0] == 'W' && m[1] == 'M')
        for (int i = 0; i < PRESET_COUNT; i++) {
            uint16_t v = rd16(m + 4 + 2 * i);
            if (own(v) || is_stock(v) || v == WMAP_EMPTY)
                src->wmap[i] = v;
            src->wrec[i] = rds16(m + 4 + 2 * PRESET_COUNT + 2 * i);
        }
    /* records no place points at (left by a move, a copy over them or a power failure) go at the next clean-up */
    static uint8_t used[WVS_KEYS] SDRAM_BSS;
    memset(used, 0, sizeof used);
    for (int i = 0; i < PRESET_COUNT; i++)
        if (own(src->wmap[i]))
            used[src->wmap[i]] = 1;
    for (int k = 0; k < WVS_KEYS; k++)
        if (!used[k])
            wvs_forget(WVS_PRESET, k);
}

static int store_err(int err)
{
    return err ? -20 + err : 0;             /* -21 arguments, -22 full, -23 flash */
}

static int save_map(const struct preset_source *src)
{
    static uint8_t m[MAP_LEN] SDRAM_BSS;
    m[0] = 'W';
    m[1] = 'M';
    m[2] = 1;
    m[3] = 0;
    for (int i = 0; i < PRESET_COUNT; i++)
        put16le(m + 4 + 2 * i, src->wmap[i]);
    for (int i = 0; i < PRESET_COUNT; i++)
        put16le(m + 4 + 2 * PRESET_COUNT + 2 * i, (uint16_t)src->wrec[i]);
    return store_err(wvs_put(WVS_MAP, 0, m, sizeof m));
}

void presets_global_controls(struct preset_controls *out)
{
    unsigned len;
    const uint8_t *d = wvs_get(WVS_MAP, WVS_GLOBAL_CONTROLS, &len);
    if (!d || wvp_decode_controls(d, len, out) != 0) {
        preset_controls_factory(out);
        return;
    }
    for (int i = out->count - 1; i >= 0; i--)
        if (!preset_control_global_ok(&out->c[i]))
            preset_control_remove(out, i);
}

int presets_store_global_controls(const struct preset_controls *c)
{
    static struct preset_controls g SDRAM_BSS;
    static uint8_t buf[3 + CONTROL_MAX * 16 + CK_KEYS * (2 + CONTROL_LABEL) + 1] SDRAM_BSS;
    g = *c;
    for (int i = g.count - 1; i >= 0; i--)
        if (!preset_control_global_ok(&g.c[i]))
            preset_control_remove(&g, i);
    int n = wvp_encode_controls(&g, buf, sizeof buf);
    return n < 0 ? -21 : store_err(wvs_put(WVS_MAP, WVS_GLOBAL_CONTROLS, buf, (unsigned)n));
}

static int free_key(const struct preset_source *src)
{
    static uint8_t used[WVS_KEYS] SDRAM_BSS;
    memset(used, 0, sizeof used);
    for (int i = 0; i < PRESET_COUNT; i++)
        if (own(src->wmap[i]))
            used[src->wmap[i]] = 1;
    for (int k = 0; k < WVS_KEYS; k++)
        if (!used[k])
            return k;
    return -1;
}

void presets_open(struct preset_source *src)
{
    memset(src, 0, sizeof *src);
    for (int i = 0; i < PRESET_COUNT; i++)
        src->map[i] = (int16_t)(i + 2);
    src->factory = flash_ptr(FLASH_FACTORY_BANK, 0x28000);
    wvs_open();
    load_map(src);
    if (store_open(&src->store, FLASH_RECORD_STORE, FLASH_RECORD_STORE_SIZE) != 0)
        return;
    src->has_store = 1;
    const uint8_t *map = store_record(&src->store, RECORD_MAP);
    if (map && src->store.record_size >= 2 * PRESET_COUNT)
        for (int i = 0; i < PRESET_COUNT; i++)
            src->map[i] = rds16(map + 2 * i);
    const uint8_t *g = store_record(&src->store, RECORD_GLOBALS);
    if (g && rd16(g) == GLOBALS_MAGIC && rd16(g + GLOBALS_PRESET) < PRESET_COUNT)
        src->last_index = rd16(g + GLOBALS_PRESET);
}

static uint16_t entry(const struct preset_source *src, int index)
{
    return index >= 0 && index < PRESET_COUNT ? src->wmap[index] : WMAP_EMPTY;
}

/* waveod's record of a place, decoded into out; 0 = there is one */
static int load_own(uint16_t v, struct preset *out)
{
    unsigned len;
    const uint8_t *d = own(v) ? wvs_get(WVS_PRESET, v, &len) : 0;
    return d && wvp_decode(d, len, out) == 0 ? 0 : -1;
}

enum preset_origin presets_load(const struct preset_source *src, int index, struct preset *out)
{
    uint16_t v = entry(src, index);
    if (own(v)) {
        if (load_own(v, out) == 0)
            return PRESET_USER;
    } else if (is_stock(v)) {
        enum preset_origin origin;
        const uint8_t *rec = stock_locate(src, v & ~WMAP_STOCK, &origin);
        if (rec && preset_decode(rec, out) == 0)
            return origin;
    }
    preset_clear(out, "");
    return PRESET_EMPTY;
}

void presets_read(const struct preset_source *src, int index, uint8_t out[PRESET_RECORD_SIZE])
{
    uint16_t v = entry(src, index);
    enum preset_origin origin = PRESET_EMPTY;
    const uint8_t *rec = 0;
    if (own(v) && load_own(v, &conv) == 0) {
        preset_encode(&conv, index, out);
        return;
    }
    if (is_stock(v))
        rec = stock_locate(src, v & ~WMAP_STOCK, &origin);
    memcpy(out, rec ? rec : preset_template, PRESET_RECORD_SIZE);
    if (origin != PRESET_USER) {
        out[8] = (uint8_t)index;
        out[9] = (uint8_t)(index >> 8);
    }
}

enum preset_origin presets_origin(const struct preset_source *src, int index)
{
    uint16_t v = entry(src, index);
    enum preset_origin origin = PRESET_EMPTY;
    unsigned len;
    if (own(v))
        return wvs_get(WVS_PRESET, v, &len) ? PRESET_USER : PRESET_EMPTY;
    if (is_stock(v))
        stock_locate(src, v & ~WMAP_STOCK, &origin);
    return origin;
}

void presets_name(const struct preset_source *src, int index, char out[PRESET_NAME_LEN + 1])
{
    uint16_t v = entry(src, index);
    out[0] = 0;
    if (own(v)) {
        unsigned len;
        const uint8_t *d = wvs_get(WVS_PRESET, v, &len);
        if (d)
            wvp_name(d, len, out);
        return;
    }
    enum preset_origin origin;
    const uint8_t *rec = is_stock(v) ? stock_locate(src, v & ~WMAP_STOCK, &origin) : 0;
    int n = 0;
    if (rec)
        for (; n < PRESET_NAME_LEN && rec[0x0a + n] && rec[0x0a + n] != 0xff; n++)
            out[n] = (char)rec[0x0a + n];
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

enum preset_place presets_place(const struct preset_source *src, int index)
{
    uint16_t v = entry(src, index);
    return own(v) ? PLACE_WAVEOD : is_stock(v) ? PLACE_ORIGINAL : PLACE_EMPTY;
}

int presets_map_value(const struct preset_source *src, int index)
{
    uint16_t v = entry(src, index);
    if (index < 0 || index >= PRESET_COUNT)
        return -1;
    return is_stock(v) ? src->map[v & ~WMAP_STOCK] : src->wrec[index];
}

/* ---------------------------------------------------------------- writing (waveod's store only) */

int presets_store(struct preset_source *src, int index, const struct preset *p)
{
    if (index < 0 || index >= PRESET_COUNT)
        return -10;
    int n = wvp_encode(p, wbuf, sizeof wbuf);
    if (n < 0)
        return -12;
    uint16_t was = src->wmap[index];
    if (own(was))                           /* rewriting its own record: the map stays */
        return store_err(wvs_put(WVS_PRESET, was, wbuf, (unsigned)n));
    int key = free_key(src);
    if (key < 0)
        return -22;
    int err = store_err(wvs_put(WVS_PRESET, key, wbuf, (unsigned)n));
    if (err)
        return err;
    int16_t rec_was = src->wrec[index];
    if (is_stock(was))                      /* the list keeps showing the record the original would have used */
        src->wrec[index] = src->map[was & ~WMAP_STOCK];
    src->wmap[index] = (uint16_t)key;       /* the record first, then the map: a power cut between leaves the
                                               old map and a record nothing points at */
    err = save_map(src);
    if (err) {
        src->wrec[index] = rec_was;
        src->wmap[index] = was;
        wvs_forget(WVS_PRESET, key);
    }
    return err;
}

int presets_save(struct preset_source *src, int index, const uint8_t rec[PRESET_RECORD_SIZE])
{
    if (index < 0 || index >= PRESET_COUNT)
        return -10;
    if (preset_decode(rec, &conv) != 0)
        preset_clear(&conv, "");
    return presets_store(src, index, &conv);
}

int presets_copy(struct preset_source *src, int from, int to)
{
    if (from < 0 || from >= PRESET_COUNT || to < 0 || to >= PRESET_COUNT)
        return -10;
    if (from == to)
        return 0;
    presets_load(src, from, &conv);
    return presets_store(src, to, &conv);
}

int presets_move(struct preset_source *src, int from, int to)
{
    if (from < 0 || from >= PRESET_COUNT || to < 0 || to >= PRESET_COUNT)
        return -10;
    uint16_t v = src->wmap[from];
    int16_t rec = src->wrec[from];
    if (from < to) {
        memmove(&src->wmap[from], &src->wmap[from + 1], sizeof src->wmap[0] * (size_t)(to - from));
        memmove(&src->wrec[from], &src->wrec[from + 1], sizeof src->wrec[0] * (size_t)(to - from));
    } else if (to < from) {
        memmove(&src->wmap[to + 1], &src->wmap[to], sizeof src->wmap[0] * (size_t)(from - to));
        memmove(&src->wrec[to + 1], &src->wrec[to], sizeof src->wrec[0] * (size_t)(from - to));
    }
    src->wmap[to] = v;
    src->wrec[to] = rec;
    return save_map(src);
}

/* ---------------------------------------------------------------- the original's settings (shared) */

static int ensure_store(struct preset_source *src)
{
    if (src->has_store)
        return 0;
    /* the original formats the store at start-up, so this only happens on a blank flash */
    if (store_format(&src->store, FLASH_RECORD_STORE, FLASH_RECORD_STORE_SIZE))
        return -12;
    src->has_store = 1;
    return 0;
}

const uint8_t *presets_globals(const struct preset_source *src)
{
    const uint8_t *rec = src->has_store ? store_record(&src->store, RECORD_GLOBALS) : 0;
    return rec && rd16(rec) == GLOBALS_MAGIC ? rec : 0;
}

int presets_write_globals(struct preset_source *src, const uint8_t *rec)
{
    int err = ensure_store(src);
    return err ? err : store_write(&src->store, RECORD_GLOBALS, rec, GLOBALS_RECORD_SIZE);
}

int presets_set_startup(struct preset_source *src, int index)
{
    static uint8_t g[GLOBALS_RECORD_SIZE] SDRAM_BSS;
    if (index < 0 || index >= PRESET_COUNT)
        return -1;
    const uint8_t *rec = presets_globals(src);
    if (rec && rd16(rec + GLOBALS_PRESET) == index)
        return 0;
    memcpy(g, rec ? rec : globals_factory, sizeof g);
    g[GLOBALS_PRESET] = (uint8_t)index;
    g[GLOBALS_PRESET + 1] = (uint8_t)(index >> 8);
    if (presets_write_globals(src, g))
        return -1;
    src->last_index = (uint16_t)index;
    return 0;
}