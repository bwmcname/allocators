
#pragma once

#define DECLARE_MEMORY_INTERFACE_METHODS()                \
    void Commit(void *addr, size_t size, size_t *actual); \
	void *Reserve(size_t size, size_t *actual);           \
	void DeCommit(void *addr, size_t size);               \
	void Release(void *reserve_addr)
