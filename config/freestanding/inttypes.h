/* Minimal <inttypes.h> for the freestanding firmware build (LVGL only needs the printf macros). */
#pragma once

#include <stdint.h>

#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"
