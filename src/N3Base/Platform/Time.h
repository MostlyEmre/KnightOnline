#ifndef N3BASE_PLATFORM_TIME_H
#define N3BASE_PLATFORM_TIME_H

#pragma once

#include "Types.h"

// Clocks and sleeping. The functions keep their Win32 names and semantics
// (timeGetTime, QueryPerformanceCounter, ...), so the engine's timing behaves the same everywhere.
namespace Platform
{

/// \brief Milliseconds since an arbitrary point (system start on Windows); wraps after ~49.7 days.
DWORD TimeGetTime();

/// \brief Milliseconds since an arbitrary point (system start on Windows), without wrapping.
ULONGLONG GetTickCount64();

/// \brief The high-resolution counter, in ticks of QueryPerformanceFrequency().
BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount);

/// \brief Ticks per second of QueryPerformanceCounter().
BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency);

/// \brief Suspends the calling thread.
void Sleep(DWORD dwMilliseconds);

/// \brief The current local date and time.
void GetLocalTime(SYSTEMTIME* lpSystemTime);

} // namespace Platform

#endif // N3BASE_PLATFORM_TIME_H
