#ifndef N3BASE_PLATFORM_WINDOW_H
#define N3BASE_PLATFORM_WINDOW_H

#pragma once

#include "Types.h"

// The game window: its geometry, focus and the display mode around it.
namespace Platform
{

/// \brief The window's client area (left/top are 0).
BOOL GetClientRect(HWND hWnd, RECT* lpRect);

/// \brief The window's frame in screen coordinates.
BOOL GetWindowRect(HWND hWnd, RECT* lpRect);

/// \brief Gives the window keyboard focus.
void SetFocus(HWND hWnd);

/// \brief The application's active window, or nullptr when the application isn't active.
HWND GetActiveWindow();

/// \brief Asks the event loop to end with the given exit code (PostQuitMessage).
void PostQuitMessage(int nExitCode);

/// \brief Handles pending window-system events without rendering (used while waiting for a lost
/// render device).
void PumpEvents();

/// \brief Switches the display to the game's mode before the window is shown: the full mode when
/// fullscreen, just the colour depth when windowed (no-op where modes can't or needn't change).
void EnterDisplayMode(bool bWindowed, int iWidth, int iHeight, int iColorDepth);

/// \brief Returns the display to the user's mode after EnterDisplayMode().
void LeaveDisplayMode(bool bWindowed, int iWidth, int iHeight, int iColorDepth);

} // namespace Platform

#endif // N3BASE_PLATFORM_WINDOW_H
