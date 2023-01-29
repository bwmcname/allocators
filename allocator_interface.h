
#pragma once
#include <stdint.h>


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

#ifdef MEM_TRACKING_ENABLED
#ifndef PREALLOC_CALLBACK
#define PREALLOC_CALLBACK(size, alignment, line, file) PreAllocThunk(size, alignment, line, file)
void PreAllocThunk(size_t size, uint32_t alignment, int line, const char *file) {}
#endif
#ifndef POSTALLOC_CALLBACK
#define POSTALLOC_CALLBACK(newMem, size, alignment, line, file) PostAllocThunk(newMem, size, alignment, line, file);
void PostAllocThunk(void *newMem, size_t size, uinit32_t alignment, int line, const char *file) { }
#endif
#ifndef PREREALLOC_CALLBACK
#define PREREALLOC_CALLBACK PreReAllocThunk(oldAlloc, size, line, file)
void PreReAllocThunk(void *oldAlloc, size_t size, int line, const char *file) {}
#endif
#ifndef POSTREALLOC_CALLBACK
#define POSTREALLOC_CALLBACK(newMem, oldMem, size, line, file) PostReAllocThunk(newMem, oldMem, size, line, file)
void PostReAllocThunk(void *newMem, void *oldMem, size_t size, int line, const char *file) { }
#endif
#ifndef PREFREE_CALLBACK
#define PREFREE_CALLBACK(mem, line, file) PreFreeThunk(mem, line, file)
void PreFreeThunk(void *mem, int line, const char *file) {}
#endif
#ifndef POSTFREE_CALLBACK
#define POSTFREE_CALLBACK(mem, line, file) PostFreeThunk(mem, line, file)
void PostFreeThunk(void *mem, int line, const char *file) {}
#endif
#endif

#ifdef MEM_TRACKING_ENABLED
#define DECLARE_ALLOCATOR_INTERFACE_METHODS()                           \
    void *AllocInternal(size_t size, uint32_t alignment, int line, const char *file); \
    void FreeInternal(void *addr, int line, const char *file);          \
    void *ReAllocInternal(void *addr, size_t size, int line, const char *file); \
    inline void *TrackedAllocInternal(size_t size, uint32_t alignment, int line, const char *file) \
    {                                                                   \
        PREALLOC_CALLBACK(size, alignment, line, file);                 \
        void *result = AllocInternal(size, alignment, line, file);  \
        POSTALLOC_CALLBACK(result, size, alignment, line, file);        \
        return result;                                                  \
    }                                                                   \
    inline void TrackedFreeInternal(void *addr, int line, const char *file) \
    {                                                                   \
        PREFREE_CALLBACK(addr, line, file);                             \
        FreeInternal(addr, line, file);                                 \
        POSTFREE_CALLBACK(addr, line, file);                            \
    }                                                                   \
    inline void *TrackedReAllocInternal(void *addr, size_t size, int line, const char *file) \
    {                                                                   \
        PREREALLOC_CALLBACK(addr, size,  line, file);                   \
        void *result = ReAllocInternal(addr, size, line, file);         \
        POSTREALLOC_CALLBACK(result, addr, size, line, file);           \
        return result;                                                  \
    }

#undef DISABLE_ALL_WARNINGS_BEGIN
#undef DISABLE_ALL_WARNINGS_END
    
#define ALLOC(size, alignment) TrackedAllocInternal(size, alignment, __LINE__, __FILE__)
#define FREE(addr) TrackedFreeInternal(addr, __LINE__, __FILE__)
#define REALLOC(addr, size) TrackedReAllocInternal(addr, size, __LINE__, __FILE__)
#else
#define DECLARE_ALLOCATOR_INTERFACE_METHODS()                           \
    void *AllocInternal(size_t size, uint32_t alignment, int line, const char *file); \
    void FreeInternal(void *addr, int line, const char *file);          \
    void *ReAllocInternal(void *addr, size_t size, int line, const char *file)

#define ALLOC(size, alignment) AllocInternal(size, alignment, __LINE__, __FILE__)
#define FREE(addr) FreeInternal(addr, __LINE__, __FILE__)
#define REALLOC(addr, size) ReAllocInternal(addr, size, __LINE__, __FILE__)
#endif
