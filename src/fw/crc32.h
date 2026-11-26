/* CRC-32 (IEEE 802.3, as zlib.crc32): crc32(0, p, n), or continued with the previous result. */
#pragma once

#include <stdint.h>

uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t n);
