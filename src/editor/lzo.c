#include "editor/lzo.h"
#include <string.h>
#include "platform/sdram_code.h"           /* main-loop code: SDRAM, not ITCM */

/* lzo1x_decompress_safe's state machine (also tools/htmidi.py): literal runs and matches with 1-, 2- and
   3-byte distances; the end marker is a 16..23 match of distance 0 */
int lzo1x_decompress(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t *out_len)
{
    const uint8_t *ip = src, *end = src + n;
    uint8_t *op = dst, *const out_end = dst + *out_len;
    uint32_t t;
    enum { LOOP, FIRST_LITERAL, MATCH, MATCH_DONE, MATCH_NEXT } st = LOOP;

#define NEED_IN(k)  do { if ((uint32_t)(end - ip) < (uint32_t)(k)) return -1; } while (0)
#define NEED_OUT(k) do { if ((uint32_t)(out_end - op) < (uint32_t)(k)) return -1; } while (0)

    NEED_IN(1);
    if (*ip > 17) {
        t = *ip++ - 17u;
        NEED_IN(t);
        NEED_OUT(t);
        memcpy(op, ip, t);
        op += t;
        ip += t;
        st = t < 4 ? MATCH_NEXT : FIRST_LITERAL;
    }
    for (;;) {
        const uint8_t *m;
        switch (st) {
        case LOOP:
            NEED_IN(1);
            t = *ip++;
            if (t >= 16) {
                st = MATCH;
                break;
            }
            if (t == 0) {
                while (1) {
                    NEED_IN(1);
                    if (*ip)
                        break;
                    t += 255;
                    ip++;
                }
                t += 15u + *ip++;
            }
            NEED_IN(t + 3);
            NEED_OUT(t + 3);
            memcpy(op, ip, t + 3);
            op += t + 3;
            ip += t + 3;
            st = FIRST_LITERAL;
            break;
        case FIRST_LITERAL:
            NEED_IN(1);
            t = *ip++;
            if (t >= 16) {
                st = MATCH;
                break;
            }
            NEED_IN(1);
            m = op - (1 + 0x0800) - (t >> 2) - ((uint32_t)*ip++ << 2);
            if (m < dst)
                return -1;
            NEED_OUT(3);
            for (int k = 0; k < 3; k++)
                *op++ = m[k];
            st = MATCH_DONE;
            break;
        case MATCH:
            if (t >= 64) {
                NEED_IN(1);
                m = op - 1 - ((t >> 2) & 7) - ((uint32_t)*ip++ << 3);
                t = (t >> 5) - 1 + 2;
            } else if (t >= 32) {
                t &= 31;
                if (t == 0) {
                    while (1) {
                        NEED_IN(1);
                        if (*ip)
                            break;
                        t += 255;
                        ip++;
                    }
                    t += 31u + *ip++;
                }
                NEED_IN(2);
                m = op - 1 - ((uint32_t)(ip[0] | ip[1] << 8) >> 2);
                ip += 2;
                t += 2;
            } else if (t >= 16) {
                m = op - ((t & 8) << 11);
                t &= 7;
                if (t == 0) {
                    while (1) {
                        NEED_IN(1);
                        if (*ip)
                            break;
                        t += 255;
                        ip++;
                    }
                    t += 7u + *ip++;
                }
                NEED_IN(2);
                m -= (uint32_t)(ip[0] | ip[1] << 8) >> 2;
                ip += 2;
                if (m == op) {                  /* end of stream */
                    *out_len = (uint32_t)(op - dst);
                    return 0;
                }
                m -= 0x4000;
                t += 2;
            } else {
                NEED_IN(1);
                m = op - 1 - (t >> 2) - ((uint32_t)*ip++ << 2);
                t = 2;
            }
            if (m < dst)
                return -1;
            NEED_OUT(t);
            while (t--)
                *op++ = *m++;
            st = MATCH_DONE;
            break;
        case MATCH_DONE:
            t = ip[-2] & 3u;
            if (t == 0) {
                st = LOOP;
                break;
            }
            NEED_IN(t);
            NEED_OUT(t);
            memcpy(op, ip, t);
            op += t;
            ip += t;
            st = MATCH_NEXT;
            break;
        case MATCH_NEXT:
            NEED_IN(1);
            t = *ip++;
            st = MATCH;
            break;
        }
    }
#undef NEED_IN
#undef NEED_OUT
}

uint32_t lzo1x_store(const uint8_t *src, uint32_t n, uint8_t *dst)
{
    uint8_t *op = dst;
    if (n == 0) {
    } else if (n <= 238) {                  /* first byte > 17: a run of (byte - 17) literals */
        *op++ = (uint8_t)(n + 17);
        memcpy(op, src, n);
        op += n;
    } else {                                /* 0, k zero bytes, r: a run of 18 + 255 k + r literals */
        uint32_t k = (n - 18) / 255, r = (n - 18) % 255;
        if (r == 0) {
            k--;
            r = 255;
        }
        *op++ = 0;
        memset(op, 0, k);
        op += k;
        *op++ = (uint8_t)r;
        memcpy(op, src, n);
        op += n;
    }
    *op++ = 0x11;                           /* end of stream */
    *op++ = 0;
    *op++ = 0;
    return (uint32_t)(op - dst);
}
