
#pragma warning(push, 0)
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <winternl.h>
#include <conio.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <unordered_map>
#include <map>
#include <unordered_set>
#pragma warning(pop)

#define CORRUPTION_DETECTION_ENABLED 1
size_t leak_checker = 0;

void *OnAlloc(void *newAlloc, size_t size, uint32_t alignment, int line, const char *file)
{
    (void)size;
    (void)alignment;
    (void)line;
    (void)file;

    leak_checker ^= (size_t)newAlloc;
    return newAlloc;
}

void *OnReAlloc(void *newAlloc, void *oldAlloc, size_t size, int line, const char *file)
{
    (void)newAlloc;
    (void)oldAlloc;
    (void)size;
    (void)line;
    (void)file;
    leak_checker ^= ((size_t)newAlloc);
    leak_checker ^= !!(size_t)newAlloc * ((size_t)oldAlloc);
    return newAlloc;
}

void OnFree(void *freed, int line, const char *file)
{
    (void)freed;
    (void)line;
    (void)file;
    leak_checker ^= (size_t)freed;
}

#define TRACKALLOC(mem, size, alignment, line, file) OnAlloc(mem, size, alignment, line, file)
#define TRACKFREE(mem, line, file) OnFree(mem, line, file)
#define TRACKREALLOC(newMem, oldMem, size, line, file) OnReAlloc(newMem, oldMem, size, line, file)

FILE *testLog = nullptr;
void MyAssert(bool value, const char *message, int line, const char *file)
{
    // if (!value && testLog) fclose(testLog);
    if (!value)
    {
#if _WIN32
        const char Seperator = '\\';
#elif
        const char Seperator = '/';
#endif

        const char *lastSeperator = nullptr;
        for (const char *c = file; *c; ++c)
        {
            if (*c == Seperator)
            {
                lastSeperator = c;
            }
        }

        const char *begin;
        if (lastSeperator)
        {
            begin = lastSeperator + 1;
        }
        else
        {
            begin = file;
        }

        printf("Assertion Failed (%s, %i): %s\n", begin, line, message);
        *((char *)nullptr) = 'x';
    }
}

#define USE_STL
#define BM_ASSERT(val, msg) MyAssert((bool)(val), msg, __LINE__, __FILE__)

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

#define BM_MALLOCATOR_IMPLEMENTATION
#include "mallocator.h"
#undef BM_MALLOCATOR_IMPLEMENTATION

#define BM_BEST_FIT_ALLOCATOR_IMPLEMENTATION
#include "best_fit_allocator.h"
#undef BM_BEST_FIT_ALLOCATOR_IMPLEMENTATION

#include "allocator_spinlock.h"
#include "allocator_mem_interface.h"

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

    uint32_t numChunks = 1000;
    checked_fixed_allocator<allocator_interface> allocator(parentAllocator, numChunks, sizeof(thingy), alignof(thingy));

    size_t numAllocations = numChunks * 10;

    std::vector<void *> entries;
    for (size_t i = 0; i < numAllocations; ++i)
    {
        entries.push_back(allocator.ALLOC(sizeof(thingy), alignof(thingy)));
    }

    for (size_t i = 0; i < numAllocations; ++i)
    {
        allocator.FREE(entries[i]);
    }
    
    printf("SUCCESS\n");
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *exceptionInfo)
{
    typedef ULONG (*RtlNtStatusToDosError_t)(NTSTATUS);
    
    HMODULE nt = LoadLibrary("NTDLL.dll");

#pragma warning(suppress: 4191)
    RtlNtStatusToDosError_t RtlNtStatusToDosError = (RtlNtStatusToDosError_t)GetProcAddress(nt, "RtlNtStatusToDosError");

    ULONG errorCode = RtlNtStatusToDosError((NTSTATUS)exceptionInfo->ExceptionRecord->ExceptionCode);

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
        printf ("Unhandled Exception: %s", result);
        LocalFree(result);
    }

    FreeLibrary(nt);

    if (testLog != nullptr)
    {
        fclose(testLog);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void PrintAlignment(void *addr)
{
    printf("Align %zu: %zu\n", (size_t)addr, GetAlignment(addr));
}

template <typename allocator_t>
void SlowRandomAllocTests(allocator_t *allocator)
{
    unsigned seed = (unsigned)__rdtsc();
    printf("Seed: %u\n", seed);
    srand(seed);
    printf("SlowRandomAllocTest\n");

    std::map<void *, size_t> allocations;

    uint64_t maxTimedOp = 0;
    int timedOpIndex = -1;

    uint64_t begin = __rdtsc();
    int actionCount = 10000;
    for (int i = 0; i < actionCount; ++i)
    {        
        int val = rand() % 10;

        uint64_t timedOpBegin = __rdtsc();
        if (val < 6)
        {
            size_t size = (rand() % Megabytes(512)) + 1; // atleast one byte
            void *ptr = allocator->ALLOC(size, 1);
            memset(ptr, 0xFA, size);

            allocations[ptr] = size;
        }
        else if (val < 7)
        {
            size_t numAllocations = allocations.size();
            if (numAllocations == 0)
            {
                continue;
            }
            auto it = allocations.begin();

            int element = (int)(rand() % numAllocations);
            std::advance(it, element);

            size_t oldSize = it->second;
            size_t newSize = it->second + (rand() % Megabytes(10));
            void *ptr = allocator->REALLOC(it->first, newSize);
            (void)ptr;
            (void)oldSize;

            if (ptr)
            {
                allocations.erase(it);
                allocations[ptr] = newSize;
            }

#if CORRUPTION_DETECTION_ENABLED
            if (ptr)
            {
                it->second = newSize;
                memset((uint8_t *)ptr + oldSize, 0xFA, newSize - oldSize);
            }
#endif
        }
        else
        {
            size_t numAllocations = allocations.size();
            if (numAllocations == 0)
            {
                continue;
            }

            int element = (int)(rand() % numAllocations);

            auto it = allocations.begin();
            std::advance(it, element);

#if CORRUPTION_DETECTION_ENABLED
            size_t size = allocations[it->first];
            uint8_t *allocation = (uint8_t *)it->first;

            for (size_t byte = 0; byte < size; ++byte)
            {
                BM_ASSERT(allocation[byte] == 0xFA, "Contents of allocation altered");
            }
#endif
                        
            allocator->FREE(it->first);
            allocations.erase(it);
        }

        uint64_t timedOpElapsed = __rdtsc() - timedOpBegin;
        if (timedOpElapsed > maxTimedOp)
        {
            maxTimedOp = timedOpElapsed;
            timedOpIndex = i;
        }
    }

    for (auto it = allocations.begin(); it != allocations.end(); ++it)
    {
        allocator->FREE(it->first);
    }

    uint64_t total = __rdtsc() - begin;
    printf("SUCCESS [Elapsed=%llu]\n", total);
    printf("Max Op [Elapsed=%llu, Index=%i]\n", maxTimedOp, timedOpIndex);
}

int main()
{
#pragma warning(suppress: 5039)
    SetUnhandledExceptionFilter(CrashHandler);
#pragma warning(suppress: 4996)
    // testLog = fopen("test_log.txt", "w");
    testLog = stdout;

    win32_virtual_memory_interface mem;
    // MemoryInterfaceTests(&mem);

    // Reserve 8 gigabytes
    best_fit_allocator<win32_virtual_memory_interface> bestFit(&mem, Gigabytes(8));
    allocator_spin_lock<best_fit_allocator<win32_virtual_memory_interface>> lockedAlloc(&bestFit);
    allocator_mem_interface<allocator_spin_lock<best_fit_allocator<win32_virtual_memory_interface>>> finalAlloc(&lockedAlloc, alignof(max_align_t));
    SlowRandomAllocTests(&finalAlloc);

    FixedAllocatorTests(&finalAlloc);

    fclose(testLog);
    testLog = nullptr;

    assert(leak_checker == 0);

    return 0;
}
