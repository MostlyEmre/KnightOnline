#include "StdAfx.h"
#include "UIChat.h"
#include "GameEng.h"
#include "resource.h"
#include "text_resources.h"
#include "PacketDef.h"
#include "APISocket.h"
#include "PlayerMySelf.h"
#include "GameProcMain.h"
#include "N3WorldManager.h"

#include <FileIO/PathResolver.h>
#include <shared/Ini.h>

#include <ctime>

#include "UIManager.h"
#include "UIMessageBoxManager.h"

#include <N3Base/DFont.h>
#include <N3Base/N3SndMgr.h>
#include <N3Base/N3UIEdit.h>
#include <N3Base/Platform/Application.h>
#include <N3Base/Platform/Window.h>

static int WarFareMain(HINSTANCE hInstance, int nShowCmd);
static Platform::EventHandlers CreateEventHandlers();

#if defined(_WIN32)
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int nShowCmd)
{
	return WarFareMain(hInstance, nShowCmd);
}
#else
int main()
{
	return WarFareMain(nullptr, 0);
}
#endif

static int WarFareMain(HINSTANCE hInstance, int nShowCmd)
{
	// NOTE: get the current directory and make it known to CN3Base
	std::error_code ec;
	CN3Base::PathSet(std::filesystem::current_path(ec).string());

	// NOTE: we are anticipating an Options file to exist within this directory
	char szIniPath[_MAX_PATH] = "";
	lstrcpy(szIniPath, CN3Base::PathGet().c_str());
	lstrcat(szIniPath, "Option.ini");

	CIni ini(PathResolver::Resolve(szIniPath));

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Chr = ini.GetInt("Texture", "LOD_Chr", 0);
	if (CN3Base::s_Options.iTexLOD_Chr < 0)
		CN3Base::s_Options.iTexLOD_Chr = 0;
	if (CN3Base::s_Options.iTexLOD_Chr >= 2)
		CN3Base::s_Options.iTexLOD_Chr = 1;

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Shape = ini.GetInt("Texture", "LOD_Shape", 0);
	if (CN3Base::s_Options.iTexLOD_Shape < 0)
		CN3Base::s_Options.iTexLOD_Shape = 0;
	if (CN3Base::s_Options.iTexLOD_Shape >= 2)
		CN3Base::s_Options.iTexLOD_Shape = 1;

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Terrain = ini.GetInt("Texture", "LOD_Terrain", 0);
	if (CN3Base::s_Options.iTexLOD_Terrain < 0)
		CN3Base::s_Options.iTexLOD_Terrain = 0;
	if (CN3Base::s_Options.iTexLOD_Terrain >= 2)
		CN3Base::s_Options.iTexLOD_Terrain = 1;

	// NOTE: should we use shadows?
	CN3Base::s_Options.iUseShadow  = ini.GetInt("Shadow", "Use", 1);

	// NOTE: what is the screen resolution?
	CN3Base::s_Options.iViewWidth  = ini.GetInt("ViewPort", "Width", 1024);
	CN3Base::s_Options.iViewHeight = ini.GetInt("ViewPort", "Height", 768);

	if (CN3Base::s_Options.iViewWidth == 1024 || CN3Base::s_Options.iViewWidth == 1366)
		CN3Base::s_Options.iViewHeight = 768;
	else if (CN3Base::s_Options.iViewWidth == 1280)
		CN3Base::s_Options.iViewHeight = 1024;
	else if (CN3Base::s_Options.iViewWidth == 1600)
		CN3Base::s_Options.iViewHeight = 1200;
	else if (CN3Base::s_Options.iViewWidth == 1920)
		CN3Base::s_Options.iViewHeight = 1080;
#if 0
	else
	{
		CN3Base::s_Options.iViewWidth = 1024;
		CN3Base::s_Options.iViewHeight = 768;
	}
#endif

	// Load the viewport's color depth
	// Officially this defaults to 16-bit, but this isn't as supported these days so we should
	// just default to 32-bit to ensure compatibility with ChangeDisplaySettings().
	CN3Base::s_Options.iViewColorDepth = ini.GetInt("ViewPort", "ColorDepth", 32);
	if (CN3Base::s_Options.iViewColorDepth != 16 && CN3Base::s_Options.iViewColorDepth != 32)
		CN3Base::s_Options.iViewColorDepth = 32;

	// Load the viewport's draw distance
	CN3Base::s_Options.iViewDist = ini.GetInt("ViewPort", "Distance", 512);
	if (CN3Base::s_Options.iViewDist < 256)
		CN3Base::s_Options.iViewDist = 256;
	if (CN3Base::s_Options.iViewDist > 512)
		CN3Base::s_Options.iViewDist = 512;

	// Load the max distance for sound effects
	CN3Base::s_Options.iEffectSndDist = ini.GetInt("Sound", "Distance", 48);
	if (CN3Base::s_Options.iEffectSndDist < 20)
		CN3Base::s_Options.iEffectSndDist = 20;
	if (CN3Base::s_Options.iEffectSndDist > 48)
		CN3Base::s_Options.iEffectSndDist = 48;

	// Load the sound enabled flags
	CN3Base::s_Options.bSndBgmEnable    = ini.GetBool("Sound", "Bgm", true);
	CN3Base::s_Options.bSndEffectEnable = ini.GetBool("Sound", "Effect", true);
	CN3Base::s_Options.bSndEnable       = (CN3Base::s_Options.bSndBgmEnable || CN3Base::s_Options.bSndEffectEnable);

	// Load config to determine if we should we use the Windows cursor
	// If false, will use the software cursor (CGameCursor) instead.
	CN3Base::s_Options.bWindowCursor    = ini.GetBool("Cursor", "WindowCursor", true);

	// Load config to determine if we should run the game windowed or not.
	// true is windowed, false is fullscreen.
	CN3Base::s_Options.bWindowMode      = ini.GetBool("Screen", "WindowMode",
	// In debug builds, if not otherwise configured, we should just prefer to use windowed mode.
#if defined(_DEBUG)
		true
#else
		false
#endif
	);

	// Load unofficial config to determine if we should enable vsync or not
	// This is officially enabled by default.
	CN3Base::s_Options.bVSyncEnabled = ini.GetBool("Screen", "VSyncEnabled", true);

	srand((uint32_t) time(nullptr));

	// 메인 윈도우를 만들고..
	Platform::MainWindowDesc desc;
	desc.szTitle         = "Knight OnLine Client";
	desc.iClientWidth    = CN3Base::s_Options.iViewWidth;
	desc.iClientHeight   = CN3Base::s_Options.iViewHeight;
	desc.bWindowed       = CN3Base::s_Options.bWindowMode;
	desc.iIconResourceId = IDI_MAIN;
	desc.iShowCommand    = nShowCmd; // 보여준다..

	HWND hWndMain        = Platform::CreateMainWindow(hInstance, desc, CreateEventHandlers());
	if (hWndMain == nullptr)
	{
		CLogWriter::Write("Cannot create window.");
		exit(-1);
	}

	CGameProcedure::s_bWindowed = true;

	// allocate the static members
	CGameProcedure::StaticMemberInit(hInstance, hWndMain);

	// set the game's current procedure to s_pProcLogIn
	CGameProcedure::ProcActiveSet((CGameProcedure*) CGameProcedure::s_pProcLogIn);

	int iExitCode = Platform::RunEventLoop(hWndMain, IDR_MAIN_ACCELATOR);

	CGameProcedure::StaticMemberRelease();

	return iExitCode;
}

// The edit box text changed (typed into the platform's text input field).
static void OnTextInputChanged()
{
	CN3UIEdit* pEdit = CN3UIEdit::GetFocusedEdit();
	if (pEdit == nullptr)
		return;

	pEdit->UpdateTextFromEditCtrl();
	pEdit->UpdateCaretPosFromEditCtrl();
	CGameProcedure::SetGameCursor(CGameProcedure::s_hCursorNormal);
}

static void OnSocketEvent(Platform::SocketEvent eEvent)
{
	switch (eEvent)
	{
		case Platform::SocketEvent::Connected:
			TRACE("Socket connected");
			break;

		case Platform::SocketEvent::Closed:
			CGameProcedure::ReportServerConnectionClosed(true);
			TRACE("Socket closed");
			break;

		case Platform::SocketEvent::Readable:
			CGameProcedure::s_pSocket->Receive();
			break;
	}
}

static void OnActivate(HWND hWnd, bool bActive)
{
	if (bActive)
	{
		Platform::SetFocus(hWnd);

		CN3UIEdit* pUIFocused = CN3UIBase::GetFocusedEdit();
		if (pUIFocused != nullptr)
		{
			pUIFocused->KillFocus();
			pUIFocused->SetFocus();
		}

		CGameProcedure::s_bIsWindowInFocus = true;
	}
	else
	{
		CGameProcedure::s_bIsWindowInFocus = false;

		if (!CGameProcedure::s_bWindowed)
		{
			CLogWriter::Write("WA_INACTIVE.");
			Platform::PostQuitMessage(0);
		}
	}
}

// Returns true to keep the window open.
static bool OnCloseRequested()
{
	if (CGameProcedure::s_pProcActive != nullptr && CGameProcedure::s_pProcActive == CGameProcedure::s_pProcMain)
	{
		if (!_IsKeyDown(VK_MENU))
		{
			CGameProcedure::s_pProcMain->RequestExit();
			return true;
		}

		if (CGameProcedure::s_pProcMain->m_fExitTimer != -1.0f)
		{
			if (CGameProcedure::s_pProcMain->m_pUIChatDlg != nullptr)
			{
				std::string szMsg = fmt::format_text_resource(IDS_CANNOT_EXIT_DURING_A_BATTLE);
				CGameProcedure::s_pProcMain->m_pUIChatDlg->AddChatMsg(N3_CHAT_NORMAL, szMsg, 0xFFFF0000);
				CGameProcedure::s_pProcMain->m_eExitType = EXIT_TYPE_QUIT;
			}

			return true;
		}
	}

	CGameProcedure::s_pSocket->Disconnect();
	CGameProcedure::s_pSocketSub->Disconnect();

	Platform::PostQuitMessage(0);
	return false;
}

static void OnMouseWheel(short delta)
{
	CN3UIBase* pUI = nullptr;
	if (CGameProcedure::s_pMsgBoxMgr != nullptr)
		pUI = CGameProcedure::s_pMsgBoxMgr->GetFocusMsgBox();

	if (pUI != nullptr && pUI->IsVisible() && pUI->OnMouseWheelEvent(delta))
		return;

	if (CGameProcedure::s_pUIMgr != nullptr)
		pUI = CGameProcedure::s_pUIMgr->GetFocusedUI();

	if (pUI != nullptr && pUI->IsVisible() && pUI->OnMouseWheelEvent(delta))
		return;

	if (CGameProcedure::s_pProcActive == CGameProcedure::s_pProcMain)
		CGameProcedure::s_pEng->CameraZoom(delta * 0.05f);
}

static Platform::EventHandlers CreateEventHandlers()
{
	Platform::EventHandlers handlers;

	// Render a frame during idle time (no messages are waiting)
	handlers.OnIdle = []
	{
		CGameProcedure::TickActive();
		CGameProcedure::RenderActive();
	};

	handlers.OnActivate         = OnActivate;
	handlers.OnCloseRequested   = OnCloseRequested;
	handlers.OnMouseWheel       = OnMouseWheel;
	handlers.OnTextInputChanged = OnTextInputChanged;
	handlers.OnSocketEvent      = OnSocketEvent;
	return handlers;
}
