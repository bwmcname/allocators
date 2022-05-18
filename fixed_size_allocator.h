#pragma once
#include "allocator_interface.h"

#pragma warning(push, 0)
#include <stdint.h>
#pragma warning(pop)

struct free_chunk
{
	free_chunk *m_nextFree;
};

struct bucket_header
{
    bucket_header *m_next;
    uint32_t m_chunkCount;
};

template <typename allocator_interface>
struct fixed_size_allocator
{
	fixed_size_allocator(allocator_interface *memoryProvider, uint32_t chunkCount, size_t chunkSize, uint32_t chunkAlignment);
	fixed_size_allocator(const fixed_size_allocator &) = delete;
	fixed_size_allocator() = delete;

	bucket_header *m_base;
	free_chunk *m_nextFree;
	allocator_interface *m_memoryProvider;

    size_t m_chunkSize;
    uint32_t m_chunkCount;
    uint32_t m_chunkAlignment;

	~fixed_size_allocator();

	DECLARE_ALLOCATOR_INTERFACE_METHODS();

private:
    bucket_header *NewBucket();
    free_chunk *InitChunkRange(void *start, uint32_t chunkCount);
    void *FirstChunk(bucket_header *bucket);
    void *GetChunk(bucket_header *bucket, uint32_t i);
    free_chunk *TryReallocBucket(bucket_header *bucket);
};

#define ALLOC_ONE() AllocInternal(0, 0, __LINE__, __FILE__)

#if !defined(BM_ASSERT)
#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)
#define BM_ASSERT(val) assert(val)
#endif

template <typename allocator_interface>
fixed_size_allocator<allocator_interface>::fixed_size_allocator(
	allocator_interface *memoryProvider,
	uint32_t chunkCount,
	size_t chunkSize,
	uint32_t chunkAlignment)
    : m_memoryProvider(memoryProvider),
      m_chunkSize(chunkSize),
      m_chunkCount(chunkCount),
      m_chunkAlignment(chunkAlignment > alignof(bucket_header) ? chunkAlignment : alignof(bucket_header))
{
	m_base = NewBucket();
    m_nextFree = (free_chunk *)FirstChunk(m_base);
}

template <typename allocator_interface>
fixed_size_allocator<allocator_interface>::~fixed_size_allocator()
{
	m_memoryProvider->FREE(m_base);
}

template <typename allocator_interface>
bucket_header *fixed_size_allocator<allocator_interface>::NewBucket()
{
    size_t requiredBytes = sizeof(bucket_header) + ((m_chunkCount) * m_chunkSize);
    bucket_header *bucket = (bucket_header *)m_memoryProvider->ALLOC(requiredBytes, m_chunkAlignment);
    BM_ASSERT(bucket);
    bucket->m_next = nullptr;
    bucket->m_chunkCount = m_chunkCount;

    InitChunkRange(FirstChunk(bucket), m_chunkCount);

    return bucket;
}

template <typename allocator_interface>
free_chunk *fixed_size_allocator<allocator_interface>::InitChunkRange(void *start, uint32_t count)
{
    uint8_t *baseByte = (uint8_t *)start;
    free_chunk *lastChunk = 0;
	uint32_t i = count - 1;
	for (;;)
	{
		free_chunk *chunk = (free_chunk *)(&baseByte[i * m_chunkSize]);
		chunk->m_nextFree = lastChunk;
		lastChunk = chunk;

		if (i-- == 0)
		{
			break;
		}
	}

    return (free_chunk *)baseByte;
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::FirstChunk(bucket_header *bucket)
{
    return (void *)(bucket + 1);
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::GetChunk(bucket_header *bucket, uint32_t i)
{
    uint8_t *first = (uint8_t *)FirstChunk(bucket);
    return (void *)(first + (m_chunkSize * i));
}

template <typename allocator_interface>
free_chunk *fixed_size_allocator<allocator_interface>::TryReallocBucket(bucket_header *header)
{
    uint32_t newChunkCount = header->m_chunkCount + m_chunkCount;
    if (m_memoryProvider->REALLOC(header, sizeof(bucket_header) + (newChunkCount * m_chunkSize)) == nullptr)
    {
        return nullptr;
    }

    free_chunk *first = InitChunkRange(GetChunk(header, header->m_chunkCount), m_chunkCount);
    header->m_chunkCount = newChunkCount;

    return first;
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    (void)size;
    (void)line;
    (void)file;
    (void)alignment;

	if (m_nextFree == nullptr)
	{
        // First, try to realloc the mem from an existing bucket
        for (bucket_header *bucket = m_base; bucket != nullptr; bucket = bucket->m_next)
        {
            free_chunk *newChunk = TryReallocBucket(bucket);
            if (newChunk)
            {
                m_nextFree = newChunk->m_nextFree;
                return (void *)newChunk;
            }
        }

        // No memory could be reallocated, add a new bucket
        bucket_header *newBucket = NewBucket();
        newBucket->m_next = m_base;
        m_base = newBucket;
        free_chunk *first = (free_chunk *)FirstChunk(newBucket);
        m_nextFree = first->m_nextFree;
        return first;
	}

	free_chunk *chunk = m_nextFree;
	m_nextFree = chunk->m_nextFree;
	return (void *)chunk;
}

template <typename allocator_interface>
void fixed_size_allocator<allocator_interface>::FreeInternal(void *addr, int line, const char *file)
{
    (void)line;
    (void)file;
    
	free_chunk *chunk = (free_chunk *)addr;
	chunk->m_nextFree = m_nextFree;
	m_nextFree = chunk;
}

template <typename allocator_interface>
void *fixed_size_allocator<allocator_interface>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
	// Maybe there is something clever we can do here... dunno.
    BM_ASSERT(false);
	return nullptr;
}
