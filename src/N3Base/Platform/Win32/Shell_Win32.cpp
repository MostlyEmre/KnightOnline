#include "../Shell.h"

#include <shellapi.h>

namespace Platform
{

void ShowMessageBox(HWND hWndOwner, const char* text, const char* caption)
{
	::MessageBoxA(hWndOwner, text, caption, MB_OK);
}

void ShowMessageBox(HWND hWndOwner, const wchar_t* text, const wchar_t* caption)
{
	::MessageBoxW(hWndOwner, text, caption, MB_OK);
}

void ShellOpen(const std::string& target)
{
	::ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string GetExecutablePath()
{
	char szPath[_MAX_PATH] {};
	::GetModuleFileNameA(nullptr, szPath, _MAX_PATH);
	return szPath;
}

LANGID GetUserDefaultLangID()
{
	return ::GetUserDefaultLangID();
}

void DebugOutput(const char* message)
{
	::OutputDebugStringA(message);
}

} // namespace Platform
