#pragma once
#include "allocator_interface.h"
#include <stdint.h>

struct free_chunk
{
	free_chunk *next_free;
};

template <typename allocator_interface>
struct fixed_size_allocator
{
	fixed_size_allocator(allocator_interface *memory_provider, size_t size, size_t chunk_size, uint32_t chunk_alignment);
	fixed_size_allocator(const fixed_size_allocator &) = delete;
	fixed_size_allocator() = delete;

	void *base;
	void *free;
	free_chunk *next_free;
	allocator_interface *memory_provider;

	~fixed_size_allocator();

	DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

#define ALLOC_ONE() AllocInternal(0, 0, __LINE__, __FILE__)

#if !defined(BM_ASSERT)
#include <assert.h>
#define BM_ASSERT(val) assert(val)
#endif

template <typename allocator_interface>
fixed_size_allocator<allocator_interface>::fixed_size_allocator(
	allocator_interface *memory_provider,
	size_t chunk_count,
	size_t chunk_size,
	uint32_t chunk_alignment)
    : memory_provider(memory_provider)
{
    size_t required_bytes = chunk_count * chunk_size;
	base = memory_provider->ALLOC(required_bytes, chunk_alignment);
	BM_ASSERT(base);

	uint8_t *base_byte = (uint8_t *)base;
	free_chunk *last_chunk = 0;

	size_t i = chunk_count - 1;
	for (;;)
	{
		free_chunk *chunk = (free_chunk *)(&base_byte[i * chunk_size]);
		chunk->next_free = last_chunk;
		last_chunk = chunk;

		if (i-- == 0)
		{
			break;
		}
	}

    next_free = last_chunk;
}

template <typename allocator_interface>
fixed_size_allocator<allocator_interface>::~fixed_size_allocator()
{
	memory_provider->FREE(base);
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{    
	if (next_free == nullptr)
	{
		return nullptr;
	}

	free_chunk *chunk = next_free;
	next_free = chunk->next_free;
	return (void *)chunk;
}

template <typename allocator_interface>
void fixed_size_allocator<allocator_interface>::FreeInternal(void *addr, int line, const char *file)
{
	free_chunk *chunk = (free_chunk *)addr;
	chunk->next_free = next_free;
	next_free = chunk;
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
	// Maybe there is something clever we can do here... dunno.
    BM_ASSERT(false);
	return nullptr;
}
