#include "../Shell.h"
#include "Cocoa/CocoaApp.h"
#include "CodePage.h"
#include "NotImplemented.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include <mach-o/dyld.h>

namespace Platform
{

namespace
{

std::string WideToUtf8(const wchar_t* text)
{
	std::string out;
	for (wchar_t ch : std::wstring_view(text != nullptr ? text : L""))
	{
		auto cp = static_cast<char32_t>(ch);
		if (cp < 0x80)
		{
			out += static_cast<char>(cp);
		}
		else if (cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

} // namespace

void ShowMessageBox(HWND /*hWndOwner*/, const char* text, const char* caption)
{
	macOS::Cocoa::ShowAlert(macOS::AnsiToUtf8(caption != nullptr ? caption : ""),
		macOS::AnsiToUtf8(text != nullptr ? text : ""));
}

void ShowMessageBox(HWND /*hWndOwner*/, const wchar_t* text, const wchar_t* caption)
{
	macOS::Cocoa::ShowAlert(WideToUtf8(caption), WideToUtf8(text));
}

void ShellOpen(const std::string& target)
{
	// Windows programs (the client launches Option.exe) don't exist here.
	std::string lower = target;
	for (char& ch : lower)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	if (lower.size() >= 4 && lower.ends_with(".exe"))
		return;

	macOS::Cocoa::OpenURL(macOS::AnsiToUtf8(target));
}

std::string GetExecutablePath()
{
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);

	std::string path(size, '\0');
	if (_NSGetExecutablePath(path.data(), &size) != 0)
		return {};

	char resolved[PATH_MAX] {};
	if (realpath(path.c_str(), resolved) == nullptr)
		return path.c_str();

	return resolved;
}

LANGID GetUserDefaultLangID()
{
	// The client only tells Taiwan (0x0404) apart; map the languages it might meet.
	std::string language = macOS::Cocoa::PreferredLanguage();
	if (language.starts_with("zh-Hant") || language == "zh-TW")
		return 0x0404;
	if (language.starts_with("zh"))
		return 0x0804;
	if (language.starts_with("ko"))
		return 0x0412;
	if (language.starts_with("ja"))
		return 0x0411;
	if (language.starts_with("tr"))
		return 0x041F;
	if (language.starts_with("de"))
		return 0x0407;
	return 0x0409; // en-US
}

void DebugOutput(const char* message)
{
	std::fputs(message, stderr);
}

namespace macOS
{

void NotImplemented(const char* szFunction)
{
	static std::mutex s_lock;
	static std::set<std::string> s_reported;

	std::lock_guard<std::mutex> lock(s_lock);
	if (s_reported.insert(szFunction).second)
	{
		std::string line = std::string("Platform: ") + szFunction
						   + " is not implemented on macOS yet\n";
		std::fputs(line.c_str(), stderr);
	}
}

} // namespace macOS

} // namespace Platform
