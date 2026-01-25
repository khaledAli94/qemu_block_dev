#include "cache.h"

// Data Synchronization Barrier
static inline void dsb(void) {
    __asm__ volatile ("dsb sy" ::: "memory");
}

// Data Memory Barrier
static inline void dmb(void) {
    __asm__ volatile ("dmb sy" ::: "memory");
}

// Instruction Synchronization Barrier
static inline void isb(void) {
    __asm__ volatile ("isb sy" ::: "memory");
}

// Clean (flush) D-cache lines covering [addr, addr+size)
void cache_clean(void *addr, size_t size) {
    uintptr_t start = CACHE_ALIGN_DOWN(addr);
    uintptr_t end   = CACHE_ALIGN_UP((uintptr_t)addr + size);

    for (uintptr_t p = start; p < end; p += CACHE_LINE_SIZE) {
        __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(p) : "memory");
    }
    dsb();
}

// Invalidate D-cache lines covering [addr, addr+size)
void cache_invalidate(void *addr, size_t size) {
    uintptr_t start = CACHE_ALIGN_DOWN(addr);
    uintptr_t end   = CACHE_ALIGN_UP((uintptr_t)addr + size);

    for (uintptr_t p = start; p < end; p += CACHE_LINE_SIZE) {
        __asm__ volatile ("mcr p15, 0, %0, c7, c6, 1" :: "r"(p) : "memory");
    }
    dsb();
}

// Clean + Invalidate D-cache lines covering [addr, addr+size)
void cache_clean_invalidate(void *addr, size_t size) {
    uintptr_t start = CACHE_ALIGN_DOWN(addr);
    uintptr_t end   = CACHE_ALIGN_UP((uintptr_t)addr + size);

    for (uintptr_t p = start; p < end; p += CACHE_LINE_SIZE) {
        __asm__ volatile ("mcr p15, 0, %0, c7, c14, 1" :: "r"(p) : "memory");
    }
    dsb();
}

// Clean entire D-cache
void cache_clean_all(void) {
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 0" :: "r"(0) : "memory");
    dsb();
}

// Invalidate entire D-cache
void cache_invalidate_all(void) {
    __asm__ volatile ("mcr p15, 0, %0, c7, c6, 0" :: "r"(0) : "memory");
    dsb();
}

// Clean + Invalidate entire D-cache
void cache_clean_invalidate_all(void) {
    __asm__ volatile ("mcr p15, 0, %0, c7, c14, 0" :: "r"(0) : "memory");
    dsb();
}
