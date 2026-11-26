/*
 * Guitar EQ 1 (EQ): the original's Guitar EQ 1 (QME-200 V1.2.2, library entry 7.0, id 0x01000035) in C, its output bit for bit
 * (tools/check_stock.py; docs/stock_port.md). The five-band EQ of eq5.h with its bands (125Hz, 400Hz, 800Hz, 1.6kHz, 4kHz).
 */
#include "eq5.h"

static const struct eq5_model MODEL = {
    { 0.012594443745911121f, 0.03236335143446922f, 0.06462161988019943f, 0.09682609140872955f, 0.38157764077186584f },
    { -1.9996827840805054f, -1.9967529773712158f, -1.9870225191116333f, -1.9482582807540894f, -1.683906078338623f },
};

EQ5_BLOCK(eq_guitar_1_block, 0x100, "Guitar EQ 1", MODEL, "125Hz", "400Hz", "800Hz", "1.6kHz", "4kHz");
