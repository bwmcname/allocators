
#pragma once

struct mallocator
{
    DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

#ifdef BM_MALLOCATOR_IMPLEMENTATION

#pragma warning(push, 0)
#include <stdlib.h>
#pragma warning(pop)

void *
mallocator::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    (void)file;
    (void)line;
    return _aligned_malloc(size, alignment);
}

void
mallocator::FreeInternal(void *addr, int line, const char *file)
{
    (void)file;
    (void)line;
    _aligned_free(addr);
}

void *
mallocator::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    (void)file;
    (void)line;
    return realloc(addr, size);
}
#endif
