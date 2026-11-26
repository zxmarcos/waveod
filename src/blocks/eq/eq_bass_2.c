/*
 * Bass EQ 2 (EQ): the original's Bass EQ 2 (QME-200 V1.2.2, library entry 7.3, id 0x0100003A) in C, its output bit for bit
 * (tools/check_stock.py; docs/stock_port.md). The five-band EQ of eq5.h with its bands (50Hz, 120Hz, 400Hz, 800Hz, 4.5kHz).
 */
#include "eq5.h"

static const struct eq5_model MODEL = {
    { 0.005038000643253326f, 0.012090715579688549f, 0.04600928723812103f, 0.08043426275253296f, 0.42299190163612366f },
    { -1.9999492168426514f, -1.9997076988220215f, -1.9967529773712158f, -1.9870225191116333f, -1.6028271913528442f },
};

EQ5_BLOCK(eq_bass_2_block, 0x103, "Bass EQ 2", MODEL, "50Hz", "120Hz", "400Hz", "800Hz", "4.5kHz");
