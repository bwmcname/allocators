
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

#define BM_FREE_LIST_ALLOCATOR_IMPLEMENTATION
#include "free_list_allocator.h"
#undef BM_FREE_LIST_ALLOCATOR_IMPLEMENTATION

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

    size_t numChunks = 1000;
    checked_fixed_allocator<allocator_interface> allocator(parentAllocator, numChunks, sizeof(thingy), alignof(thingy));

    std::vector<void *> entries;
    for (size_t i = 0; i < numChunks; ++i)
    {
        entries.push_back(allocator.ALLOC_ONE());
    }

    for (size_t i = 0; i < numChunks; ++i)
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
    // unsigned seed = (unsigned)__rdtsc();
    unsigned seed = 1744071468;
    printf("Seed: %u\n", seed);
    srand(seed);
    printf("SlowRandomAllocTest\n");

    std::map<void *, size_t> allocations;
    std::unordered_map<void *, char *> allocatedStrings;

    int actionCount = 10000;
    for (int i = 0; i < actionCount; ++i)
    {        
        int val = rand() % 10;

        if (val < 8)
        {
            size_t size = (rand() % Megabytes(512)) + 1; // atleast one byte
            void *ptr = allocator->ALLOC(size, 1);
            memset(ptr, 0xFA, size);

            allocations[ptr] = size;
            allocator->DetectCorruption();
        }
        else if (val < 9)
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
            if (ptr)
            {
                it->second = newSize;
                memset((uint8_t *)ptr + oldSize, 0xFA, newSize - oldSize);
            }

            allocator->DetectCorruption();
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

            size_t size = allocations[it->first];
            uint8_t *allocation = (uint8_t *)it->first;
            
            for (size_t byte = 0; byte < size; ++byte)
            {
                BM_ASSERT(allocation[byte] == 0xFA, "Contents of allocation altered");
            }
                        
            allocator->FREE(it->first);
            allocator->DetectCorruption();
            allocations.erase(it);
        }
    }

    printf("SUCCESS\n");
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
    
    free_list_allocator<win32_virtual_memory_interface> allocator(&mem, Gigabytes(2));
    SlowRandomAllocTests(&allocator);

    // FixedAllocatorTests(&allocator);

    fclose(testLog);
    testLog = nullptr;
    return 0;
}
