#include "../Window.h"
#include "NotImplemented.h"

namespace Platform
{

BOOL GetClientRect(HWND /*hWnd*/, RECT* lpRect)
{
	macOS::NotImplemented("GetClientRect");
	*lpRect = {};
	return FALSE;
}

BOOL GetWindowRect(HWND /*hWnd*/, RECT* lpRect)
{
	macOS::NotImplemented("GetWindowRect");
	*lpRect = {};
	return FALSE;
}

void SetFocus(HWND /*hWnd*/)
{
	macOS::NotImplemented("SetFocus");
}

HWND GetActiveWindow()
{
	macOS::NotImplemented("GetActiveWindow");
	return nullptr;
}

void PostQuitMessage(int /*nExitCode*/)
{
	macOS::NotImplemented("PostQuitMessage");
}

void PumpEvents()
{
	macOS::NotImplemented("PumpEvents");
}

void EnterDisplayMode(bool /*bWindowed*/, int /*iWidth*/, int /*iHeight*/, int /*iColorDepth*/)
{
	macOS::NotImplemented("EnterDisplayMode");
}

void LeaveDisplayMode(bool /*bWindowed*/, int /*iWidth*/, int /*iHeight*/, int /*iColorDepth*/)
{
	macOS::NotImplemented("LeaveDisplayMode");
}

} // namespace Platform
