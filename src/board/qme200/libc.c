/* The few C library functions the freestanding firmware needs (LVGL brings its own allocator and printf). */
#include <stddef.h>
#include <stdint.h>

/* keeps the compiler from turning these loops back into calls to themselves */
#define NO_BUILTIN __attribute__((no_builtin))

NO_BUILTIN void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
        for (; n >= 16; n -= 16, d += 16, s += 16) {
            uint32_t a = ((const uint32_t *)s)[0], b = ((const uint32_t *)s)[1];
            uint32_t c = ((const uint32_t *)s)[2], e = ((const uint32_t *)s)[3];
            ((uint32_t *)d)[0] = a;
            ((uint32_t *)d)[1] = b;
            ((uint32_t *)d)[2] = c;
            ((uint32_t *)d)[3] = e;
        }
        for (; n >= 4; n -= 4, d += 4, s += 4)
            *(uint32_t *)d = *(const uint32_t *)s;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

NO_BUILTIN void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n];
    return dst;
}

NO_BUILTIN void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    if (((uintptr_t)d & 3) == 0) {
        uint32_t w = (uint8_t)c * 0x01010101u;
        for (; n >= 16; n -= 16, d += 16) {
            ((uint32_t *)d)[0] = w;
            ((uint32_t *)d)[1] = w;
            ((uint32_t *)d)[2] = w;
            ((uint32_t *)d)[3] = w;
        }
        for (; n >= 4; n -= 4, d += 4)
            *(uint32_t *)d = w;
    }
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

NO_BUILTIN int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

/* the run-time ABI's names for them, which the compiler calls for copies and clears (note memset's argument order) */
#define AEABI(name, ...) void name(__VA_ARGS__)
AEABI(__aeabi_memcpy, void *d, const void *s, size_t n) { memcpy(d, s, n); }
AEABI(__aeabi_memcpy4, void *d, const void *s, size_t n) { memcpy(d, s, n); }
AEABI(__aeabi_memcpy8, void *d, const void *s, size_t n) { memcpy(d, s, n); }
AEABI(__aeabi_memmove, void *d, const void *s, size_t n) { memmove(d, s, n); }
AEABI(__aeabi_memmove4, void *d, const void *s, size_t n) { memmove(d, s, n); }
AEABI(__aeabi_memmove8, void *d, const void *s, size_t n) { memmove(d, s, n); }
AEABI(__aeabi_memset, void *d, size_t n, int c) { memset(d, c, n); }
AEABI(__aeabi_memset4, void *d, size_t n, int c) { memset(d, c, n); }
AEABI(__aeabi_memset8, void *d, size_t n, int c) { memset(d, c, n); }
AEABI(__aeabi_memclr, void *d, size_t n) { memset(d, 0, n); }
AEABI(__aeabi_memclr4, void *d, size_t n) { memset(d, 0, n); }
AEABI(__aeabi_memclr8, void *d, size_t n) { memset(d, 0, n); }

NO_BUILTIN size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

NO_BUILTIN int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

NO_BUILTIN char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}
