#ifndef N3BASE_PLATFORM_SHELL_H
#define N3BASE_PLATFORM_SHELL_H

#pragma once

#include "Types.h"

#include <string>

// Process and desktop services: message boxes, opening documents/URLs, the executable's location,
// the user's language and debugger output.
namespace Platform
{

/// \brief Shows a modal message box with an OK button. Text is in the client's ANSI code page.
void ShowMessageBox(HWND hWndOwner, const char* text, const char* caption);

/// \brief Shows a modal message box with an OK button.
void ShowMessageBox(HWND hWndOwner, const wchar_t* text, const wchar_t* caption);

/// \brief Opens a document, program or URL with its default handler (ShellExecute "open").
void ShellOpen(const std::string& target);

/// \brief The full path of the running executable.
std::string GetExecutablePath();

/// \brief The user's default language as a Windows LANGID (e.g. 0x0409 for en-US).
LANGID GetUserDefaultLangID();

/// \brief Writes a message to the debugger / system log.
void DebugOutput(const char* message);

} // namespace Platform

#endif // N3BASE_PLATFORM_SHELL_H
