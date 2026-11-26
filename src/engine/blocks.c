#include "engine/blocks.h"
#include "engine/cabir.h"
#include "nam/capture.h"

_Static_assert(WVFX_VOL == MENU_VOL && WVFX_CAB == MENU_CAB && WVFX_IR == MENU_IR && WVFX_DLY == MENU_DLY,
               "sdk/wvfx.h menus follow fw/library.h");
_Static_assert(WVFX_KNOBS == 15 && WVFX_MAX_PARAMS == LIB_MAX_PARAMS, "sdk/wvfx.h knobs follow the presets");

extern const struct wvfx_desc cabir_block;          /* engine/cabir_block.c */
extern const struct wvfx_desc stereo_util_block;    /* blocks/stereo_util.c */
extern const struct wvfx_desc gate_sc_block;        /* blocks/gate_sc.c */
extern const struct wvfx_desc comp_sc_block;        /* blocks/comp_sc.c */
extern const struct wvfx_desc nam_block;            /* engine/nam_block.c */
extern const struct wvfx_desc fx_loop_block;        /* blocks/fx_loop.c */
extern const struct wvfx_desc fx_send_block;
extern const struct wvfx_desc fx_return_block;

static const struct wvfx_desc *const builtins[] = {
    &cabir_block,
    &nam_block,
    &stereo_util_block,
    &gate_sc_block,
    &comp_sc_block,
    &fx_loop_block,
    &fx_send_block,
    &fx_return_block,
};

#define BUILTINS ((int)(sizeof builtins / sizeof builtins[0]))

int blocks_builtin_count(void)
{
    return BUILTINS;
}

const struct wvfx_desc *blocks_builtin(int i)
{
    return i >= 0 && i < BUILTINS ? builtins[i] : 0;
}

static int listed(const struct wvfx_desc *d, int menu)
{
    return d->menu == menu && !(d->flags & (WVFX_ENGINE | WVFX_REPLACE));
}

int blocks_count(int menu)
{
    int n = 0;
    for (int i = 0; i < BUILTINS; i++)
        n += listed(builtins[i], menu);
    return n;
}

const struct wvfx_desc *blocks_at(int menu, int i)
{
    for (int k = 0; k < BUILTINS; k++)
        if (listed(builtins[k], menu) && i-- == 0)
            return builtins[k];
    return 0;
}

int blocks_find(int menu, uint32_t id)
{
    int n = 0;
    for (int k = 0; k < BUILTINS; k++) {
        if (!listed(builtins[k], menu))
            continue;
        if (builtins[k]->id == id)
            return n;
        n++;
    }
    return -1;
}

const struct wvfx_desc *blocks_engine(int menu, int any)
{
    if (menu != MENU_CAB && menu != MENU_IR)        /* the only engine block: Cab and User IR */
        return 0;
    return any || cabir_in_use() ? &cabir_block : 0;
}

const struct wvfx_desc *blocks_replacement(int menu)
{
    for (int k = 0; k < BUILTINS; k++)
        if (builtins[k]->menu == menu && (builtins[k]->flags & WVFX_REPLACE))
            return builtins[k];
    return 0;
}

const struct wvfx_desc *blocks_clone_engine(const void *data, uint32_t size)
{
    return nam_capture_is(data, size) ? &nam_block : 0;
}

int blocks_param(const struct wvfx_desc *d, int k, struct lib_param *out)
{
    if (!d || k < 0 || k >= d->nparams)
        return -1;
    const struct wvfx_param *p = &d->params[k];
    out->name = p->name;
    out->min = p->min;
    out->max = p->max;
    out->step = p->step;
    out->def = p->def;
    out->idx = p->knob;
    out->f1 = 0;
    out->f2 = p->sync;
    out->f3 = p->format ? p->format : 0xFF;    /* the original's 0 is a list: a code no table has = a number */
    out->options = p->options;
    out->unit = p->unit;
    return 0;
}
