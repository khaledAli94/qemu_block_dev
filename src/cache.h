#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>

// Cache line size for Cortex-A7
#define CACHE_LINE_SIZE 32

// Align helpers
#define CACHE_ALIGN_DOWN(addr) ((uintptr_t)(addr) & ~(CACHE_LINE_SIZE - 1))
#define CACHE_ALIGN_UP(addr)   (((uintptr_t)(addr) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))

// Core operations
void cache_clean(void *addr, size_t size);
void cache_invalidate(void *addr, size_t size);
void cache_clean_invalidate(void *addr, size_t size);

// Whole-cache operations (rarely needed)
void cache_clean_all(void);
void cache_invalidate_all(void);
void cache_clean_invalidate_all(void);


#endif
