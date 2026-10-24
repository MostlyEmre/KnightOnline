#include "../Input.h"
#include "../KeyCodes.h"
#include "Cocoa/CocoaApp.h"
#include "InputScript.h"

#include <array>
#include <string>

namespace Platform
{

namespace
{

HCURSOR s_hCursor        = nullptr;
int s_iCursorShowCounter = 0;
std::array<bool, 256> s_wasDown {};

// Virtual key -> whether it's down now. Covers the keys the client asks GetAsyncKeyState about.
bool IsVirtualKeyDown(int vKey)
{
	using namespace macOS::Cocoa;
	if (macOS::InputScript::IsActive())
	{
		switch (vKey)
		{
			case VK_LBUTTON:
				return (macOS::InputScript::MouseButtons() & 0x1) != 0;
			case VK_RBUTTON:
				return (macOS::InputScript::MouseButtons() & 0x2) != 0;
			case VK_RETURN:
				return macOS::InputScript::IsKeyDown(DIK_RETURN);
			case VK_TAB:
				return macOS::InputScript::IsKeyDown(DIK_TAB);
			case VK_MENU:
				return macOS::InputScript::IsKeyDown(DIK_LMENU);
			case VK_CONTROL:
				return macOS::InputScript::IsKeyDown(DIK_LCONTROL);
			case VK_SHIFT:
				return macOS::InputScript::IsKeyDown(DIK_LSHIFT);
			default:
				return false;
		}
	}

	switch (vKey)
	{
		case VK_LBUTTON:
			return (PressedMouseButtons() & 0x1) != 0;
		case VK_RBUTTON:
			return (PressedMouseButtons() & 0x2) != 0;
		case VK_MBUTTON:
			return (PressedMouseButtons() & 0x4) != 0;
		case VK_MENU:
			return IsOptionDown();
		case VK_CONTROL:
			return IsControlDown();
		case VK_SHIFT:
			return IsKeyDown(DIK_LSHIFT) || IsKeyDown(DIK_RSHIFT);
		case VK_RETURN:
			return IsKeyDown(DIK_RETURN);
		case VK_TAB:
			return IsKeyDown(DIK_TAB);
		default:
			return false;
	}
}

} // namespace

SHORT GetAsyncKeyState(int vKey)
{
	if (vKey < 0 || vKey >= 256)
		return 0;

	bool bDown                           = IsVirtualKeyDown(vKey);
	bool bPressed                        = bDown && !s_wasDown[static_cast<size_t>(vKey)];
	s_wasDown[static_cast<size_t>(vKey)] = bDown;

	return static_cast<SHORT>((bDown ? 0x8000 : 0) | (bPressed ? 0x0001 : 0));
}

BOOL GetCursorPos(POINT* lpPoint)
{
	int x = 0, y = 0;
	if (macOS::InputScript::IsActive())
	{
		macOS::InputScript::CursorClient(x, y);
		macOS::Cocoa::ClientToScreen(x, y);
	}
	else
	{
		macOS::Cocoa::CursorScreenPosition(x, y);
	}
	*lpPoint = { x, y };
	return TRUE;
}

BOOL SetCursorPos(int x, int y)
{
	if (!macOS::InputScript::IsActive())
		macOS::Cocoa::WarpCursor(x, y);
	return TRUE;
}

BOOL ScreenToClient(HWND /*hWnd*/, POINT* lpPoint)
{
	int x = lpPoint->x, y = lpPoint->y;
	macOS::Cocoa::ScreenToClient(x, y);
	*lpPoint = { x, y };
	return TRUE;
}

BOOL ClientToScreen(HWND /*hWnd*/, POINT* lpPoint)
{
	int x = lpPoint->x, y = lpPoint->y;
	macOS::Cocoa::ClientToScreen(x, y);
	*lpPoint = { x, y };
	return TRUE;
}

UINT GetDoubleClickTime()
{
	return macOS::Cocoa::DoubleClickIntervalMs();
}

HCURSOR LoadCursorResource(HINSTANCE /*hInstance*/, int resourceId)
{
	// The executable's cursor resources are bundled as files named by resource ID.
	std::string szPath = macOS::Cocoa::ResourceDirectory() + "/Cursors/"
						 + std::to_string(resourceId) + ".cur";
	return static_cast<HCURSOR>(macOS::Cocoa::LoadCursorFile(szPath));
}

HCURSOR SetCursor(HCURSOR hCursor)
{
	HCURSOR hPrevious = s_hCursor;
	s_hCursor         = hCursor;
	macOS::Cocoa::SetCursor(hCursor);
	return hPrevious;
}

HCURSOR GetCursor()
{
	return s_hCursor;
}

int ShowCursor(BOOL bShow)
{
	bool bWasVisible      = s_iCursorShowCounter >= 0;
	s_iCursorShowCounter += bShow ? 1 : -1;
	bool bVisible         = s_iCursorShowCounter >= 0;
	if (bVisible != bWasVisible)
		macOS::Cocoa::SetCursorVisible(bVisible);
	return s_iCursorShowCounter;
}

} // namespace Platform
