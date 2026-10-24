#include "../Input.h"
#include "NotImplemented.h"

namespace Platform
{

SHORT GetAsyncKeyState(int /*vKey*/)
{
	macOS::NotImplemented("GetAsyncKeyState");
	return 0;
}

BOOL GetCursorPos(POINT* lpPoint)
{
	macOS::NotImplemented("GetCursorPos");
	*lpPoint = {};
	return FALSE;
}

BOOL SetCursorPos(int /*x*/, int /*y*/)
{
	macOS::NotImplemented("SetCursorPos");
	return FALSE;
}

BOOL ScreenToClient(HWND /*hWnd*/, POINT* /*lpPoint*/)
{
	macOS::NotImplemented("ScreenToClient");
	return FALSE;
}

BOOL ClientToScreen(HWND /*hWnd*/, POINT* /*lpPoint*/)
{
	macOS::NotImplemented("ClientToScreen");
	return FALSE;
}

UINT GetDoubleClickTime()
{
	macOS::NotImplemented("GetDoubleClickTime");
	return 500;
}

HCURSOR LoadCursorResource(HINSTANCE /*hInstance*/, int /*resourceId*/)
{
	macOS::NotImplemented("LoadCursorResource");
	return nullptr;
}

HCURSOR SetCursor(HCURSOR /*hCursor*/)
{
	macOS::NotImplemented("SetCursor");
	return nullptr;
}

HCURSOR GetCursor()
{
	macOS::NotImplemented("GetCursor");
	return nullptr;
}

int ShowCursor(BOOL /*bShow*/)
{
	macOS::NotImplemented("ShowCursor");
	return 0;
}

} // namespace Platform
