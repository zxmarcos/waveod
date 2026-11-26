/*
 * LZO1X, the compression of the editor protocol's message payloads (the original uses miniLZO:
 * FUN_0001F1F4 decompress, FUN_0001F148 compress).
 */
#pragma once

#include <stdint.h>

/* Decompress `n` bytes into dst (room for *out_len bytes); *out_len = bytes produced. 0 = ok, -1 = the input
   is damaged or does not fit. */
int lzo1x_decompress(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t *out_len);

/* A valid LZO1X stream holding `n` bytes as literal runs (n + n / 255 + 21 bytes at most); its length. */
uint32_t lzo1x_store(const uint8_t *src, uint32_t n, uint8_t *dst);
