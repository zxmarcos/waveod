/* waveod's own effect blocks (sdk/wvfx.h, engine/blocks.c, docs/blocks.md): every built-in's descriptor, Stereo
   Util's matrix, where the catalog lists them and their parameters' text (a chain running one: tools/check_blocks.py). */
#include "engine/blocks.h"
#include "engine/cabir.h"
#include "fw/catalog.h"
#include "fw/crc32.h"
#include "fw/wv_library.h"
#include "model/param_format.h"
#include "test.h"
#include <math.h>
#include <string.h>

/* every built-in is a well-formed block */
static void test_descriptors(void)
{
    for (int i = 0; i < blocks_builtin_count(); i++) {
        const struct wvfx_desc *d = blocks_builtin(i);
        int ok = d->magic == WVFX_MAGIC && d->abi == WVFX_ABI && WVFX_IS_ID(d->id) &&
                 ((d->id >> 16) & 0xff) == d->menu && d->menu < LIB_MAX_MENUS && d->name && d->category &&
                 strlen(d->name) < 24 && d->nparams <= WVFX_MAX_PARAMS && d->init && d->process;
        CHECK(ok, "block %d (%s): magic, abi, id 0x%08x of its menu, name, functions", i, d->name ? d->name : "?",
              d->id);
        uint32_t knobs = 0;
        for (int k = 0; k < d->nparams; k++) {
            struct lib_param p;
            int pok = blocks_param(d, k, &p) == 0 && p.name && p.idx < WVFX_KNOBS && !(knobs >> p.idx & 1u) &&
                      p.min <= p.def && p.def <= p.max && p.step > 0.0f;
            knobs |= 1u << p.idx;
            if (pok && p.options)               /* one name per value */
                pok = param_list_size(&p) == (int)(p.max - p.min) + 1 && p.min == 0.0f && p.step == 1.0f;
            CHECK(pok, "block %s parameter %d (%s): own knob, default in range, options per value", d->name, k,
                  p.name ? p.name : "?");
        }
        for (int j = 0; j < i; j++)
            CHECK(blocks_builtin(j)->id != d->id, "block %s: id unique", d->name);
    }
}

/* the matrix of Stereo Util on a stereo test signal: out = m * (l, r) once the ramp is over */
static void run_block(const struct wvfx_desc *d, void *state, float *knobs, int enabled, float l, float r,
                      float *out_l, float *out_r)
{
    float io[2 * 64];
    for (int f = 0; f < 64; f++)
        io[2 * f] = l, io[2 * f + 1] = r;
    struct wvfx_io b = { io, io, 0, 0, knobs, 500, 64, (uint8_t)enabled, 0 };
    d->process(state, &b);
    *out_l = io[2 * 63];
    *out_r = io[2 * 63 + 1];
}

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static void test_stereo_util(const struct wvfx_desc *d)
{
    static uint8_t state[256];
    float k[WVFX_KNOBS] = { 0 }, l, r;
    for (int i = 0; i < d->nparams; i++)
        k[d->params[i].knob] = d->params[i].def;
    struct wvfx_env env = { WVFX_ABI, 44100.0f, 64, 0, 0, 0, 0 };
    CHECK(d->state_size <= sizeof state && d->init(state, 0, &env, k) == 0, "Stereo Util: init");
    run_block(d, state, k, 1, 0.5f, -0.25f, &l, &r);
    CHECK(l == 0.5f && r == -0.25f, "Stereo Util at its defaults passes the signal (%g %g)", l, r);
    k[4] = 4;                                           /* Channels = Swap */
    run_block(d, state, k, 1, 0.5f, -0.25f, &l, &r);
    CHECK(near(l, -0.25f) && near(r, 0.5f), "Stereo Util: swap at the end of the block (%g %g)", l, r);
    k[4] = 0;
    k[3] = 3;                                           /* Polarity = both */
    k[0] = 6;                                           /* +6 dB */
    run_block(d, state, k, 1, 0.5f, -0.25f, &l, &r);
    float g = powf(10.0f, 6.0f / 20.0f);
    CHECK(fabsf(l + 0.5f * g) < 1e-4f && fabsf(r - 0.25f * g) < 1e-4f, "Stereo Util: inverted, +6 dB (%g %g)", l, r);
    k[3] = 0, k[0] = 0;
    k[2] = 0;                                           /* Width 0 = mono */
    run_block(d, state, k, 1, 0.5f, -0.25f, &l, &r);
    CHECK(near(l, 0.125f) && near(r, 0.125f), "Stereo Util: width 0 is the mid (%g %g)", l, r);
    k[2] = 100;
    k[1] = 50;                                          /* balance half right */
    run_block(d, state, k, 1, 0.5f, -0.25f, &l, &r);
    CHECK(near(l, 0.25f) && near(r, -0.25f), "Stereo Util: balance R50 halves the left (%g %g)", l, r);
    run_block(d, state, k, 0, 0.5f, -0.25f, &l, &r);
    CHECK(near(l, 0.5f) && near(r, -0.25f), "Stereo Util: bypassed passes the signal (%g %g)", l, r);

    /* a change moves over the block: no jump at its first sample */
    float io[2 * 64];
    for (int f = 0; f < 64; f++)
        io[2 * f] = io[2 * f + 1] = 1.0f;
    k[1] = 0, k[0] = -24;
    struct wvfx_io b = { io, io, 0, 0, k, 500, 64, 1, 0 };
    d->process(state, &b);
    CHECK(io[0] > 0.9f && fabsf(io[126] - powf(10.0f, -24.0f / 20.0f)) < 1e-4f,
          "Stereo Util: -24 dB ramps over the block (%g .. %g)", io[0], io[126]);
}

/* the catalog: built-ins after the library's entries, found by id, with their parameters */
static void test_catalog(void)
{
    static struct catalog cat;
    if (catalog_open(&cat) != 0) {
        CHECK(0, "catalog_open");
        return;
    }
    int lib = lib_count(&cat.lib, MENU_VOL);
    CHECK(catalog_count(&cat, MENU_VOL) == lib + blocks_count(MENU_VOL) && blocks_count(MENU_VOL) >= 1,
          "VOL lists the library's %d entries, then the built-ins", lib);
    struct catalog_item item;
    int ok = catalog_item(&cat, MENU_VOL, lib, &item) == 0 && item.block && strcmp(item.name, "Stereo Util") == 0 &&
             item.id == WVFX_ID(WVFX_VOL, 1) && item.nparams == item.block->nparams && !item.engine.elf &&
             strcmp(item.engine.category, "Utility") == 0;
    CHECK(ok, "VOL item %d: Stereo Util, a block with its parameters and category", lib);
    int menu = MENU_VOL, index = -1;
    CHECK(catalog_resolve(&cat, &menu, WVFX_ID(WVFX_VOL, 1), &index) == RESOLVE_EXACT && index == lib,
          "Stereo Util resolves by id (index %d)", index);
    menu = MENU_VOL;
    CHECK(catalog_resolve(&cat, &menu, WVFX_ID(WVFX_VOL, 0x1FF), &index) == RESOLVE_MISSING && index == CATALOG_MISSING &&
          menu == MENU_VOL, "an id of waveod's not installed: missing, kept in its menu");
    struct catalog_item gone;
    CHECK(catalog_item(&cat, MENU_VOL, CATALOG_MISSING, &gone) == 0 && strcmp(gone.name, "AUSENTE") == 0 &&
          !gone.block && !gone.engine.elf && gone.nparams == 0, "a missing item: AUSENTE, nothing to run");
    menu = MENU_VOL;
    CHECK(catalog_resolve(&cat, &menu, 0x0F00FFFFu, &index) == RESOLVE_FALLBACK && index == 0,
          "an unknown id of the original's still falls back to the menu's first");
    struct catalog_item first;
    CHECK(catalog_item(&cat, MENU_VOL, 0, &first) == 0 && !first.block && first.nparams == first.engine.nparams,
          "VOL item 0 is the library's");

    const char *want[] = { "0.0 dB", "Center", "100%", "Normal", "Stereo" };
    for (int k = 0; k < item.nparams && k < 5; k++) {
        struct lib_param p;
        char text[24] = "";
        if (catalog_param(&item, k, &p) == 0)
            param_format(&p, p.def, 0, text, sizeof text);
        CHECK(strcmp(text, want[k]) == 0, "Stereo Util parameter %d at its default shows \"%s\"", k, text);
    }
    struct lib_param p;
    char text[24];
    catalog_param(&item, 4, &p);
    param_format(&p, 4.0f, 0, text, sizeof text);
    CHECK(strcmp(text, "Swap") == 0 && param_list_size(&p) == 5, "Stereo Util Channels 4 shows \"%s\"", text);
    catalog_param(&item, 1, &p);
    param_format(&p, -30.0f, 0, text, sizeof text);
    CHECK(strcmp(text, "L30") == 0, "Stereo Util Balance -30 shows \"%s\"", text);

    /* a Cab item: the library's engine entry with waveod's engine block while it is in use */
    if (catalog_count(&cat, MENU_CAB) > 0) {
        struct catalog_item cab;
        catalog_item(&cat, MENU_CAB, 0, &cab);
        CHECK(cab.block && cab.block->flags & WVFX_ENGINE && cab.nparams == cab.engine.nparams + cab.block->nparams,
              "Cab item 0: the entry's %d parameters, then the engine block's", cab.engine.nparams);
        cabir_set_in_use(0);
        catalog_item(&cat, MENU_CAB, 0, &cab);
        CHECK(!cab.block && cab.nparams == cab.engine.nparams, "Cab item 0 on the original's engine: its entry only");
        cabir_set_in_use(1);
    }

    /* FX Loop, SND, RTN: the original's entries, run by waveod's blocks (WVFX_REPLACE), not listed again */
    static const int loops[3] = { MENU_LOOP, MENU_SND, MENU_RTN };
    for (int i = 0; i < 3 && lib_count(&cat.lib, loops[i]) > 0; i++) {
        struct catalog_item it;
        int m = loops[i];
        int lok = catalog_count(&cat, m) == lib_count(&cat.lib, m) && catalog_item(&cat, m, 0, &it) == 0 &&
                  it.block == blocks_replacement(m) && it.block && it.id == it.engine.id && !WVFX_IS_ID(it.id) &&
                  it.nparams == it.engine.nparams + it.block->nparams;
        CHECK(lok, "menu %d item 0: the library's entry (id 0x%08x) run by waveod's block, its parameters after", m,
              it.id);
    }
}

/* waveod's library (fw/wv_library.h): a one-entry image as tools/wvfx.py packs it, checked, read, broken */
static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void test_library(void)
{
    static const uint8_t check[] = "123456789";
    CHECK(crc32(0, check, 9) == 0xCBF43926u, "crc32 of \"123456789\": %08x", crc32(0, check, 9));

    static uint8_t img[512];
    memset(img, 0xff, sizeof img);
    uint32_t head = 0x18 + 4 * 16, lists = head, entry;
    put32(img + 0x04, head);
    uint8_t *l = img + lists;
    for (int m = 0; m < 16; m++)
        put32(img + 0x18 + 4 * m, lists + 4 * (uint32_t)m + (m > MENU_DLY ? 4 : 0));
    entry = (lists + 4 * 16 + 4 + 7) & ~7u;
    for (int m = 0; m < 16; m++)
        put32(l + 4 * m + (m > MENU_DLY ? 4 : 0), m == MENU_DLY ? 1 : 0);
    put32(l + 4 * MENU_DLY + 4, entry);
    uint8_t *e = img + entry;
    const char strings[] = "Echo\0Delay\0Mode\0Tape\0BBD\0\0Mix\0%.0f%%";
    uint32_t hdr = 24 + 2 * 24 + sizeof strings;
    put32(e + 8, WVFX_ID(WVFX_DLY, 7));
    put32(e + 12, 0xffffffffu);
    e[16] = 2, e[17] = 0, e[18] = 0, e[19] = 0;
    e[20] = 0, e[21] = 0, e[22] = 5, e[23] = 0;     /* name "Echo" at 0, category "Delay" at 5 */
    uint8_t *p = e + 24;
    float f[4] = { 0, 1, 1, 0 };
    p[0] = 16, p[1] = 0, p[2] = 11, p[3] = 0;       /* options at 16, name "Mode" at 11 */
    memcpy(p + 4, f, 16);
    p[20] = 0, p[21] = LIB_P_OPTIONS, p[22] = 0, p[23] = 0xff;
    p += 24;
    float g[4] = { 0, 100, 1, 50 };
    p[0] = 30, p[1] = 0, p[2] = 26, p[3] = 0;       /* unit at 30, name "Mix" at 26 */
    memcpy(p + 4, g, 16);
    p[20] = 1, p[21] = LIB_P_UNIT, p[22] = 0, p[23] = 0xff;
    memcpy(e + 72, strings, sizeof strings);
    put32(e, hdr);
    put32(e + 4, hdr);
    uint32_t total = entry + hdr;
    put32(img + 0x08, entry);
    put32(img + 0x0C, total);
    put32(img + 0x14, 16);
    memcpy(img, "WVLB", 4);
    put32(img + 0x10, crc32(0, img + 0x18, total - 0x18));
    CHECK(wvlib_check(img, sizeof img) == 0, "a library as packed: valid");

    struct library lib;
    struct lib_entry le;
    struct lib_param a, b;
    int ok = lib_open(&lib, img, total) == 0;
    lib.wv = 1;
    ok = ok && lib_count(&lib, MENU_DLY) == 1 && lib_entry(&lib, MENU_DLY, 0, &le) == 0 && strcmp(le.name, "Echo") == 0 &&
         lib_param(&le, 0, &a) == 0 && lib_param(&le, 1, &b) == 0;
    char text[24] = "";
    if (ok)
        param_format(&b, 50.0f, 0, text, sizeof text);
    CHECK(ok && param_list_size(&a) == 2 && strcmp(param_list_item(&a, 1), "BBD") == 0 && !a.unit && b.unit &&
          !b.options && strcmp(text, "50%") == 0, "a library entry: options of Mode, unit of Mix (\"%s\")", text);
    img[total - 1] ^= 1;
    CHECK(wvlib_check(img, sizeof img) != 0, "one byte changed: invalid");
    img[total - 1] ^= 1;
    memcpy(img, "WVLX", 4);
    CHECK(wvlib_check(img, sizeof img) != 0, "another magic: invalid");
}

static const struct wvfx_desc *builtin(uint32_t id)
{
    for (int i = 0; i < blocks_builtin_count(); i++)
        if (blocks_builtin(i)->id == id)
            return blocks_builtin(i);
    return 0;
}

/* `blocks` blocks of a block: io = a sine of amplitude `level` (aux: `key_level`, the same sine); returns the
   ratio of the last block's peak to the input's */
static float run_dyn(const struct wvfx_desc *d, void *st, float *k, int enabled, float level, float key_level,
                     int blocks, int *phase)
{
    float io[2 * 64], aux[2 * 64], peak = 0.0f;
    for (int b = 0; b < blocks; b++) {
        for (int f = 0; f < 64; f++, ++*phase) {
            float s = sinf(2.0f * 3.14159265f * 441.0f * (float)*phase / 44100.0f);
            io[2 * f] = io[2 * f + 1] = level * s;
            aux[2 * f] = aux[2 * f + 1] = key_level * s;
        }
        struct wvfx_io x = { io, aux, 0, 0, k, 500, 64, (uint8_t)enabled, 0 };
        d->process(st, &x);
        if (b == blocks - 1)
            for (int f = 0; f < 64; f++)
                peak = fabsf(io[2 * f]) > peak ? fabsf(io[2 * f]) : peak;
    }
    return peak / level;
}

static void defaults(const struct wvfx_desc *d, float *k)
{
    for (int i = 0; i < d->nparams; i++)
        k[d->params[i].knob] = d->params[i].def;
}

static void test_dynamics(void)
{
    static uint8_t st[256];
    struct wvfx_env env = { WVFX_ABI, 44100.0f, 64, 0, 0, 0, 0 };
    float k[WVFX_KNOBS] = { 0 };
    int ph = 0;
    CHECK(fabsf(wvfx_log2(8.0f) - 3.0f) < 1e-4f && fabsf(wvfx_to_db(0.1f) + 20.0f) < 1e-3f &&
          fabsf(wvfx_log2(0.7f) - log2f(0.7f)) < 1e-4f, "wvfx_log2 / wvfx_to_db");

    const struct wvfx_desc *g = builtin(WVFX_ID(WVFX_DYN, 1));
    if (!g || g->state_size > sizeof st) {
        CHECK(0, "Gate SC built in");
        return;
    }
    defaults(g, k);                                     /* -55 dB, range 90, key Input */
    g->init(st, 0, &env, k);
    float open = run_dyn(g, st, k, 1, 0.1f, 0.1f, 20, &ph);
    float shut = run_dyn(g, st, k, 1, 0.1f, 0.0005f, 1200, &ph);    /* key at -66 dB: closes after hold + release */
    CHECK(open > 0.99f && shut < 1e-4f, "Gate SC: a loud key passes (%g), a quiet key closes it (%g)", open, shut);
    float again = run_dyn(g, st, k, 1, 0.1f, 0.1f, 10, &ph);
    CHECK(again > 0.99f, "Gate SC: opens again with the key (%g)", again);
    k[1] = 20;                                          /* range 20 dB */
    shut = run_dyn(g, st, k, 1, 0.1f, 0.0005f, 1200, &ph);
    CHECK(fabsf(shut - 0.1f) < 0.002f, "Gate SC: range 20 dB leaves 0.1 (%g)", shut);
    k[5] = 1;                                           /* key Chain: the loud signal itself keeps it open */
    float chain = run_dyn(g, st, k, 1, 0.1f, 0.0005f, 100, &ph);
    CHECK(chain > 0.99f, "Gate SC: keyed by the chain, the signal keeps it open (%g)", chain);
    k[5] = 0;
    run_dyn(g, st, k, 1, 0.1f, 0.0005f, 300, &ph);
    float off = run_dyn(g, st, k, 0, 0.1f, 0.0005f, 30, &ph);
    CHECK(off > 0.99f, "Gate SC: bypassed it passes (%g)", off);

    const struct wvfx_desc *c = builtin(WVFX_ID(WVFX_DYN, 2));
    if (!c || c->state_size > sizeof st) {
        CHECK(0, "Comp SC built in");
        return;
    }
    memset(k, 0, sizeof k);
    defaults(c, k);                                     /* -20 dB, 4:1, knee 6, key Chain */
    c->init(st, 0, &env, k);
    float lvl = 0.5f;                                   /* -6 dB: 14 dB over, 10.5 dB of reduction */
    float got = run_dyn(c, st, k, 1, lvl, lvl, 400, &ph);
    float want = powf(10.0f, -10.5f / 20.0f);
    CHECK(fabsf(got - want) < 0.02f * want, "Comp SC: -6 dB at 4:1 over -20 dB: gain %g (want %g)", got, want);
    float under = run_dyn(c, st, k, 1, 0.05f, 0.05f, 600, &ph);     /* -26 dB: below the knee */
    CHECK(fabsf(under - 1.0f) < 0.01f, "Comp SC: below the knee, unity (%g)", under);
    k[5] = 6;                                           /* makeup */
    k[6] = 0;                                           /* mix 0: the dry signal */
    float dry = run_dyn(c, st, k, 1, lvl, lvl, 50, &ph);
    CHECK(fabsf(dry - 1.0f) < 1e-4f, "Comp SC: mix 0 is the dry signal (%g)", dry);
    k[6] = 100;
    k[7] = 0;                                           /* key Input: a quiet key, no reduction, makeup only */
    float keyed = run_dyn(c, st, k, 1, lvl, 0.01f, 600, &ph);
    CHECK(fabsf(keyed - powf(10.0f, 6.0f / 20.0f)) < 0.01f, "Comp SC: keyed by a quiet input, makeup only (%g)",
          keyed);
    float bypass = run_dyn(c, st, k, 0, lvl, lvl, 3, &ph);
    CHECK(fabsf(bypass - 1.0f) < 1e-4f, "Comp SC: bypassed, the dry signal (%g)", bypass);
}

/* one block of FX Loop / SND / RTN with a constant input (l, r) and return (rl, rr): the last frame's output and send */
static void run_loop(const struct wvfx_desc *d, void *state, const float *knobs, int enabled, float *out, float *send)
{
    float io[2 * 64], snd[2 * 64], ret[2 * 64];
    for (int f = 0; f < 64; f++) {
        io[2 * f] = 0.5f, io[2 * f + 1] = 0.25f;
        ret[2 * f] = 0.125f, ret[2 * f + 1] = -0.0625f;
        snd[2 * f] = snd[2 * f + 1] = 0.0f;
    }
    struct wvfx_io b = { io, io, snd, ret, knobs, 500, 64, (uint8_t)enabled, 0 };
    d->process(state, &b);
    out[0] = io[2 * 63], out[1] = io[2 * 63 + 1];
    send[0] = snd[2 * 63], send[1] = snd[2 * 63 + 1];
}

static const struct wvfx_desc *builtin_id(uint32_t id)
{
    for (int i = 0; i < blocks_builtin_count(); i++)
        if (blocks_builtin(i)->id == id)
            return blocks_builtin(i);
    return 0;
}

/* FX Loop, SND and RTN (blocks/fx_loop.c): send x level, dry / return by Mix as the original, phase, bypass */
static void test_fx_loop(void)
{
    static uint8_t st[64];
    const struct wvfx_desc *loop = builtin_id(WVFX_ID(WVFX_LOOP, 1)), *snd = builtin_id(WVFX_ID(WVFX_SND, 1));
    const struct wvfx_desc *rtn = builtin_id(WVFX_ID(WVFX_RTN, 1));
    CHECK(loop && snd && rtn && blocks_replacement(MENU_LOOP) == loop && blocks_replacement(MENU_SND) == snd &&
          blocks_replacement(MENU_RTN) == rtn && blocks_count(MENU_LOOP) == 0, "FX Loop, SND, RTN: replacements");
    if (!loop || !snd || !rtn)
        return;
    float o[2], s[2];
    static const struct {
        float k[4];
        float out[2], send;
        const char *what;
    } cases[] = {
        { { 100, 100, 50, 0 }, { 0.625f, 0.1875f }, 0.375f, "Mix 50: dry + return, send the mono sum" },
        { { 100, 100, 0, 0 }, { 0.5f, 0.25f }, 0.375f, "Mix 0: dry" },
        { { 100, 100, 100, 0 }, { 0.125f, -0.0625f }, 0.375f, "Mix 100: the return" },
        { { 50, 25, 50, 0 }, { 0.53125f, 0.234375f }, 0.1875f, "Send 50, Return 25" },
        { { 0, 100, 30, 0 }, { 0.575f, 0.2125f }, 0.0f, "Send 0, Mix 30: return x 0.6" },
        { { 100, 100, 100, 1 }, { -0.125f, 0.0625f }, 0.375f, "Return Phase Invert" },
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        float k[WVFX_KNOBS] = { 0 };
        memcpy(k, cases[c].k, sizeof cases[c].k);
        loop->init(st, 0, 0, k);
        run_loop(loop, st, k, 1, o, s);
        CHECK(near(o[0], cases[c].out[0]) && near(o[1], cases[c].out[1]) && near(s[0], cases[c].send) &&
              near(s[1], cases[c].send), "FX Loop %s: out (%g, %g) send %g", cases[c].what, o[0], o[1], s[0]);
    }
    float k[WVFX_KNOBS] = { 100, 100, 50 };
    loop->init(st, 0, 0, k);
    run_loop(loop, st, k, 0, o, s);                     /* the switch: one block of ramp */
    run_loop(loop, st, k, 0, o, s);
    CHECK(near(o[0], 0.5f) && near(o[1], 0.25f) && s[0] == 0.0f, "FX Loop bypassed: dry, nothing sent");
    float ks[WVFX_KNOBS] = { 50 };
    snd->init(st, 0, 0, ks);
    run_loop(snd, st, ks, 1, o, s);
    CHECK(o[0] == 0.5f && o[1] == 0.25f && near(s[0], 0.1875f), "SND: passes the signal, sends x 0.5 (%g)", s[0]);
    float kr[WVFX_KNOBS] = { 100, 50, 1 };
    rtn->init(st, 0, 0, kr);
    run_loop(rtn, st, kr, 1, o, s);
    CHECK(near(o[0], 0.375f) && near(o[1], 0.3125f) && s[0] == 0.0f, "RTN inverted at Mix 50: dry - return (%g, %g)",
          o[0], o[1]);
    float kz[WVFX_KNOBS] = { 0, 50 };                   /* a knob moving: the gain ramps over the block */
    rtn->init(st, 0, 0, kz);
    float io[2 * 64], ret[2 * 64], snd0[2 * 64] = { 0 };
    for (int f = 0; f < 64; f++)
        io[2 * f] = io[2 * f + 1] = 0.0f, ret[2 * f] = ret[2 * f + 1] = 1.0f;
    kz[0] = 100;
    struct wvfx_io b = { io, io, snd0, ret, kz, 500, 64, 1, 0 };
    rtn->process(st, &b);
    CHECK(near(io[0], 1.0f / 64.0f) && near(io[2 * 63], 1.0f), "RTN: Return Level ramps over one block (%g .. %g)",
          io[0], io[2 * 63]);
}

void test_blocks(void)
{
    test_fx_loop();
    test_library();
    test_dynamics();
    test_descriptors();
    for (int i = 0; i < blocks_builtin_count(); i++)
        if (blocks_builtin(i)->id == WVFX_ID(WVFX_VOL, 1))
            test_stereo_util(blocks_builtin(i));
}

void test_blocks_catalog(void)
{
    test_catalog();
}
