#include "../Window.h"
#include "Cocoa/CocoaApp.h"
#include "InputScript.h"
#include "MacState.h"

namespace Platform
{

BOOL GetClientRect(HWND /*hWnd*/, RECT* lpRect)
{
	*lpRect = { 0, 0, macOS::Cocoa::ClientWidth(), macOS::Cocoa::ClientHeight() };
	return TRUE;
}

BOOL GetWindowRect(HWND /*hWnd*/, RECT* lpRect)
{
	int left = 0, top = 0, right = 0, bottom = 0;
	macOS::Cocoa::WindowScreenRect(left, top, right, bottom);
	*lpRect = { left, top, right, bottom };
	return TRUE;
}

void SetFocus(HWND /*hWnd*/)
{
	// The game window is the only one; keyboard focus within it is the text input field's business.
}

HWND GetActiveWindow()
{
	return macOS::Cocoa::IsActive() || macOS::InputScript::IsActive() ? macOS::MainWindow()
																	  : nullptr;
}

void PostQuitMessage(int nExitCode)
{
	macOS::RequestQuit(nExitCode);
}

void PumpEvents()
{
	macOS::Cocoa::PumpEvents();
}

void EnterDisplayMode(bool /*bWindowed*/, int /*iWidth*/, int /*iHeight*/, int /*iColorDepth*/)
{
	// The display mode stays as it is: fullscreen scales the game's image to the display instead.
}

void LeaveDisplayMode(bool /*bWindowed*/, int /*iWidth*/, int /*iHeight*/, int /*iColorDepth*/)
{
}

} // namespace Platform
