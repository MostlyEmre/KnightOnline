/*
*/

#include "StdAfx.h"
#include "LocalInput.h"

#include "GameProcMain.h"
#include "GameEng.h"

#include <N3Base/Platform/Time.h>
#include <N3Base/Platform/Window.h>

CLocalInput::CLocalInput()
{
	m_hWnd          = nullptr;

	m_bNoKeyDown    = FALSE;

	m_nMouseFlag    = 0;
	m_nMouseFlagOld = 0;

	m_dwTickLBDown  = 0;
	m_dwTickRBDown  = 0;

	m_ptCurMouse.x = m_ptCurMouse.y = 0;
	m_ptOldMouse.x = m_ptOldMouse.y = 0;

	SetRect(&m_rcLBDrag, 0, 0, 0, 0);
	SetRect(&m_rcMBDrag, 0, 0, 0, 0);
	SetRect(&m_rcRBDrag, 0, 0, 0, 0);

	memset(m_byCurKeys, 0, sizeof(m_byCurKeys));
	memset(m_byOldKeys, 0, sizeof(m_byOldKeys));
	memset(m_bKeyPresses, 0, sizeof(m_bKeyPresses));
	memset(m_bKeyPresseds, 0, sizeof(m_bKeyPresseds));
	memset(m_dwTickKeyPress, 0, sizeof(m_dwTickKeyPress));
}

CLocalInput::~CLocalInput()
{
	// shutdown keyboard
	if (m_Keyboard.IsCreated())
	{
		UnacquireKeyboard();
		m_Keyboard.Release();
	}
}

//////////////////////////////////////////////////////////////////////////////////
// Try to accquire all devices. Use SetActiveDevices() if you do not want
// some devices.
//////////////////////////////////////////////////////////////////////////////////
BOOL CLocalInput::Init(HINSTANCE hInst, HWND hWnd)
{
	m_hWnd = hWnd; // 윈도우 핸들 기억..

	// Create the keyboard device
	if (!m_Keyboard.Init(hInst, hWnd))
		return FALSE;

	AcquireKeyboard();
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Controls which devices you have accquired.
/////////////////////////////////////////////////////////////////////////////////////////////
void CLocalInput::SetActiveDevices(BOOL bKeyboard)
{
	if (bKeyboard)
		AcquireKeyboard();
	else
		UnacquireKeyboard();
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Wipes out the internal key data.
/////////////////////////////////////////////////////////////////////////////////////////////
void CLocalInput::KeyboardFlushData()
{
	memset(m_byOldKeys, 0, NUMDIKEYS);
	memset(m_byCurKeys, 0, NUMDIKEYS);
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Sets the mouse position. This restricts the position to the physical display.
/////////////////////////////////////////////////////////////////////////////////////////////
void CLocalInput::MouseSetPos(int x, int y)
{
	m_ptCurMouse.x = x;
	m_ptCurMouse.y = y;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Retrieves the keystate of a given key.
/////////////////////////////////////////////////////////////////////////////////////////////
BOOL CLocalInput::KeyboardGetKeyState(int nDIKey)
{
	if (m_bMuted || nDIKey < 0 || nDIKey >= NUMDIKEYS)
		return FALSE;

	return m_byCurKeys[nDIKey];
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Obtains access to the keyboard.
/////////////////////////////////////////////////////////////////////////////////////////////
void CLocalInput::AcquireKeyboard()
{
	if (m_Keyboard.Acquire())
	{
		//			m_bKeyboard = TRUE;
		KeyboardFlushData();
		return;
	}
	//		m_bKeyboard = FALSE;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Releases access to the keyboard.
/////////////////////////////////////////////////////////////////////////////////////////////
void CLocalInput::UnacquireKeyboard()
{
	KeyboardFlushData();
	//	m_bKeyboard = FALSE;

	m_Keyboard.Unacquire();
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Updates all devices. Call this before you check for input.
/////////////////////////////////////////////////////////////////////////////////////////////
// 되도록이면 전체 프로시저 돌때 한번씩만 도는게 좋다.. 여러번 하면 혼란이 올수도 있다.
void CLocalInput::Tick()
{
	HWND hWndActive = Platform::GetActiveWindow(); // 포커싱되었을때만...
	if (hWndActive != m_hWnd)
		return;

	///////////////////////
	//  KEYBOARD
	///////////////////////
	//	if(m_bKeyboard)
	//	{
	memcpy(m_byOldKeys, m_byCurKeys, NUMDIKEYS); // 전의 키 상태 기록

	if (!m_Keyboard.GetState(m_byCurKeys))       // 현재 키 상태 기록
		AcquireKeyboard();
	else
	{
		m_bNoKeyDown = TRUE; // 첨엔 아무것도 안눌림

		for (int i = 0; i < NUMDIKEYS; i++)
		{
			if (!m_byOldKeys[i] && m_byCurKeys[i])
				m_bKeyPresses[i] = TRUE; // 눌리는 순간
			else
				m_bKeyPresses[i] = FALSE;

			if (m_byOldKeys[i] && !m_byCurKeys[i])
				m_bKeyPresseds[i] = TRUE; // 눌렀다 떼는 순간..
			else
				m_bKeyPresseds[i] = FALSE;

			if (m_byCurKeys[i])
				m_bNoKeyDown = FALSE;
		}
	}
	//	}

	///////////////////////
	//  MOUSE
	///////////////////////

	m_ptOldMouse = m_ptCurMouse; // 일단 전의 것 복사...

	RECT rcClient;
	Platform::GetClientRect(m_hWnd, &rcClient);
	Platform::GetCursorPos(&m_ptCurMouse);           // 좀 이상해서... 그냥 시스템 마우스 커서 위치 가져오기
	Platform::ScreenToClient(m_hWnd, &m_ptCurMouse); // 클라이언트 영역으로 변환

	if (PtInRect(&rcClient, m_ptCurMouse))           //  && GetFocus() == m_hWnd) // 스크린 영역 밖에 있거나 포커스가 가있지 않으면..
	{
		// 마우스 버튼 상태 보관.
		m_nMouseFlagOld = m_nMouseFlag;
		m_nMouseFlag    = 0;

		// 마우스 상태 가져오기
		if (_IsKeyDown(VK_LBUTTON))
			m_nMouseFlag |= MOUSE_LBDOWN;

		if (_IsKeyDown(VK_MBUTTON))
			m_nMouseFlag |= MOUSE_MBDOWN;

		if (_IsKeyDown(VK_RBUTTON))
			m_nMouseFlag |= MOUSE_RBDOWN;

		// 버튼 클릭 직후..
		if (!(m_nMouseFlagOld & MOUSE_LBDOWN) && (m_nMouseFlag & MOUSE_LBDOWN))
			m_nMouseFlag |= MOUSE_LBCLICK;

		if (!(m_nMouseFlagOld & MOUSE_MBDOWN) && (m_nMouseFlag & MOUSE_MBDOWN))
			m_nMouseFlag |= MOUSE_MBCLICK;

		if (!(m_nMouseFlagOld & MOUSE_RBDOWN) && (m_nMouseFlag & MOUSE_RBDOWN))
			m_nMouseFlag |= MOUSE_RBCLICK;

		// 버튼에서 손을 떼면
		if ((m_nMouseFlagOld & MOUSE_LBDOWN) && !(m_nMouseFlag & MOUSE_LBDOWN))
			m_nMouseFlag |= MOUSE_LBCLICKED;

		if ((m_nMouseFlagOld & MOUSE_MBDOWN) && !(m_nMouseFlag & MOUSE_MBDOWN))
			m_nMouseFlag |= MOUSE_MBCLICKED;

		if ((m_nMouseFlagOld & MOUSE_RBDOWN) && !(m_nMouseFlag & MOUSE_RBDOWN))
			m_nMouseFlag |= MOUSE_RBCLICKED;

		static DWORD dwDblClk = Platform::GetDoubleClickTime(); // 윈도우의 더블 클릭시간을 가져오고..
		if (m_nMouseFlag & MOUSE_LBCLICKED)                     // 왼쪽 더블 클릭 감지
		{
			static DWORD dwCLicked = 0;
			if (Platform::TimeGetTime() < dwCLicked + dwDblClk)
				m_nMouseFlag |= MOUSE_LBDBLCLK;
			dwCLicked = Platform::TimeGetTime();
		}

		if (m_nMouseFlag & MOUSE_MBCLICKED) // 왼쪽 더블 클릭 감지
		{
			static DWORD dwCLicked = 0;
			if (Platform::TimeGetTime() < dwCLicked + dwDblClk)
				m_nMouseFlag |= MOUSE_MBDBLCLK;
			dwCLicked = Platform::TimeGetTime();
		}

		if (m_nMouseFlag & MOUSE_RBCLICKED) // 왼쪽 더블 클릭 감지
		{
			static DWORD dwCLicked = 0;
			if (Platform::TimeGetTime() < dwCLicked + dwDblClk)
				m_nMouseFlag |= MOUSE_RBDBLCLK;
			dwCLicked = Platform::TimeGetTime();
		}

		// 드래그 영역 처리
		if (m_nMouseFlag & MOUSE_LBDOWN)
		{
			m_rcLBDrag.right  = m_ptCurMouse.x;
			m_rcLBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_MBDOWN)
		{
			m_rcMBDrag.right  = m_ptCurMouse.x;
			m_rcMBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_RBDOWN)
		{
			m_rcRBDrag.right  = m_ptCurMouse.x;
			m_rcRBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_LBCLICK)
		{
			m_rcLBDrag.left = m_ptCurMouse.x;
			m_rcLBDrag.top  = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_MBCLICK)
		{
			m_rcMBDrag.left = m_ptCurMouse.x;
			m_rcMBDrag.top  = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_RBCLICK)
		{
			m_rcRBDrag.left = m_ptCurMouse.x;
			m_rcRBDrag.top  = m_ptCurMouse.y;
		}
	}
}
