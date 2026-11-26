/*
 * Skreamer 9 (DRIVE › OD): the original's Skreamer 9 (QME-200 V1.2.2, library entry 3.1, id 0x03000001) in C, its
 * output bit for bit (tools/check_stock.py; docs/stock_port.md). The original's module has the Skreamer's code
 * instruction for instruction; the path is in skreamer.h.
 */
#include "skreamer.h"

SKREAMER_BLOCK(skreamer9_block, "Skreamer 9", 0x101, 40.0f, 70.0f);
