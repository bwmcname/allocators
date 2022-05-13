
#pragma once

#include "platform.h"
#include "allocator_interface.h"
#include <stdint.h>

template <typename T>
struct allocator_spin_lock
{
    allocator_spin_lock(T *allocator);

    uint32_t m_lock;
    T *m_allocator;
    DECLARE_ALLOCATOR_INTERFACE_METHODS();
    void Lock();
    void Unlock();
};

template <typename T>
allocator_spin_lock<T>::allocator_spin_lock(T *allocator)
    : m_lock(0),
      m_allocator(allocator)
{}

template <typename T>
inline void allocator_spin_lock<T>::Lock()
{
    while(ICE(&m_lock, 1, 0) != 0);
}

template <typename T>
inline void allocator_spin_lock<T>::Unlock()
{
    m_lock = 0;
}

template <typename T>
void *allocator_spin_lock<T>::AllocInternal(size_t size, uint32_t alignment, int line, const char *file)
{
    Lock();
    void *result = m_allocator->AllocInternal(size, alignment, line, file);
    Unlock();

    return result;
}

template <typename T>
void allocator_spin_lock<T>::FreeInternal(void *addr, int line, const char *file)
{
    Lock();
    m_allocator->FreeInternal(addr, line, file);
    Unlock();
}

template <typename T>
void *allocator_spin_lock<T>::ReAllocInternal(void *addr, size_t size, int line, const char *file)
{
    Lock();
    void *result = m_allocator->ReAllocInternal(addr, size, line, file);
    Unlock();
    return result;
}
