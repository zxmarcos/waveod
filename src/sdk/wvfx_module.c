/*
 * The header of a block built as a module (tools/wvfx.py build compiles it with -DWVFX_DESC=<the descriptor's
 * name>, by convention <file name>_block): first in the code region (sdk/wvfx.ld), pointing at the descriptor.
 */
#include "wvfx.h"

#ifndef WVFX_DESC
#error "WVFX_DESC: the name of the block's descriptor"
#endif

extern const struct wvfx_desc WVFX_DESC;

__attribute__((section(".module_header"), used))
const struct wvfx_module_header wvfx_module_header = {
    0, WVFX_MAGIC, WVFX_ABI, &WVFX_DESC, 0, 0, 0, 0, 0, 0xFFAA55AAu,
};
