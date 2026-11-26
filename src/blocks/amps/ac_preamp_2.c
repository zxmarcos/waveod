/*
 * AC Preamp 2 (AMP › Acoustic): the original's AC Preamp 2 (QME-200 V1.2.2, library entry 4.62, id 0x0800007B) in
 * C, its output bit for bit (tools/check_stock.py; docs/stock_port.md). The acoustic preamp of ac_preamp.h with its
 * EQ frequency 0.1202 + 1.7342 f and Q 2.6 - 2 Q, as its Init.
 */
#include "ac_preamp.h"

static const struct ac_preamp_model MODEL = { 0.12024962157011032f, 1.7342160940170288f, -2.0f, 2.0f };

AC_PREAMP_BLOCK(ac_preamp_2_block, 0x7B, "AC Preamp 2", MODEL);
