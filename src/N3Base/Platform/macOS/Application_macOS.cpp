#include "../Application.h"
#include "Cocoa/CocoaApp.h"
#include "InputScript.h"
#include "MacState.h"

namespace Platform
{

namespace
{

EventHandlers s_handlers;
bool s_bQuit       = false;
int s_iExitCode    = 0;
char s_windowToken = 0; // HWND points here

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

	if (!macOS::Cocoa::CreateGameWindow(desc.szTitle != nullptr ? desc.szTitle : "",
			desc.iClientWidth, desc.iClientHeight, !desc.bWindowed, !macOS::InputScript::IsActive(),
			events))
		return nullptr;

	return macOS::MainWindow();
}

int RunEventLoop(HWND /*hWnd*/, int /*iAcceleratorResourceId*/)
{
	while (!s_bQuit)
	{
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
	}

	return s_iExitCode;
}

} // namespace Platform
