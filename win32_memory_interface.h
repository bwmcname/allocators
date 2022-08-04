
#pragma once

#include "memory_interface.h"

#pragma warning(push, 0)
#include <Windows.h>
#pragma warning(pop)

struct win32_virtual_memory_interface
{
	win32_virtual_memory_interface(const win32_virtual_memory_interface &) = delete;
	win32_virtual_memory_interface();

	DWORD page_size;

    DECLARE_MEMORY_INTERFACE_METHODS();
};

#if defined(BM_WIN32_MEMORY_INTERFACE_IMPLEMENTATION)

#pragma warning(push, 0)
#include <stdint.h>
#pragma warning(pop)

#if !defined(BM_ASSERT)
#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)
#define BM_ASSERT(val, msg) assert(val)
#endif

win32_virtual_memory_interface::win32_virtual_memory_interface()
{
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);

	page_size = system_info.dwPageSize;
}

void win32_virtual_memory_interface::Commit(void *addr, size_t size, size_t *actual_commit)
{
	size_t pages_committed = size / page_size + ((size % page_size) ? 1 : 0);
	*actual_commit = pages_committed * page_size;

	void *result = VirtualAlloc(addr, *actual_commit, MEM_COMMIT, PAGE_READWRITE);
    BM_ASSERT(result, "Failed to commit memory");
}

void *win32_virtual_memory_interface::Reserve(size_t size, size_t *actual)
{
	// reserve atleast as many pages we need to satisfy to_reserve bytes.
	size_t available_pages = (size_t)(size / page_size + ((size % page_size) ? 1 : 0));
	*actual = available_pages * page_size;
	void *page_base = VirtualAlloc(nullptr, *actual, MEM_RESERVE, PAGE_READWRITE);
	BM_ASSERT(page_base, "Failed to reserve memory");
	return page_base;
}

void win32_virtual_memory_interface::DeCommit(void *addr, size_t size)
{
	BM_ASSERT(VirtualFree(addr, size, MEM_DECOMMIT), "Failed to de-commit memory");
}

void win32_virtual_memory_interface::Release(void *addr)
{
	BM_ASSERT(VirtualFree(addr, 0, MEM_RELEASE), "Failed to release memory");
}

size_t win32_virtual_memory_interface::GetPageSize()
{
    return page_size;
}

#endif
