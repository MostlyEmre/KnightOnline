#ifndef N3BASE_PLATFORM_WIN32_WIN32INTERNAL_H
#define N3BASE_PLATFORM_WIN32_WIN32INTERNAL_H

#pragma once

// Shared between the Win32 platform implementation files; not part of the platform interface.

#include "../Types.h"

namespace Platform::Win32
{

/// \brief Posted to the main window by WSAAsyncSelect() for socket events.
inline constexpr UINT WM_SOCKETMSG = WM_USER + 1;

/// \brief The text input field's EDIT control, or nullptr (TextInput_Win32.cpp).
HWND GetTextInputWindow();

} // namespace Platform::Win32

#endif // N3BASE_PLATFORM_WIN32_WIN32INTERNAL_H
