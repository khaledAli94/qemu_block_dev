#include <string.h>
#include <malloc.h>


char *strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}
size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

char *strdup(const char *src) {
    size_t len = strlen(src);
    char *dest = malloc(len + 1); // strlen doesn't count \0 terminator
    if (dest) {
        for (size_t i = 0; i <= len; i++) dest[i] = src[i];
    }
    return dest;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}