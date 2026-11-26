/*
 * Cry Wah (WAH): the original's Cry Wah (QME-200 V1.2.2, library entry 2.1, id 0x05000008) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The pedal wah of wah.h with its constants: the band
 * pass at 0.05984 + 0.2394 p rad, Q term 9 - 7.11 p, x 7.08; dry x 0.375.
 */
#include "wah.h"

static const struct wah_model MODEL = {
    { 0.9998167753219604f, -1.999633550643921f, 0.9998167753219604f,
      1.0191422700881958f, -1.9992672204971313f, 0.980857789516449f },
    0.05983985960483551f, 0.23935943841934204f, -7.110000133514404f, 7.079500198364258f, 0.375f,
};

WAH_BLOCK(wah_cry_block, 0x101, "Cry Wah", MODEL);
