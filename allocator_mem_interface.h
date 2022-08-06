#pragma once

#include "memory_interface.h"
#include "allocator_interface.h"

template <typename T>
struct allocator_mem_interface
{
    allocator_mem_interface(T *allocator, uint32_t minAlignment);

    uint32_t m_minAlignment; 
    T *m_allocator;
    DECLARE_MEMORY_INTERFACE_METHODS();
    DECLARE_ALLOCATOR_INTERFACE_METHODS();
};

template <typename T>
allocator_mem_interface<T>::allocator_mem_interface(T *allocator, uint32_t minAlignment)
    : m_minAlignment(minAlignment),
      m_allocator(allocator)
{
}

template <typename T>
void allocator_mem_interface<T>::Commit(void *addr, size_t size, size_t *actual)
{
    // Nothing to do.
}

template <typename T>
void *allocator_mem_interface<T>::Reserve(size_t size, size_t *actual)
{
    *actual = size;
    return m_allocator.AllocInternal(size, m_minAlignment, __LINE__, __FILE__);
}

template <typename T>
void allocator_mem_interface<T>::DeCommit(void *addr, size_t size)
{
    // Nothing to do
}

template <typename T>
void allocator_mem_interface<T>::Release(void *addr)
{
    m_allocator.FreeInternal(addr, __LINE__, __FILE__);
}

template <typename T>
size_t allocator_mem_interface<T>::GetPageSize()
{
    return m_minAlignment;
}

template <typename T>
void *allocator_mem_interface<T>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    return m_allocator->AllocInternal(size, alignment, line, file);
}

template <typename T>
void allocator_mem_interface<T>::FreeInternal(void *addr, int line, const char *file)
{
    m_allocator->FreeInternal(addr, line, file);
}

template <typename T>
void *allocator_mem_interface<T>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    return m_allocator->ReAllocInternal(addr, size, line, file);
}
