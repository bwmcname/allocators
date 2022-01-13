
#include "best_fit_allocator.h"

struct dummy_interface
{
    DECLARE_MEMORY_INTERFACE_METHODS();
};

void dummy_interface::Commit(void *addr, size_t size, size_t *actual)
{
    (void)addr;
    (void)size;
    (void)actual;
}

void *dummy_interface::Reserve(size_t size, size_t *actual)
{
    (void)size;
    (void)actual;
    return nullptr;
}

void dummy_interface::DeCommit(void *addr, size_t size)
{
    (void)addr;
    (void)size;
}

void dummy_interface::Release(void *reserve_addr)
{
    (void)reserve_addr;
}

size_t dummy_interface::GetPageSize()
{
    return 0;
}

int main()
{
    dummy_interface memory_interface;
    best_fit_allocator<dummy_interface> allocator(&memory_interface, 0);

    void *ptr = allocator.ALLOC(12341234, 8);
    allocator.REALLOC(ptr, 12341235);
    allocator.FREE(ptr);
}
