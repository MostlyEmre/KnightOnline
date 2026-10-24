#include "../Application.h"
#include "Cocoa/CocoaApp.h"
#include "Cocoa/OptionsWindow.h"
#include "InputScript.h"
#include "MacState.h"
#include "Options_macOS.h"

#include <chrono>
#include <thread>

namespace Platform
{

namespace
{

EventHandlers s_handlers;
bool s_bQuit             = false;
int s_iExitCode          = 0;
char s_windowToken       = 0; // HWND points here
bool s_bOptionsAfterQuit = false;
bool s_bRelaunch         = false;

// The Settings window. Changes go straight to Option.ini, for the next start.
bool ShowSettings(bool bAfterQuit)
{
	int iFitWidth = 0, iFitHeight = 0;
	macOS::Cocoa::FullscreenSize(iFitWidth, iFitHeight);

	macOS::Cocoa::OptionsWindowHooks hooks;
	hooks.OnChange  = [](const macOS::GameOptions& options) { macOS::SaveOptions(options); };
	hooks.OnRestart = []
	{
		s_bRelaunch = true;
		macOS::RequestQuit(0);
	};
	return macOS::Cocoa::ShowOptionsWindow(
		macOS::LoadOptions(), iFitWidth, iFitHeight, bAfterQuit, hooks);
}

// Sleeps out the rest of the frame for the frame rate limits, and keeps the display awake while
// the game is in front.
void PaceFrame(std::chrono::steady_clock::time_point frameStart)
{
	const macOS::GameOptions& options = macOS::StartupOptions();
	bool bActive                      = macOS::Cocoa::IsActive();
	macOS::Cocoa::SetKeepAwake(bActive && options.bKeepAwake);

	// Test scripts run unthrottled whatever's in front.
	int iFps = bActive || macOS::InputScript::IsActive() ? options.iMaxFps : options.iBackgroundFps;
	if (iFps > 0)
		std::this_thread::sleep_until(frameStart + std::chrono::microseconds(1'000'000 / iFps));
}

} // namespace

namespace macOS
{

HWND MainWindow()
{
	return reinterpret_cast<HWND>(&s_windowToken);
}

const EventHandlers& Handlers()
{
	return s_handlers;
}

void RequestQuit(int iExitCode)
{
	s_bQuit     = true;
	s_iExitCode = iExitCode;
}

void RequestOptionsAfterQuit()
{
	s_bOptionsAfterQuit = true;
}

} // namespace macOS

HWND CreateMainWindow(
	HINSTANCE /*hInstance*/, const MainWindowDesc& desc, const EventHandlers& handlers)
{
	s_handlers = handlers;

	macOS::Cocoa::WindowEvents events;
	events.OnActivate = [](bool bActive)
	{
		if (s_handlers.OnActivate)
			s_handlers.OnActivate(macOS::MainWindow(), bActive);
	};

	events.OnCloseRequested = []
	{
		if (s_handlers.OnCloseRequested)
			(void) s_handlers.OnCloseRequested();
	};

	events.OnMouseWheel = [](int iDelta)
	{
		// While a test script plays, input comes from the script only.
		if (s_handlers.OnMouseWheel && !macOS::InputScript::IsActive())
			s_handlers.OnMouseWheel(static_cast<short>(iDelta));
	};
	events.OnMouseHWheel = [](int iDelta)
	{
		if (s_handlers.OnMouseHWheel && !macOS::InputScript::IsActive())
			s_handlers.OnMouseHWheel(static_cast<short>(iDelta));
	};
	events.OnShowSettings             = [] { ShowSettings(false); };

	const macOS::GameOptions& options = macOS::StartupOptions();
	macOS::Cocoa::InputOptions input;
	input.bSwipeTurns   = options.bSwipeTurns;
	input.dSwipeScale   = options.iSwipeSpeed / 100.0 * (options.bSwipeReversed ? -1.0 : 1.0);
	input.bControlClick = options.bControlClick;
	macOS::Cocoa::SetInputOptions(input);

	macOS::Cocoa::WindowOptions window;
	window.iScalePercent = options.iWindowScale;
	window.bSmooth       = options.bSmoothScaling;

	// Test scripts run in a window that doesn't take focus, so they never cover the screen or take
	// the keyboard of whoever is using the Mac.
	bool bScripted       = macOS::InputScript::IsActive();
	bool bFullscreen     = !desc.bWindowed && !bScripted;
	if (!macOS::Cocoa::CreateGameWindow(desc.szTitle != nullptr ? desc.szTitle : "",
			desc.iClientWidth, desc.iClientHeight, bFullscreen, !bScripted, window, events))
		return nullptr;

	return macOS::MainWindow();
}

int RunEventLoop(HWND /*hWnd*/, int /*iAcceleratorResourceId*/)
{
	while (!s_bQuit)
	{
		auto frameStart = std::chrono::steady_clock::now();
		macOS::Cocoa::PumpEvents();
		if (s_bQuit)
			break;

		macOS::DispatchSocketEvents();
		macOS::InputScript::Tick();
		if (s_bQuit)
			break;

		// Render a frame once the pending events are handled.
		if (s_handlers.OnIdle)
			s_handlers.OnIdle();

		PaceFrame(frameStart);
	}
	macOS::Cocoa::SetKeepAwake(false);

	// The game closed itself to show its options (Option.exe on Windows).
	if (s_bOptionsAfterQuit)
	{
		macOS::Cocoa::HideGameWindow();
		s_bRelaunch = ShowSettings(true);
	}
	if (s_bRelaunch)
		macOS::Cocoa::RelaunchApp();

	return s_iExitCode;
}

} // namespace Platform
