#include "../Time.h"

#include <chrono>
#include <ctime>
#include <thread>

#include <mach/mach_time.h>
#include <sys/time.h>

namespace Platform
{

namespace
{

uint64_t MonotonicMilliseconds()
{
	return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1'000'000;
}

} // namespace

DWORD TimeGetTime()
{
	return static_cast<DWORD>(MonotonicMilliseconds());
}

ULONGLONG GetTickCount64()
{
	return MonotonicMilliseconds();
}

BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
	lpPerformanceCount->QuadPart = static_cast<LONGLONG>(mach_absolute_time());
	return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
	// mach_absolute_time() ticks are numer/denom nanoseconds.
	mach_timebase_info_data_t timebase {};
	mach_timebase_info(&timebase);
	lpFrequency->QuadPart = static_cast<LONGLONG>(
		1'000'000'000ULL * timebase.denom / timebase.numer);
	return TRUE;
}

void Sleep(DWORD dwMilliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

void GetLocalTime(SYSTEMTIME* lpSystemTime)
{
	timeval now {};
	gettimeofday(&now, nullptr);

	tm local {};
	localtime_r(&now.tv_sec, &local);

	lpSystemTime->wYear         = static_cast<WORD>(local.tm_year + 1900);
	lpSystemTime->wMonth        = static_cast<WORD>(local.tm_mon + 1);
	lpSystemTime->wDayOfWeek    = static_cast<WORD>(local.tm_wday);
	lpSystemTime->wDay          = static_cast<WORD>(local.tm_mday);
	lpSystemTime->wHour         = static_cast<WORD>(local.tm_hour);
	lpSystemTime->wMinute       = static_cast<WORD>(local.tm_min);
	lpSystemTime->wSecond       = static_cast<WORD>(local.tm_sec);
	lpSystemTime->wMilliseconds = static_cast<WORD>(now.tv_usec / 1000);
}

} // namespace Platform
