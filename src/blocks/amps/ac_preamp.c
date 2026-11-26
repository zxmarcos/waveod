/*
 * AC Preamp (AMP › Acoustic): the original's AC Preamp (QME-200 V1.2.2, library entry 4.61, id 0x0800007A) in C,
 * its output bit for bit (tools/check_stock.py; docs/stock_port.md). The acoustic preamp of ac_preamp.h with its EQ
 * frequency 0.0142 + 0.2080 f and Q 3.6 - 3 Q.
 */
#include "ac_preamp.h"

static const struct ac_preamp_model MODEL = { 0.014247585088014603f, 0.20801474153995514f, -3.0f, 3.0f };

AC_PREAMP_BLOCK(ac_preamp_block, 0x7A, "AC Preamp", MODEL);
