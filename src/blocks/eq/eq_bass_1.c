/*
 * Bass EQ 1 (EQ): the original's Bass EQ 1 (QME-200 V1.2.2, library entry 7.2, id 0x01000039) in C, its output bit for bit
 * (tools/check_stock.py; docs/stock_port.md). The five-band EQ of eq5.h with its bands (33Hz, 150Hz, 600Hz, 2kHz, 8kHz).
 */
#include "eq5.h"

static const struct eq5_model MODEL = {
    { 0.003325096098706126f, 0.015112979337573051f, 0.06896723806858063f, 0.19880561530590057f, 0.4091414511203766f },
    { -1.999977946281433f, -1.999543309211731f, -1.9926966428756714f, -1.9193503856658936f, -1.6313278675079346f },
};

EQ5_BLOCK(eq_bass_1_block, 0x102, "Bass EQ 1", MODEL, "33Hz", "150Hz", "600Hz", "2kHz", "8kHz");
