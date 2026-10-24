#ifndef N3BASE_PLATFORM_MACOS_MACSTATE_H
#define N3BASE_PLATFORM_MACOS_MACSTATE_H

#pragma once

// Shared between the macOS platform implementation files; not part of the platform interface.

#include "../Application.h"

namespace Platform::macOS
{

/// \brief The handle of the (only) game window.
HWND MainWindow();

/// \brief The handlers the game passed to CreateMainWindow().
const EventHandlers& Handlers();

/// \brief PostQuitMessage(): ends the event loop with this exit code.
void RequestQuit(int iExitCode);

/// \brief Raises socket events for the open sockets that have something to report (Socket_macOS.cpp).
void DispatchSocketEvents();

} // namespace Platform::macOS

#endif // N3BASE_PLATFORM_MACOS_MACSTATE_H
