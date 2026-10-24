#ifndef N3BASE_PLATFORM_TYPES_H
#define N3BASE_PLATFORM_TYPES_H

#pragma once

// The Win32 data types the engine uses throughout (HWND, RECT, DWORD, ...). On Windows these are
// the real ones; elsewhere Compat/Win32Types.h defines them, without any of the OS behind them.

#if defined(_WIN32)
#include <windows.h>
#else
#include "Compat/Win32Types.h"
#endif

#endif // N3BASE_PLATFORM_TYPES_H
