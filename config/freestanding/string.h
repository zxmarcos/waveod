/* <string.h> of the freestanding firmware: the functions implemented in src/board/qme200/libc.c. */
#pragma once

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strncpy(char *dst, const char *src, size_t n);
