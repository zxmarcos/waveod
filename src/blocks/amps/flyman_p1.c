/*
 * Flyman+ 1 (AMP › Flyman): the original's Flyman+ 1 amp (QME-200 V1.2.2, library entry 4.38, id 0x0700005D) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The path is in flyman.h; this model: the PfMode B, the low pass 2, the clip +-0.8 (k 1 / 365.7 below
 * zero, 1 / 341.3 above), the presence on 5 x the tone stack, x 0.7987 x Volume out (Volume also inside the tone
 * stack); the tone stack in single precision.
 */
#include "flyman.h"

static const struct flyman_model MODEL = {
    &FLYMAN_PFMODE_B, &FLYMAN_LOWPASS_2, 0.8f, 0.0027343749534338713f, 0.0029296875f, 5.0f, 0.7987200021743774f, 1, 0,
};

FLYMAN_BLOCK(flyman_p1_block, 0x5D, "Flyman+ 1")
