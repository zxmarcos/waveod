/*
 * Flyman+ 2 (AMP › Flyman): the original's Flyman+ 2 amp (QME-200 V1.2.2, library entry 4.39, id 0x0700005E) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The path is in flyman.h; this model: the PfMode A, the low pass 2, the clip +-0.8, the presence on 3 x the
 * tone stack, x 1.04 out; the tone stack in double.
 */
#include "flyman.h"

static const struct flyman_model MODEL = {
    &FLYMAN_PFMODE_A, &FLYMAN_LOWPASS_2, 0.8f, 0.0027343749534338713f, 0.0029296875f, 3.0f, 1.0399999618530273f, 0, 1,
};

FLYMAN_BLOCK(flyman_p2_block, 0x5E, "Flyman+ 2")
