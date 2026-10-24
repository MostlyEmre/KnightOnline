#include "../TextInput.h"
#include "Win32Internal.h"

#include <imm.h>

namespace Platform::TextInput
{

namespace
{

HWND s_hWndEdit        = nullptr;
HWND s_hWndParent      = nullptr;
WNDPROC s_lpfnEditProc = nullptr;
Handlers s_handlers;

void NotifyCaretMayHaveMoved()
{
	if (s_handlers.OnCaretMayHaveMoved)
		s_handlers.OnCaretMayHaveMoved();
}

LRESULT APIENTRY EditWndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	//
	// When the focus is in an edit control inside a dialog box, the
	//  default ENTER key action will not occur.
	//
	switch (Message)
	{
		case WM_KEYDOWN:
			if (wParam == VK_RETURN)
			{
				if (s_handlers.OnReturn)
					s_handlers.OnReturn();

				return 0;
			}

			CallWindowProc(s_lpfnEditProc, hWnd, Message, wParam, lParam);
			NotifyCaretMayHaveMoved();
			return 0;

		case WM_CHAR:
			NotifyCaretMayHaveMoved();

			if (wParam == VK_RETURN || wParam == VK_TAB)
				return 0;
			break;

		case WM_INPUTLANGCHANGE:
		{
			POINT ptPos = { 0, 0 };
			SetImeStatus(ptPos, true);
		}
		break;

		case WM_IME_ENDCOMPOSITION:
		{
			POINT ptPos = { -1000, -1000 };
			SetImeStatus(ptPos, false);
		}
		break;

		case WM_IME_STARTCOMPOSITION:
		{
			POINT ptPos = { 0, 0 };
			SetImeStatus(ptPos, true);
		}
		break;

		default:
			break;
	}

	return CallWindowProc(s_lpfnEditProc, hWnd, Message, wParam, lParam);
}

} // namespace

bool Create(HWND hWndParent, const RECT& rc, const Handlers& handlers)
{
	if (nullptr == hWndParent)
		return false;
	if (s_hWndEdit)
		return false;

	s_handlers     = handlers;
	s_hWndParent   = hWndParent;
	s_hWndEdit     = CreateWindowA("EDIT", "EditWindow",
			WS_CHILD | WS_TABSTOP | ES_LEFT | ES_WANTRETURN, rc.left, rc.top, rc.right - rc.left,
			rc.bottom - rc.top, hWndParent, nullptr, nullptr, nullptr);

	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	s_lpfnEditProc = reinterpret_cast<WNDPROC>(
		SetWindowLongPtr(s_hWndEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditWndProc)));

	// Set the edit control's text size to the maximum.
	::SendMessage(s_hWndEdit, EM_LIMITTEXT, 0, 0);

	// Set the edit control's font
	HFONT hFont = static_cast<HFONT>(GetStockObject(ANSI_FIXED_FONT));
	::SendMessage(s_hWndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));
	::SendMessage(s_hWndEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));

	// 배경 지우기...??
	HDC hDC = GetDC(s_hWndEdit);
	SetBkMode(hDC, TRANSPARENT);
	SetROP2(hDC, R2_XORPEN);
	ReleaseDC(s_hWndEdit, hDC);

	return true;
}

bool IsCreated()
{
	return s_hWndEdit != nullptr;
}

void Focus(const RECT& rcRegion, const std::string& szText)
{
	if (s_hWndEdit == nullptr)
		return;

	::SetFocus(s_hWndEdit);

	int iX = rcRegion.left;
	int iY = rcRegion.top;
	int iH = rcRegion.bottom - rcRegion.top;
	int iW = rcRegion.right - rcRegion.left;
	::MoveWindow(s_hWndEdit, iX, iY, iW, iH, false);

	::SetWindowTextA(s_hWndEdit, szText.c_str());
}

void Unfocus()
{
	if (s_hWndEdit == nullptr)
		return;

	::SetWindowTextA(s_hWndEdit, "");
	::SetFocus(s_hWndParent);
}

std::string GetText()
{
	char szBuff[512] {};
	if (s_hWndEdit != nullptr)
		::GetWindowTextA(s_hWndEdit, szBuff, 512);
	return szBuff;
}

int GetCaretPos()
{
	if (s_hWndEdit == nullptr)
		return 0;

	LRESULT lResult = ::SendMessage(s_hWndEdit, EM_GETSEL, 0, 0);
	return LOWORD(lResult);
}

void SetImeStatus(POINT ptPos, bool bOpen)
{
	// The tools never create the field; keeping them free of imm32, as before.
#ifndef _N3TOOL
	HKL hHKL = GetKeyboardLayout(0);
	if (ImmIsIME(hHKL))
	{
		HIMC hImc = ImmGetContext(s_hWndEdit);
		if (bOpen)
		{
			SendMessage(s_hWndEdit, WM_IME_NOTIFY, IMN_OPENSTATUSWINDOW, 0);
			ImmSetStatusWindowPos(hImc, &ptPos);
		}
		else
		{
			SendMessage(s_hWndEdit, WM_IME_NOTIFY, IMN_CLOSESTATUSWINDOW, 0);
		}
		ImmReleaseContext(s_hWndEdit, hImc);
	}
#else
	(void) ptPos;
	(void) bOpen;
#endif
}

} // namespace Platform::TextInput

namespace Platform::Win32
{

HWND GetTextInputWindow()
{
	return TextInput::s_hWndEdit;
}

} // namespace Platform::Win32
