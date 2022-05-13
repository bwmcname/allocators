
#pragma once

#ifdef _WIN32
#include <windows.h>
#define ICE(dest, exc, comp) (InterlockedCompareExchange(dest, exc, comp))
#endif

#ifndef ICE
#error "Platform does not define the InterlockedCompareExchange macro (ICE)."
#endif
