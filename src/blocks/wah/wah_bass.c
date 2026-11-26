/*
 * Bass Wah (WAH): the original's Bass Wah (QME-200 V1.2.2, library entry 2.3, id 0x05000007) in C, its output bit
 * for bit (tools/check_stock.py; docs/stock_port.md). The pedal wah of wah.h with its constants: the band
 * pass at 0.0285 + 0.1852 p rad, Q term 9 - 7.47 p, x 7.943; dry x 0.36.
 */
#include "wah.h"

static const struct wah_model MODEL = {
    { 0.9999675154685974f, -1.9999350309371948f, 0.9999675154685974f,
      1.0080606937408447f, -1.9998700618743896f, 0.9919393062591553f },
    0.028495172038674355f, 0.18521861732006073f, -7.46999979019165f, 7.943299770355225f, 0.36000001430511475f,
};

WAH_BLOCK(wah_bass_block, 0x103, "Bass Wah", MODEL);
