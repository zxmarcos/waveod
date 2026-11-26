/*
 * Flyman 1 (AMP › Flyman): the original's Flyman 1 amp (QME-200 V1.2.2, library entry 4.36, id 0x07000040) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The path is in flyman.h; this model: the PfMode A, the low pass 1, the clip +-3 (k 1 / 853.3 below
 * zero, 1 / 682.7 above), the presence on 2 x the tone stack, x 0.4118 out; the tone stack in single precision.
 */
#include "flyman.h"

static const struct flyman_model MODEL = {
    &FLYMAN_PFMODE_A, &FLYMAN_LOWPASS_1, 3.0f, 0.001171875f, 0.00146484375f, 2.0f, 0.41183999180793762f, 0, 0,
};

FLYMAN_BLOCK(flyman_1_block, 0x40, "Flyman 1")
