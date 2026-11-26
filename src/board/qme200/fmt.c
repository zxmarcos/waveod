/* Text for the diagnostics without a printf: append to a bounded buffer. */
#include "board/qme200/board.h"

void fmt_init(struct fmt *f, char *buf, unsigned size)
{
    f->buf = buf;
    f->size = size;
    f->len = 0;
    if (size)
        buf[0] = 0;
}

void fmt_str(struct fmt *f, const char *s)
{
    while (*s && f->len + 1 < f->size)
        f->buf[f->len++] = *s++;
    if (f->size)
        f->buf[f->len] = 0;
}

void fmt_dec(struct fmt *f, uint32_t v)
{
    char tmp[11];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    char out[12];
    int k = 0;
    while (n)
        out[k++] = tmp[--n];
    out[k] = 0;
    fmt_str(f, out);
}

void fmt_hex(struct fmt *f, uint32_t v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char out[9];
    if (digits > 8)
        digits = 8;
    for (int i = 0; i < digits; i++)
        out[i] = hex[(v >> (4 * (digits - 1 - i))) & 15];
    out[digits] = 0;
    fmt_str(f, out);
}
