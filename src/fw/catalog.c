#include "fw/catalog.h"
#include "engine/blocks.h"
#include "fw/flash_map.h"
#include "fw/wv_library.h"
#include "fw/ir_fs.h"
#include "nam/capture.h"
#include "sdk/wvfx.h"
#include "platform/flash.h"
#include <string.h>

int catalog_open(struct catalog *c)
{
    memset(c, 0, sizeof *c);
    int err = lib_open(&c->lib, flash_ptr(FLASH_LIBRARY, 0x18), FLASH_LIBRARY_MAX);
    if (err == 0 && !flash_ptr(FLASH_LIBRARY, c->lib.size))
        err = -1;
    ir_dir_open(&c->irs);           /* optional: without it the IR menus are just empty */
    wvlib_open(&c->wvl);            /* optional too */
    return err;
}

int catalog_reopen_wvlib(struct catalog *c)
{
    return wvlib_open(&c->wvl);
}

int catalog_is_special(int menu)
{
    return menu == MENU_CAB || menu == MENU_IR || menu == MENU_CLONE;
}

/* directory index of item `index` of a special menu */
static int dir_index(const struct catalog *c, int menu, int index)
{
    switch (menu) {
    case MENU_CAB:   return index < ir_cab_count(&c->irs) ? index : -1;
    case MENU_IR:    return index < IR_USER_COUNT ? IR_DIR_USER_FIRST(&c->irs) + index : -1;
    case MENU_CLONE: return index < IR_CLONE_COUNT ? IR_DIR_CLONE_FIRST(&c->irs) + index : -1;
    default:         return -1;
    }
}

/* Module k of waveod's library is one the firmware already has (an entry of the effect library with its id, which
   the update file brings: tools/portfw.py, or a built-in): it is not listed again, the firmware's copy plays. */
static int wv_shadowed(const struct catalog *c, int menu, int k)
{
    struct lib_entry e;
    return lib_entry(&c->wvl, menu, k, &e) == 0 &&
           (lib_find(&c->lib, menu, e.id) >= 0 || blocks_find(menu, e.id) >= 0);
}

/* the modules of waveod's library listed in menu */
static int wv_listed(const struct catalog *c, int menu)
{
    int n = 0, total = lib_count(&c->wvl, menu);
    for (int k = 0; k < total; k++)
        n += !wv_shadowed(c, menu, k);
    return n;
}

/* the library index of listed module i, -1 */
static int wv_index(const struct catalog *c, int menu, int i)
{
    int total = lib_count(&c->wvl, menu);
    for (int k = 0; k < total; k++)
        if (!wv_shadowed(c, menu, k) && i-- == 0)
            return k;
    return -1;
}

int catalog_count(const struct catalog *c, int menu)
{
    if (!catalog_is_special(menu))
        return menu >= 0 && menu < c->lib.menus ?
               lib_count(&c->lib, menu) + blocks_count(menu) + wv_listed(c, menu) : 0;
    if (lib_count(&c->lib, menu) < 1 || !c->irs.base)
        return 0;
    switch (menu) {
    case MENU_CAB: return ir_cab_count(&c->irs);
    case MENU_IR:  return IR_USER_COUNT;
    default:       return IR_CLONE_COUNT;
    }
}

int catalog_item(const struct catalog *c, int menu, int index, struct catalog_item *out)
{
    memset(out, 0, sizeof *out);
    if (index == CATALOG_MISSING && menu >= 0 && menu < c->lib.menus) {
        out->engine.link_id = -1;
        out->engine.name = out->engine.category = "";
        memcpy(out->name, CATALOG_MISSING_NAME, sizeof CATALOG_MISSING_NAME);
        return 0;
    }
    if (index < 0 || index >= catalog_count(c, menu))
        return -1;
    if (!catalog_is_special(menu)) {
        int n = lib_count(&c->lib, menu), b = blocks_count(menu);
        if (index >= n + b) {                       /* a module of waveod's library */
            if (lib_entry(&c->wvl, menu, wv_index(c, menu, index - n - b), &out->engine) != 0)
                return -1;
            out->id = out->engine.id;
            out->nparams = out->engine.nparams;
            strncpy(out->name, out->engine.name, sizeof out->name - 1);
            return 0;
        }
        if (index >= n) {                           /* a built-in block */
            const struct wvfx_desc *d = blocks_at(menu, index - n);
            if (!d)
                return -1;
            out->block = d;
            out->id = out->engine.id = d->id;
            out->engine.link_id = -1;
            out->engine.name = d->name;
            out->engine.category = d->category;
            out->nparams = d->nparams;
            strncpy(out->name, d->name, sizeof out->name - 1);
            return 0;
        }
        if (lib_entry(&c->lib, menu, index, &out->engine) != 0)
            return -1;
        out->id = out->engine.id;
        out->block = blocks_replacement(menu);      /* waveod's code in place of the original's module */
        out->nparams = out->engine.nparams + (out->block ? out->block->nparams : 0);
        strncpy(out->name, out->engine.name, sizeof out->name - 1);
        return 0;
    }
    if (lib_entry(&c->lib, menu, 0, &out->engine) != 0)
        return -1;
    char name[IR_FS_NAME_LEN + 1];
    if (ir_dir_entry(&c->irs, dir_index(c, menu, index), &out->id, name) != 0)
        return -1;
    char stored[IR_FS_NAME_LEN + 1];                /* the name a user IR / capture was stored with, if any */
    if (menu != MENU_CAB && ir_fs_name(menu == MENU_CLONE, index, stored) == 0 && stored[0])
        memcpy(name, stored, sizeof stored);
    strncpy(out->name, name, sizeof out->name - 1);
    if (menu == MENU_CLONE) {                       /* the engine follows the slot's data: NAM or the original's */
        uint32_t off = catalog_data(c, menu, index);
        out->block = off ? blocks_clone_engine(flash_ptr(off, IRFS_CLONE_DATA), IRFS_CLONE_DATA) : 0;
    } else {
        out->block = blocks_engine(menu, 0);
    }
    out->nparams = out->engine.nparams + (out->block ? out->block->nparams : 0);
    return 0;
}

int catalog_param(const struct catalog_item *item, int k, struct lib_param *out)
{
    if (k < 0 || k >= item->nparams)
        return -1;
    if (k < item->engine.nparams)
        return lib_param(&item->engine, k, out);
    return blocks_param(item->block, k - item->engine.nparams, out);
}

static int find_item(const struct catalog *c, int menu, uint32_t id)
{
    if (!catalog_is_special(menu)) {
        int i = lib_find(&c->lib, menu, id);
        if (i < 0 && (i = blocks_find(menu, id)) >= 0)
            i += lib_count(&c->lib, menu);
        if (i < 0 && (i = lib_find(&c->wvl, menu, id)) >= 0) {   /* not shadowed: found above otherwise */
            int k = i;
            for (int j = 0; j < k; j++)
                i -= wv_shadowed(c, menu, j);
            i += lib_count(&c->lib, menu) + blocks_count(menu);
        }
        return i;
    }
    int n = catalog_count(c, menu);
    for (int i = 0; i < n; i++) {
        uint32_t item_id;
        if (ir_dir_entry(&c->irs, dir_index(c, menu, i), &item_id, 0) == 0 && item_id == id)
            return i;
    }
    return -1;
}

enum resolve_result catalog_resolve(const struct catalog *c, int *menu_io, uint32_t id, int *index)
{
    int menu = *menu_io;
    if (menu < 0 || menu >= c->lib.menus)
        return RESOLVE_INVALID;
    int i = find_item(c, menu, id);
    if (i >= 0) {
        *index = i;
        return RESOLVE_EXACT;
    }
    if (menu + 1 < c->lib.menus && (i = find_item(c, menu + 1, id)) >= 0) {
        *menu_io = menu + 1;
        *index = i;
        return RESOLVE_EXACT;
    }
    if (WVFX_IS_ID(id) && !catalog_is_special(menu)) {   /* waveod's, not installed here: kept as it is */
        *index = CATALOG_MISSING;
        return RESOLVE_MISSING;
    }
    if (catalog_count(c, menu) < 1)
        return RESOLVE_INVALID;
    *index = 0;
    return RESOLVE_FALLBACK;
}

enum clone_kind catalog_clone_kind(const struct catalog *c, int index)
{
    uint32_t off = catalog_data(c, MENU_CLONE, index);
    const void *p = off ? flash_ptr(off, IRFS_CLONE_DATA) : 0;
    if (!nam_capture_is(p, IRFS_CLONE_DATA))
        return CLONE_ORIGINAL;
    return nam_capture_resampled(p) ? CLONE_NAM_48K : CLONE_NAM;
}

uint32_t catalog_data(const struct catalog *c, int menu, int index)
{
    switch (menu) {
    case MENU_CAB:   return ir_cab_data(&c->irs, index);
    case MENU_IR:    return ir_user_data(index);
    case MENU_CLONE: return ir_clone_data(&c->irs, index);
    default:         return 0;
    }
}
