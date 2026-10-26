#include "../Window.h"

namespace Platform
{

BOOL GetClientRect(HWND hWnd, RECT* lpRect)
{
	return ::GetClientRect(hWnd, lpRect);
}

BOOL GetWindowRect(HWND hWnd, RECT* lpRect)
{
	return ::GetWindowRect(hWnd, lpRect);
}

void SetFocus(HWND hWnd)
{
	::SetFocus(hWnd);
}

HWND GetActiveWindow()
{
	return ::GetActiveWindow();
}

void PostQuitMessage(int nExitCode)
{
	::PostQuitMessage(nExitCode);
}

void PumpEvents()
{
	MSG msg {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void EnterDisplayMode(bool bWindowed, int iWidth, int iHeight, int iColorDepth)
{
	if (bWindowed)
	{
		DEVMODE dm {};
		EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &dm);
		if (dm.dmBitsPerPel != (DWORD) iColorDepth)
		{
			dm.dmSize       = sizeof(DEVMODE);
			dm.dmPelsWidth  = iWidth;
			dm.dmPelsHeight = iHeight;
			dm.dmBitsPerPel = iColorDepth;
			dm.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			::ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
		}
	}
	else
	{
		DEVMODE dm {};
		dm.dmSize       = sizeof(DEVMODE);
		dm.dmPelsWidth  = iWidth;
		dm.dmPelsHeight = iHeight;
		dm.dmBitsPerPel = iColorDepth;
		dm.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		::ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
	}
}

void LeaveDisplayMode(bool bWindowed, int iWidth, int iHeight, int iColorDepth)
{
	if (bWindowed)
	{
		DEVMODE dm {};
		::EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &dm);

		if (dm.dmBitsPerPel != (DWORD) iColorDepth)
		{
			dm.dmSize   = sizeof(DEVMODE);
			dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			::ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
		}
	}
	else
	{
		DEVMODE dm {};
		::EnumDisplaySettings(nullptr, ENUM_REGISTRY_SETTINGS, &dm);

		if (dm.dmPelsWidth != (DWORD) iWidth || dm.dmPelsHeight != (DWORD) iHeight
			|| dm.dmBitsPerPel != (DWORD) iColorDepth)
		{
			dm.dmSize   = sizeof(DEVMODE);
			dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			::ChangeDisplaySettings(&dm, CDS_FULLSCREEN);
		}
	}
}

} // namespace Platform
