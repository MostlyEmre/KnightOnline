#include "../Application.h"
#include "Win32Internal.h"

#include "../../LogWriter.h"

#include <windowsx.h>
#include <winsock.h> // WSAGETSELECTEVENT, FD_*

namespace Platform
{

namespace
{

EventHandlers s_handlers;

LRESULT CALLBACK WndProcMain(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_COMMAND:
		{
			uint16_t wNotifyCode = HIWORD(wParam); // notification code

			// NOLINTNEXTLINE(performance-no-int-to-ptr)
			HWND hwndCtl         = reinterpret_cast<HWND>(lParam);

			if (wNotifyCode == EN_CHANGE && hwndCtl != nullptr
				&& hwndCtl == Win32::GetTextInputWindow() && s_handlers.OnTextInputChanged)
				s_handlers.OnTextInputChanged();
		}
		break;

		case Win32::WM_SOCKETMSG:
		{
			if (!s_handlers.OnSocketEvent)
				break;

			switch (WSAGETSELECTEVENT(lParam))
			{
				case FD_CONNECT:
					s_handlers.OnSocketEvent(SocketEvent::Connected);
					break;

				case FD_CLOSE:
					s_handlers.OnSocketEvent(SocketEvent::Closed);
					break;

				case FD_READ:
					s_handlers.OnSocketEvent(SocketEvent::Readable);
					break;

				default:
					break;
			}
		}
		break;

		case WM_ACTIVATE:
		{
			int iActive = LOWORD(wParam); // activation flag
			switch (iActive)
			{
				case WA_CLICKACTIVE:
				case WA_ACTIVE:
					if (s_handlers.OnActivate)
						s_handlers.OnActivate(hWnd, true);
					return 1;

				case WA_INACTIVE:
					if (s_handlers.OnActivate)
						s_handlers.OnActivate(hWnd, false);
					break;

				default:
					break;
			}
		}
		break;

		case WM_CLOSE:
		case WM_DESTROY:
		case WM_QUIT:
			if (s_handlers.OnCloseRequested && s_handlers.OnCloseRequested())
				return 1;
			break;

		case WM_MOUSEWHEEL:
			if (s_handlers.OnMouseWheel)
				s_handlers.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
			break;

		default:
			break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

} // namespace

HWND CreateMainWindow(
	HINSTANCE hInstance, const MainWindowDesc& desc, const EventHandlers& handlers)
{
	s_handlers = handlers;

	WNDCLASSEXA wc;

	//  only register the window class once - use hInstance as a flag.
	wc.cbSize        = sizeof(WNDCLASSEXA);
	wc.style         = 0;
	wc.lpfnWndProc   = WndProcMain;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = LoadIconA(hInstance, MAKEINTRESOURCEA(desc.iIconResourceId));
	wc.hCursor       = nullptr;
	wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
	wc.lpszMenuName  = nullptr;
	wc.lpszClassName = desc.szTitle;
	wc.hIconSm       = nullptr;

	if (0 == ::RegisterClassExA(&wc))
	{
		CLogWriter::Write("Cannot register window class.");
		return nullptr;
	}

	DWORD style    = 0;
	int iViewWidth = 0, iViewHeight = 0;
	if (desc.bWindowed)
	{
		style = WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_GROUP;

		RECT rc;
		rc.left   = 0;
		rc.right  = desc.iClientWidth;
		rc.top    = 0;
		rc.bottom = desc.iClientHeight;

		AdjustWindowRect(&rc, style, FALSE);

		iViewWidth  = rc.right - rc.left;
		iViewHeight = rc.bottom - rc.top;
	}
	else
	{
		style       = WS_POPUP | WS_CLIPCHILDREN;
		iViewWidth  = desc.iClientWidth;
		iViewHeight = desc.iClientHeight;
	}

	HWND hWnd = ::CreateWindowExA(0, wc.lpszClassName, desc.szTitle, style, 0, 0, iViewWidth,
		iViewHeight, nullptr, nullptr, hInstance, nullptr);
	if (hWnd == nullptr)
		return nullptr;

	::ShowWindow(hWnd, desc.iShowCommand);
	::SetActiveWindow(hWnd);

	return hWnd;
}

int RunEventLoop(HWND hWnd, int iAcceleratorResourceId)
{
#if _DEBUG
	HACCEL hAccel = LoadAcceleratorsA(nullptr, MAKEINTRESOURCEA(iAcceleratorResourceId));
	HDC hDC       = GetDC(hWnd);
#else
	(void) iAcceleratorResourceId;
#endif // #if _DEBUG

	MSG msg {};
	BOOL bGotMsg = FALSE;

	while (WM_QUIT != msg.message)
	{
		// Use PeekMessage() if the app is active, so we can use idle time to
		// render the scene. Else, use GetMessage() to avoid eating CPU time.
		bGotMsg = PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE);
		if (bGotMsg)
		{
#if _DEBUG
			if (0 == TranslateAccelerator(hWnd, hAccel, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
#else
			TranslateMessage(&msg);
			DispatchMessage(&msg);
#endif // #if _DEBUG
		}
		else if (s_handlers.OnIdle)
		{
			// Render a frame during idle time (no messages are waiting)
			s_handlers.OnIdle();
		}
	}

#if _DEBUG
	ReleaseDC(hWnd, hDC);
	DestroyAcceleratorTable(hAccel);
#endif // #if _DEBUG

	return static_cast<int>(msg.wParam);
}

} // namespace Platform
