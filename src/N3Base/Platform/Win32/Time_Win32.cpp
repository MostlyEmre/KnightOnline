#include "../Time.h"

#include <mmsystem.h>

namespace Platform
{

DWORD TimeGetTime()
{
	return ::timeGetTime();
}

ULONGLONG GetTickCount64()
{
	return ::GetTickCount64();
}

BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
	return ::QueryPerformanceCounter(lpPerformanceCount);
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
	return ::QueryPerformanceFrequency(lpFrequency);
}

void Sleep(DWORD dwMilliseconds)
{
	::Sleep(dwMilliseconds);
}

void GetLocalTime(SYSTEMTIME* lpSystemTime)
{
	::GetLocalTime(lpSystemTime);
}

} // namespace Platform
