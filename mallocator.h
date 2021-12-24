
#pragma once

struct mallocator
{
    DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

#ifdef BM_CHECKED_FIXED_ALLOCATOR_IMPLEMENTATION
#include <stdlib.h>

void *
mallocator::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    return _aligned_malloc(size, alignment);
}

void
mallocator::FreeInternal(void *addr, int line, const char *file)
{
    _aligned_free(addr);
}

void *
mallocator::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    return realloc(addr, size);
}
#endif
