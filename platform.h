
#pragma once

#ifdef _WIN32
#pragma warning(push, 0)
#include <windows.h>
#pragma warning(pop)
#define ICE(dest, exc, comp) (InterlockedCompareExchange(dest, exc, comp))
#endif

#ifdef __clang__
#define ICE(dest, exc, comp) (__sync_val_compare_and_swap(dest, exc, comp))
#endif

#ifndef ICE
#error "Platform does not define the InterlockedCompareExchange macro (ICE)."
#endif
