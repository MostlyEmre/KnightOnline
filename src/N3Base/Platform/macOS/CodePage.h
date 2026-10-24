#ifndef N3BASE_PLATFORM_MACOS_CODEPAGE_H
#define N3BASE_PLATFORM_MACOS_CODEPAGE_H

#pragma once

#include <string>
#include <string_view>

// The client's strings are in the Windows ANSI code page of the system it was made for: 1252
// (Western European) for the 1.298 US client. macOS speaks Unicode, so text is converted where
// it crosses into Cocoa (fonts, text input, alerts).
namespace Platform::macOS
{

/// \brief Windows-1252 bytes to UTF-8.
std::string AnsiToUtf8(std::string_view text);

/// \brief UTF-8 to Windows-1252; characters 1252 doesn't have become '?'.
std::string Utf8ToAnsi(std::string_view text);

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_CODEPAGE_H
