#include "StdAfx.h"
#include "Genie.h"
#include "APISocket.h"
#include "GameProcMain.h"
#include "PacketDef.h"
#include "PlayerMySelf.h"
#include "UIArt.h"
#include "UIManager.h"

#include <FileIO/PathResolver.h>
#include <N3Base/N3UIString.h>

namespace
{

constexpr D3DCOLOR COLOR_STARTED = 0xff80ff80;
constexpr D3DCOLOR COLOR_STOPPED = 0xffffff00;

constexpr int INDICATOR_WIDTH    = 110;
constexpr int INDICATOR_TOP      = 64;

} // namespace

CGenie::CGenie(CGameProcMain* pMain) : m_pMain(pMain)
{
}

CGenie::~CGenie()
{
	delete m_pIndicator;
}

void CGenie::OnAvailable(bool bAllowed)
{
	m_bAvailable = bAllowed;
}

bool CGenie::IsAvailable() const
{
	return m_bAvailable;
}

bool CGenie::IsRunning() const
{
	return m_bRunning;
}

bool CGenie::IsWaitingForStart() const
{
	return m_bWaitingStart;
}

Genie::Settings& CGenie::Settings()
{
	return m_Settings;
}

void CGenie::SendGenie(uint8_t bySubOpcode)
{
	// Never to a server that hasn't said it knows WIZ_GENIE: it would disconnect us.
	if (!m_bAvailable || CGameProcedure::s_pSocket == nullptr || !CGameProcedure::s_pSocket->IsConnected())
		return;

	uint8_t byBuff[4];
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_GENIE);
	CAPISocket::MP_AddByte(byBuff, iOffset, bySubOpcode);
	CGameProcedure::s_pSocket->Send(byBuff, iOffset);
}

void CGenie::RequestStart()
{
	if (!m_bAvailable || m_bRunning || m_bWaitingStart)
		return;

	if (CGameProcedure::s_pPlayer == nullptr || CGameProcedure::s_pPlayer->IsDead())
		return;

	SaveSettings();
	m_bWaitingStart = true;
	m_fStartSentAt  = m_fNow;
	SendGenie(GENIE_START);
}

void CGenie::OnStartResult(bool bOk)
{
	if (!m_bWaitingStart)
		return;

	m_bWaitingStart = false;
	if (!bOk)
	{
		Stop(Genie::StopReason::ServerRefused);
		return;
	}

	m_bRunning = true;
	m_vOrigin  = CGameProcedure::s_pPlayer->Position();
	m_pMain->MsgOutput("Genie started. Moving by hand stops it.", COLOR_STARTED);
}

void CGenie::Stop(Genie::StopReason eReason)
{
	const bool bWasRunning = m_bRunning;
	m_bRunning             = false;
	m_bWaitingStart        = false;

	if (bWasRunning)
	{
		SendGenie(GENIE_STOP);
		HaltActions();
	}

	const char* szMessage = Genie::StopMessage(eReason);
	if (szMessage[0] != '\0' && eReason != Genie::StopReason::LeftGame)
		m_pMain->MsgOutput(szMessage, COLOR_STOPPED);
}

void CGenie::HaltActions()
{
	m_pMain->CommandEnableAttackContinous(false, nullptr);
	if (CGameProcedure::s_pPlayer != nullptr && CGameProcedure::s_pPlayer->IsMovingNow())
		m_pMain->CommandMove(MD_STOP, true);
}

void CGenie::Tick(float fNow)
{
	m_fNow = fNow;

	if (m_bWaitingStart && Genie::StartTimedOut(m_fStartSentAt, fNow))
	{
		Stop(Genie::StopReason::NoReply);
		return;
	}
}

void CGenie::OnLeaveGame()
{
	Stop(Genie::StopReason::LeftGame);
	m_bAvailable = false;
	m_szSettingsOwner.clear();
}

void CGenie::LoadSettings()
{
	if (CGameProcedure::s_pPlayer == nullptr)
		return;

	const std::string& szName = CGameProcedure::s_pPlayer->m_InfoBase.szID;
	if (szName.empty() || szName == m_szSettingsOwner)
		return;

	CIni ini(PathResolver::Resolve(Genie::SettingsPath(CN3Base::PathGet(), szName)));
	m_Settings = {};
	Genie::Load(ini, m_Settings);
	m_szSettingsOwner = szName;
}

void CGenie::SaveSettings()
{
	LoadSettings(); // the settings in hand are this character's
	if (m_szSettingsOwner.empty())
		return;

	const std::filesystem::path path = Genie::SettingsPath(CN3Base::PathGet(), m_szSettingsOwner);
	CIni ini(PathResolver::Resolve(path));
	Genie::Save(ini, m_Settings);
	ini.Save(PathResolver::ResolveForWriting(path));
}

void CGenie::BuildIndicator()
{
	m_pIndicator = new CN3UIBase();
	m_pIndicator->Init(nullptr);
	const RECT rc = UIArt::MakeRect(0, 0, INDICATOR_WIDTH, UIArt::BAR_HEIGHT);
	m_pIndicator->SetRegion(rc);
	UIArt::AddBar(m_pIndicator, rc, UIArt::BAR_BLUE);
	UIArt::AddString(m_pIndicator, UIArt::OffsetBy(rc, 1, 1), "Genie", 11, true, UIArt::COLOR_SHADOW, UISTYLE_STRING_ALIGNCENTER);
	UIArt::AddString(m_pIndicator, rc, "Genie", 11, true, UIArt::COLOR_BUTTON, UISTYLE_STRING_ALIGNCENTER);
}

void CGenie::RenderIndicator()
{
	if (!m_bRunning)
		return;

	if (m_pIndicator == nullptr)
		BuildIndicator();

	// Top middle of the screen, under the target bar.
	const int iLeft = static_cast<int>((CN3Base::s_CameraData.vp.Width - INDICATOR_WIDTH) / 2);
	const RECT& rc  = m_pIndicator->GetRegion();
	if (rc.left != iLeft || rc.top != INDICATOR_TOP)
		m_pIndicator->MoveOffset(iLeft - rc.left, INDICATOR_TOP - rc.top);

	CUIManager::RenderStateSet();
	m_pIndicator->Render();
	CUIManager::RenderStateRestore();
}
