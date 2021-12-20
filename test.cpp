
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <winternl.h>

#define BM_WIN32_MEMORY_INTERFACE_IMPLEMENTATION
#include "win32_memory_interface.h"
#undef BM_WIN32_MEMORY_INTERFACE_IMPLEMENTATION

#define BM_FIXED_SIZE_ALLOCATOR_IMPLEMENTATION
#include "fixed_size_allocator.h"
#undef BM_FIXED_SIZE_ALLOCATOR_IMPLEMENTATION

#define BM_LEAK_CHECK CheckForLeaks
#define BM_CHECKED_FIXED_ALLOCATOR_IMPLEMENTATION
#include "checked_fixed_allocator.h"
#undef BM_CHECKED_FIXED_ALLOCATOR_IMPLEMENTATION

void CheckForLeaks(alloc_block *block)
{
    if (block != nullptr)
    {
        printf("Leaks Detected!\n");

        alloc_block *current_block = block;
        do
        {
            printf("%s(%i)\n", current_block->file, current_block->line);
            current_block = current_block->next;
        } while (current_block);
    }
}

struct Mallocator
{
    DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

void *
Mallocator::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    return _aligned_malloc(size, alignment);
}

void
Mallocator::FreeInternal(void *addr, int line, const char *file)
{
    _aligned_free(addr);
}

void *
Mallocator::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    return realloc(addr, size);
}

static constexpr size_t Kilobytes(size_t num)
{
    return num * 1024llu;
}

static constexpr size_t Megabytes(size_t num)
{
    return num * Kilobytes(1024llu);
}

static constexpr size_t Gigabytes(size_t num)
{
    return num * Megabytes(1024llu);
}

template <typename mem_interface>
static void MemoryInterfaceTests(mem_interface *mem)
{
    printf("MemoryInterfaceTests: ");
    size_t desiredReserve = Megabytes(2);
    size_t actualReserve;
    void *basePage = mem->Reserve(desiredReserve, &actualReserve);

    assert(desiredReserve <= actualReserve);

    size_t desiredCommit = Megabytes(1);
    size_t actualCommit;
    mem->Commit(basePage, desiredCommit, &actualCommit);

    assert(desiredCommit <= actualCommit);

    memset(basePage, 0xBB, desiredCommit);

    mem->DeCommit(basePage, actualCommit);
    mem->Release(basePage);

    printf("SUCCESS\n");
}

template <typename allocator_interface>
static void FixedAllocatorTests(allocator_interface *parentAllocator)
{
    printf("FixedAllocatorTests: ");
    
    struct thingy
    {
        int a;
        float b;
        char c;
    };

    int numChunks = 1000;
    checked_fixed_allocator<allocator_interface> allocator(parentAllocator, numChunks, sizeof(thingy), alignof(thingy));

    std::vector<void *> entries;
    for (int i = 0; i < numChunks; ++i)
    {
        entries.push_back(allocator.ALLOC_ONE());
    }

    for (int i = 0; i < numChunks; ++i)
    {
        allocator.FREE(entries[i]);
    }

    printf("SUCCESS\n");
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    typedef ULONG (*RtlNtStatusToDosError_t)(NTSTATUS);
    
    HMODULE nt = LoadLibrary("NTDLL.dll");
    RtlNtStatusToDosError_t RtlNtStatusToDosError = (RtlNtStatusToDosError_t)GetProcAddress(nt, "RtlNtStatusToDosError");

    ULONG errorCode = RtlNtStatusToDosError(exceptionInfo->ExceptionRecord->ExceptionCode);

    LPTSTR result = nullptr;
    
    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&result,
        0,
        NULL);        

    if (result)
    {
        printf ("Unandled Exception: %s", result);
        LocalFree(result);
    }

    FreeLibrary(nt);

    return EXCEPTION_EXECUTE_HANDLER;
}

int main()
{
    SetUnhandledExceptionFilter(CrashHandler);

    win32_virtual_memory_interface mem;
    MemoryInterfaceTests(&mem);

    Mallocator mallocator;
    FixedAllocatorTests(&mallocator);
}
