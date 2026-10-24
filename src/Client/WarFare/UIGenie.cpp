#include "StdAfx.h"
#include "UIGenie.h"
#include "Genie.h"
#include "GameProcMain.h"

#include <N3Base/N3UIString.h>

namespace
{

constexpr D3DCOLOR COLOR_STATUS = 0xfffae6af;

// Layout, from the window's top left.
constexpr int WIDTH             = 420;
constexpr int STATUS_TOP        = 44;
constexpr int BUTTON_TOP        = STATUS_TOP + 32;
constexpr int HEIGHT            = BUTTON_TOP + UIArt::BAR_HEIGHT + 20;

} // namespace

CUIGenie::CUIGenie()
{
	SetToggleKey(DIK_G);
}

CUIGenie::~CUIGenie() = default;

CGenie* CUIGenie::GetGenie()
{
	return CGameProcedure::s_pProcMain != nullptr ? CGameProcedure::s_pProcMain->m_pGenie : nullptr;
}

void CUIGenie::Build()
{
	BuildFrame(WIDTH, HEIGHT, "Genie");

	m_pStatus = UIArt::AddString(
		this, UIArt::MakeRect(24, STATUS_TOP, WIDTH - 24, STATUS_TOP + 22), "", 10, true, COLOR_STATUS, UISTYLE_STRING_ALIGNCENTER);

	const int iLeft = WIDTH / 2 - UIArt::BAR_WIDTH - 3;
	m_pBtnStart     = CUIArtButton::Create(
        this, UIArt::MakeRect(iLeft, BUTTON_TOP, iLeft + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Start");
	m_pBtnClose = CUIArtButton::Create(
		this, UIArt::MakeRect(WIDTH / 2 + 3, BUTTON_TOP, WIDTH / 2 + 3 + UIArt::BAR_WIDTH, BUTTON_TOP + UIArt::BAR_HEIGHT), "Close");
}

void CUIGenie::Open()
{
	if (IsVisible())
		return;

	if (m_pBtnStart == nullptr)
		Build();

	if (CGenie* pGenie = GetGenie())
		pGenie->LoadSettings();

	SetPosCenter();
	m_pBtnStart->SetState(UI_STATE_BUTTON_NORMAL);
	m_pBtnClose->SetState(UI_STATE_BUTTON_NORMAL);
	UpdateControls();
	CUIArtWindow::Open();
}

void CUIGenie::Close()
{
	if (!IsVisible())
		return;

	if (CGenie* pGenie = GetGenie())
		pGenie->SaveSettings();

	CUIArtWindow::Close();
}

void CUIGenie::Tick()
{
	CUIArtWindow::Tick();
	if (IsVisible())
		UpdateControls();
}

void CUIGenie::UpdateControls()
{
	CGenie* pGenie = GetGenie();
	if (pGenie == nullptr)
		return;

	if (pGenie->IsRunning())
	{
		m_pBtnStart->SetLabel("Stop");
		m_pStatus->SetString("Genie is hunting. Moving by hand stops it.");
	}
	else if (pGenie->IsWaitingForStart())
	{
		m_pBtnStart->SetLabel("Starting...");
		m_pStatus->SetString("Asking the server...");
	}
	else if (!pGenie->IsAvailable())
	{
		m_pBtnStart->SetLabel("Start");
		m_pStatus->SetString("This server doesn't allow Genie.");
	}
	else
	{
		m_pBtnStart->SetLabel("Start");
		m_pStatus->SetString("Genie hunts here while you're away.");
	}
}

bool CUIGenie::ReceiveMessage(CN3UIBase* pSender, uint32_t dwMsg)
{
	if (CUIArtWindow::ReceiveMessage(pSender, dwMsg))
		return true;

	if (dwMsg != UIMSG_BUTTON_CLICK)
		return false;

	if (pSender == m_pBtnClose)
	{
		Close();
		return true;
	}

	if (pSender == m_pBtnStart)
	{
		CGenie* pGenie = GetGenie();
		if (pGenie == nullptr)
			return true;

		if (pGenie->IsRunning())
			pGenie->Stop(Genie::StopReason::ByPlayer);
		else
			pGenie->RequestStart();

		UpdateControls();
		return true;
	}

	return false;
}
