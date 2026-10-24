#include "../PlatformOptions.h"

#include <windows.h>

namespace Platform
{

bool HasOptionsProgram()
{
	return true; // Option.exe
}

void GetFullscreenSize(int& iWidth, int& iHeight)
{
	iWidth  = ::GetSystemMetrics(SM_CXSCREEN);
	iHeight = ::GetSystemMetrics(SM_CYSCREEN);
}

std::vector<PlatformOption> GetPlatformOptions()
{
	return {};
}

void SetPlatformOption(const std::string& /*szKey*/, int /*iValue*/)
{
}

void RequestRestart()
{
	char szPath[MAX_PATH] = {};
	::GetModuleFileNameA(nullptr, szPath, MAX_PATH);

	STARTUPINFOA si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};
	if (::CreateProcessA(szPath, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
	{
		::CloseHandle(pi.hThread);
		::CloseHandle(pi.hProcess);
	}

	::PostQuitMessage(0);
}

} // namespace Platform
