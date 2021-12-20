#pragma once
#include "fixed_size_allocator.h"

struct alloc_block
{
    alloc_block *next;
    alloc_block *prev;
    const char *file;
    int line;
};

template <typename allocator_interface>
struct checked_fixed_allocator
{
    fixed_size_allocator<allocator_interface> internal_allocator;
    alloc_block *head;
    uint32_t align_correction;

    checked_fixed_allocator(allocator_interface *memory_provider, size_t chunk_count, size_t chunk_size, uint32_t chunk_alignment);
    checked_fixed_allocator(const checked_fixed_allocator &) = delete;
    checked_fixed_allocator() = delete;

    ~checked_fixed_allocator();

    DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

#if !defined(BM_ASSERT)
#include <assert.h>
#define BM_ASSERT(val) assert(val)
#endif

#if !defined(BM_LEAK_CHECK)

static void DefaultLeakCheck(alloc_block *first)
{
    BM_ASSERT(first == nullptr);
}

#define BM_LEAK_CHECK DefaultLeakCheck
#else
// need to declare it first
void BM_LEAK_CHECK (alloc_block *first);
#endif

template <typename allocator_interface>
checked_fixed_allocator<allocator_interface>::checked_fixed_allocator(
    allocator_interface *memory_provider,
    size_t chunk_count,
    size_t chunk_size,
    uint32_t chunk_alignment)
    : align_correction(sizeof(alloc_block) + (sizeof(alloc_block) % chunk_alignment)),
      internal_allocator(memory_provider, chunk_count, chunk_size + align_correction, alignof(alloc_block) > chunk_alignment ? alignof(alloc_block) : chunk_alignment),
      head(nullptr)
{
}

template <typename allocator_interface>
checked_fixed_allocator<allocator_interface>::~checked_fixed_allocator()
{
    BM_LEAK_CHECK(head);
}

template <typename allocator_interface>
void *checked_fixed_allocator<allocator_interface>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    alloc_block *block = (alloc_block *)internal_allocator.AllocInternal(size, alignment, line, file);
    block->line = line;
    block->file = file;

    if (head)
    {
        block->next = head;
        block->prev = nullptr;
        head->prev = block;
    }
    else
    {
        block->next = nullptr;
        block->prev = nullptr;
    }

    head = block;
    
    return (uint8_t *)block + align_correction;
}

template <typename allocator_interface>
void checked_fixed_allocator<allocator_interface>::FreeInternal(void *addr, int line, const char *file)
{
    alloc_block *block = (alloc_block *)((uint8_t *)addr - align_correction);

    if (block->prev)
    {
        block->prev->next = block->next;
    }

    if (block->next)
    {
        block->next->prev = block->prev;
    }

    if (block == head)
    {
        head = block->next;
    }

    internal_allocator.FreeInternal(block, line, file);
}

template <typename allocator_interface>
void *checked_fixed_allocator<allocator_interface>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    BM_ASSERT(false);
    return internal_allocator.ReAllocInternal(addr, size, line, file);
}
