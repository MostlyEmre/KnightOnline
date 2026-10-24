#ifndef N3BASE_PLATFORM_APPLICATION_H
#define N3BASE_PLATFORM_APPLICATION_H

#pragma once

#include "Types.h"

#include <cstdint>
#include <functional>

// The main window and the event loop that drives the game. The platform translates its window-
// system events into the handlers below; whenever no events are pending it calls OnIdle, where the
// game ticks and renders a frame.
namespace Platform
{

enum class SocketEvent : uint8_t
{
	Connected,
	Readable,
	Closed
};

struct EventHandlers
{
	/// \brief No events are pending: run a frame.
	std::function<void()> OnIdle;

	/// \brief The window was activated (bActive) or deactivated.
	std::function<void(HWND hWnd, bool bActive)> OnActivate;

	/// \brief The user asked to close the window. Return true to keep it open.
	std::function<bool()> OnCloseRequested;

	/// \brief The mouse wheel turned, in WHEEL_DELTA (120) units per notch.
	std::function<void(short sDelta)> OnMouseWheel;

	/// \brief Sideways scrolling (a tilted wheel, two fingers swiped across a trackpad), in
	/// WHEEL_DELTA units; positive to the right.
	std::function<void(short sDelta)> OnMouseHWheel;

	/// \brief The player asked for the game's Options window from outside the game (macOS: the app
	/// menu's Settings..., Cmd-,).
	std::function<void()> OnShowOptions;

	/// \brief Scripted runs (InputScript's "genie" command): open or close Genie's window, as G
	/// does in the game. A script's keys don't reach the game without the window focused.
	std::function<void()> OnShowGenie;

	/// \brief The text in the text input field (TextInput.h) changed.
	std::function<void()> OnTextInputChanged;

	/// \brief A connected socket (Socket.h) changed state.
	std::function<void(SocketEvent eEvent)> OnSocketEvent;
};

struct MainWindowDesc
{
	const char* szTitle = "";
	int iClientWidth    = 0;
	int iClientHeight   = 0;
	bool bWindowed      = true;
	int iIconResourceId = 0; // in the executable's resources, where the platform has them
	int iShowCommand    = 0; // how to show the window first (Win32 SW_*; 0 = default)
};

/// \brief Prepares the process before the game reads anything; the game then treats its working
/// directory as the game data folder. Nothing to do on Windows. The macOS app makes a folder in
/// Application Support its working directory, lays it over the game data inside the bundle and
/// starts the bundled servers. Returns false if the game can't run (the user has been told why).
bool InitializeProcess();

/// \brief Creates and shows the main window. The handlers apply from creation on.
/// Returns nullptr on failure.
HWND CreateMainWindow(
	HINSTANCE hInstance, const MainWindowDesc& desc, const EventHandlers& handlers);

/// \brief Runs the event loop until PostQuitMessage(); returns its exit code. Debug builds
/// translate keyboard accelerators from the given resource where the platform has them.
int RunEventLoop(HWND hWnd, int iAcceleratorResourceId);

} // namespace Platform

#endif // N3BASE_PLATFORM_APPLICATION_H
