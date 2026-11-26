/*
 * Skreamer (DRIVE › OD): the original's Skreamer (QME-200 V1.2.2, library entry 3.0, id 0x03000000) in C, its output
 * bit for bit (tools/check_stock.py; docs/stock_port.md). The path is in skreamer.h.
 */
#include "skreamer.h"

SKREAMER_BLOCK(skreamer_block, "Skreamer", 0x100, 40.0f, 70.0f);
