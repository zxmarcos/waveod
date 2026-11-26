/*
 * Voks Wah (WAH): the original's Voks Wah (QME-200 V1.2.2, library entry 2.0, id 0x05000001) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The pedal wah of wah.h with its constants: the band
 * pass at 0.06269 + 0.2622 p rad, Q term 9 - 7.2 p, x 7.499; dry x 0.34.
 */
#include "wah.h"

static const struct wah_model MODEL = {
    { 0.9998373985290527f, -1.9996747970581055f, 0.9998373985290527f,
      1.0180342197418213f, -1.999349594116211f, 0.9819657802581787f },
    0.06268937885761261f, 0.26215559244155884f, -7.199999809265137f, 7.498899936676025f, 0.3400000035762787f,
};

WAH_BLOCK(wah_voks_block, 0x100, "Voks Wah", MODEL);
