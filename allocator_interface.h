
#pragma once
#pragma warning(push, 0)
#include <stdint.h>
#pragma warning(pop)


#if 0
void *OnAlloc(void *newMem, size_t size, uint32_t alignment, int line, const char *file)
{
    // Do something to track the allocation
    // return the new allocation when done.
}

void *OnReAlloc(void *newAlloc, void *oldAlloc, size_t size, int line, const char *file)
{
    // Do something to track the allocation
    // return the new allocation when done
}

void OnFree(void *freed, int line, const char *file)
{
    // Do something to track the free.
}
#endif

#if defined(TRACKALLOC) || defined(TRACKREALLOC) || defined(TRACKFREE)
#define DECLARE_ALLOCATOR_INTERFACE_METHODS()                                         \
    __pragma(warning(push, 0))                                          \
    void *AllocInternal(size_t size, uint32_t alignment, int line, const char *file); \
    void FreeInternal(void *addr, int line, const char *file);                        \
    void *ReAllocInternal(void *addr, size_t size, int line, const char *file);       \
    inline void *TrackedAllocInternal(size_t size, uint32_t alignment, int line, const char *file) { return TRACKALLOC(AllocInternal(size, alignment, line, file), size, alignment, line, file); } \
    inline void TrackedFreeInternal(void *addr, int line, const char *file) { FreeInternal(addr, line, file); TRACKFREE(addr, line, file); } \
    inline void *TrackedReAllocInternal(void *addr, size_t size, int line, const char *file) { return TRACKREALLOC(ReAllocInternal(addr, size, line, file), addr, size, line, file); } \
    __pragma(warning(pop))
#define ALLOC(size, alignment) TrackedAllocInternal(size, alignment, __LINE__, __FILE__)
#define FREE(addr) TrackedFreeInternal(addr, __LINE__, __FILE__)
#define REALLOC(addr, size) TrackedReAllocInternal(addr, size, __LINE__, __FILE__)
#else
#define DECLARE_ALLOCATOR_INTERFACE_METHODS()                                         \
    void *AllocInternal(size_t size, uint32_t alignment, int line, const char *file); \
    void FreeInternal(void *addr, int line, const char *file);                        \
    void *ReAllocInternal(void *addr, size_t size, int line, const char *file)

#define ALLOC(size, alignment) AllocInternal(size, alignment, __LINE__, __FILE__)
#define FREE(addr) FreeInternal(addr, __LINE__, __FILE__)
#define REALLOC(addr, size) ReAllocInternal(addr, size, __LINE__, __FILE__)
#endif
