#ifndef STRING_H
#define STRING_H
#include <malloc.h>

char *strcpy(char *dst, const char *src);
size_t strlen(const char *s);
char *strdup(const char *s);

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif
