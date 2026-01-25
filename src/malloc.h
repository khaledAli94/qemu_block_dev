#ifndef __MALLOC_H__
#define __MALLOC_H__

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

// typedef unsigned int size_t;
// #define NULL ((void*)0)

extern void *memset(void *dest, int value, size_t len);
extern void *memcpy(void *dest, const void *src, unsigned len);
// extern int strcmp(const char *s1, const char *s2);
// extern int strncmp(const char *s1, const char *s2, size_t len);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);


void malloc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MALLOC_H__ */
