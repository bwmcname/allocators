
#pragma once
#pragma warning(push, 0)
#include <stdint.h>
#pragma warning(pop)

#define DECLARE_ALLOCATOR_INTERFACE_METHODS()                                         \
    void *AllocInternal(size_t size, uint32_t alignment, int line, const char *file); \
    void FreeInternal(void *addr, int line, const char *file);                        \
    void *ReAllocInternal(void *addr, size_t size, int line, const char *file)

#define ALLOC(size, alignment) AllocInternal(size, alignment, __LINE__, __FILE__)
#define FREE(addr) FreeInternal(addr, __LINE__, __FILE__)
#define REALLOC(addr, size) ReAllocInternal(addr, size, __LINE__, __FILE__)
