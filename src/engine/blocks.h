/*
 * waveod's own effect blocks (sdk/wvfx.h, docs/blocks.md): the built-in ones, by menu.
 *
 * A menu lists the original library's entries first, then these (fw/catalog.c). Engine blocks (WVFX_ENGINE) are
 * not listed: they run the items of a menu of the original's (Cab, User IR; Clone items that hold a NAM capture)
 * in place of its library entry. Replacement blocks (WVFX_REPLACE) are not listed either: they run a menu's library
 * entries in place of the original's module (FX Loop, SND, RTN), the entry's id, name and knobs kept.
 */
#pragma once

#include "fw/library.h"
#include "sdk/wvfx.h"

/* Built-ins listed in `menu` (engine blocks left out), and the i-th of them. */
int blocks_count(int menu);
const struct wvfx_desc *blocks_at(int menu, int i);

/* The built-in with `id` in `menu`, its index there, or -1. */
int blocks_find(int menu, uint32_t id);

/* The engine block of `menu`: the one in use (0 when the original's entry runs the menu), or with `any` whether
   it is in use or not. */
const struct wvfx_desc *blocks_engine(int menu, int any);

/* The block that runs the library entries of `menu` in place of the original's modules, or 0. */
const struct wvfx_desc *blocks_replacement(int menu);

/* The engine block of a Clone item with this data (`size` bytes of its slot, 0 = none): NAM for a NAM capture
   (engine/nam_block.c), else 0 (the original's Clone module). */
const struct wvfx_desc *blocks_clone_engine(const void *data, uint32_t size);

/* Parameter k of a block, as the library's records read (lib_param). 0 = there is one. */
int blocks_param(const struct wvfx_desc *d, int k, struct lib_param *out);

/* Every built-in, for the checks (tests, the describe of the PC app). */
int blocks_builtin_count(void);
const struct wvfx_desc *blocks_builtin(int i);
