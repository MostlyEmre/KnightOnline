#include "../Input.h"

namespace Platform
{

SHORT GetAsyncKeyState(int vKey)
{
	return ::GetAsyncKeyState(vKey);
}

BOOL GetCursorPos(POINT* lpPoint)
{
	return ::GetCursorPos(lpPoint);
}

BOOL SetCursorPos(int x, int y)
{
	return ::SetCursorPos(x, y);
}

BOOL ScreenToClient(HWND hWnd, POINT* lpPoint)
{
	return ::ScreenToClient(hWnd, lpPoint);
}

BOOL ClientToScreen(HWND hWnd, POINT* lpPoint)
{
	return ::ClientToScreen(hWnd, lpPoint);
}

UINT GetDoubleClickTime()
{
	return ::GetDoubleClickTime();
}

HCURSOR LoadCursorResource(HINSTANCE hInstance, int resourceId)
{
	return ::LoadCursorA(hInstance, MAKEINTRESOURCEA(resourceId));
}

HCURSOR SetCursor(HCURSOR hCursor)
{
	return ::SetCursor(hCursor);
}

HCURSOR GetCursor()
{
	return ::GetCursor();
}

int ShowCursor(BOOL bShow)
{
	return ::ShowCursor(bShow);
}

} // namespace Platform
