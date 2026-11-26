/*
 * Flyman 2 (AMP › Flyman): the original's Flyman 2 amp (QME-200 V1.2.2, library entry 4.37, id 0x07000041) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). Mono: the left input, the result on both
 * outputs. The path is in flyman.h; this model: the PfMode B, the low pass 1, the clip +-3, the presence on the tone
 * stack, x 1.04 out; the tone stack in double.
 */
#include "flyman.h"

static const struct flyman_model MODEL = {
    &FLYMAN_PFMODE_B, &FLYMAN_LOWPASS_1, 3.0f, 0.001171875f, 0.00146484375f, 1.0f, 1.0399999618530273f, 0, 1,
};

FLYMAN_BLOCK(flyman_2_block, 0x41, "Flyman 2")
