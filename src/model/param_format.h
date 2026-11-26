/*
 * Parameter value text, following the display formats of the original app (parameter flag f3).
 */
#pragma once

#include "fw/library.h"

#define PARAM_FORMAT_COUNT 0xC4
/* waveod's own formats, past the original's (sdk/wvfx.h WVFX_FMT_*; a block's parameter may also name its values
   or give a unit: lib_param options / unit, which come first) */
#define PARAM_FORMAT_WV_SIGNED   0xF0         /* "+40", "0", "-40" */
#define PARAM_FORMAT_WV_DEGREES  0xF1         /* "30°" */

enum param_format_kind {
    FMT_NUMBER = 0,     /* "%.0f" (also every format this table does not know) */
    FMT_LIST,           /* option names indexed by the value */
    FMT_PRINTF,         /* one template */
    FMT_MIN_WORD,       /* "Off" / "Mute" at the minimum, template otherwise */
    FMT_MAX_WORD,       /* "Limit" at the maximum, template otherwise */
    FMT_PAN,            /* "Center" at 0, R / L otherwise */
    FMT_SIGNED,         /* "+x" above 0 */
    FMT_THOUSANDS,      /* second template (value / 1000) from 1000 up: Hz -> kHz, ms -> s */
};

struct param_format {
    uint8_t kind;
    const char *const *list;
    const char *tmpl[3];
};

extern const struct param_format param_formats[PARAM_FORMAT_COUNT];

/* Tempo divisions shown for a synced parameter (index 0..10). */
extern const char *const param_sync_divisions[11];

/* Format `value` of parameter `p` into `out` (at least 24 bytes). */
void param_format(const struct lib_param *p, float value, int synced, char *out, unsigned size);

/* Number of options of a list format, 0 otherwise. */
int param_list_size(const struct lib_param *p);

/* Option i of a list format, or 0. */
const char *param_list_item(const struct lib_param *p, int i);
