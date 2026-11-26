#include "fw/library.h"
#include "fw/bytes.h"

#define ENTRY_FIXED   24u
#define PARAM_SIZE    24u

static const char *const menu_tags[LIB_MAX_MENUS] = {
    "DYN", "FX", "WAH", "DRV", "AMP", "CAB", "IR", "EQ",
    "MOD", "DLY", "RVB", "CLN", "LOOP", "SND", "RTN", "VOL"
};

const char *lib_menu_tag(int menu)
{
    return menu >= 0 && menu < LIB_MAX_MENUS ? menu_tags[menu] : "?";
}

int lib_open(struct library *lib, const uint8_t *data, uint32_t max_size)
{
    lib->base = 0;
    lib->size = 0;
    lib->menus = 0;
    lib->wv = 0;
    if (!data || max_size < 0x18)
        return -1;
    uint32_t ncat = rd32(data + 0x14), total = rd32(data + 0x0c);
    if (ncat == 0 || ncat > LIB_MAX_MENUS || rd32(data + 0x04) != 0x18 + 4 * ncat)
        return -1;
    if (total < rd32(data + 0x08) || total > max_size)
        return -1;
    lib->base = data;
    lib->size = total;
    lib->menus = (int)ncat;
    return 0;
}

static const uint8_t *list_of(const struct library *lib, int menu)
{
    if (!lib->base || menu < 0 || menu >= lib->menus)
        return 0;
    uint32_t off = rd32(lib->base + 0x18 + 4u * (uint32_t)menu);
    if (off + 4 > lib->size)
        return 0;
    return lib->base + off;
}

int lib_count(const struct library *lib, int menu)
{
    const uint8_t *l = list_of(lib, menu);
    return l ? (int)rd32(l) : 0;
}

int lib_entry(const struct library *lib, int menu, int index, struct lib_entry *out)
{
    const uint8_t *l = list_of(lib, menu);
    if (!l || index < 0 || (uint32_t)index >= rd32(l))
        return -1;
    uint32_t off = rd32(l + 4 + 4u * (uint32_t)index);
    if (off + ENTRY_FIXED > lib->size)
        return -1;
    const uint8_t *e = lib->base + off;
    uint32_t total = rd32(e), hdr = rd32(e + 4);
    if (hdr > total || off + total > lib->size)
        return -1;
    out->id = rd32(e + 8);
    /* waveod's own block (id 0x57MMnnnn, sdk/wvfx.h WVFX_IS_ID): its records name options / units in either library
       (the update file carries waveod's effects in section 1 too: tools/portfw.py); the original's ids are
       menu << 24 | n and never start with 0x57 */
    out->wv = lib->wv || (out->id >> 24) == 0x57u;
    out->link_id = (int32_t)rd32(e + 12);
    out->nparams = e[16];
    out->subgroup = e[17];
    out->params = e + ENTRY_FIXED;
    out->strings = e + ENTRY_FIXED + PARAM_SIZE * out->nparams;
    out->name = (const char *)out->strings + rd16(e + 20);
    out->category = (const char *)out->strings + rd16(e + 22);
    out->elf = e + hdr;
    out->elf_size = total - hdr;
    return 0;
}

int lib_param(const struct lib_entry *e, int k, struct lib_param *out)
{
    if (k < 0 || k >= e->nparams)
        return -1;
    const uint8_t *p = e->params + PARAM_SIZE * (unsigned)k;
    out->name = (const char *)e->strings + rd16(p + 2);
    out->min = rdf32(p + 4);
    out->max = rdf32(p + 8);
    out->step = rdf32(p + 12);
    out->def = rdf32(p + 16);
    out->idx = p[20];
    out->f1 = p[21];
    out->f2 = p[22];
    out->f3 = p[23];
    out->options = 0;
    out->unit = 0;
    if (e->wv && rd16(p) != 0xFFFF) {       /* waveod's: the first field names the options or the unit */
        const char *s = (const char *)e->strings + rd16(p);
        if (out->f1 & LIB_P_OPTIONS)
            out->options = s;
        else if (out->f1 & LIB_P_UNIT)
            out->unit = s;
    }
    return 0;
}

int lib_find(const struct library *lib, int menu, uint32_t id)
{
    int n = lib_count(lib, menu);
    struct lib_entry e;
    for (int i = 0; i < n; i++)
        if (lib_entry(lib, menu, i, &e) == 0 && e.id == id)
            return i;
    return -1;
}

int lib_locate(const struct library *lib, uint32_t id, int *menu, int *index)
{
    for (int m = 0; m < lib->menus; m++) {
        int i = lib_find(lib, m, id);
        if (i >= 0) {
            *menu = m;
            *index = i;
            return 0;
        }
    }
    return -1;
}
