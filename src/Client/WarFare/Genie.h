#ifndef CLIENT_WARFARE_GENIE_H
#define CLIENT_WARFARE_GENIE_H

#pragma once

#include "GenieRules.h"
#include "GenieSettings.h"

#include <N3Base/My_3DStruct.h>

#include <string>

class CGameProcMain;
class CN3UIBase;

// Genie (auto-hunt): plays for the player while it's on. It hunts with the client's own commands
// and packets; the server only says whether it may run (WIZ_GENIE). Its decisions are in
// GenieRules.h, its settings in GenieSettings.h, and its window is CUIGenie.
class CGenie
{
public:
	explicit CGenie(CGameProcMain* pMain);
	~CGenie();

	CGenie(const CGenie&)            = delete;
	CGenie& operator=(const CGenie&) = delete;

	/// \brief GENIE_AVAILABLE: whether the server allows Genie. Until it says so, Genie can't start
	/// (and the client never sends WIZ_GENIE, which a server without Genie would disconnect it for).
	void OnAvailable(bool bAllowed);
	bool IsAvailable() const;

	/// \brief The server's answer to GENIE_START
	void OnStartResult(bool bOk);

	/// \brief Asks the server to start (Start in the window)
	void RequestStart();

	/// \brief Stops, says why in the message window, and tells the server
	void Stop(Genie::StopReason eReason);

	bool IsRunning() const;
	bool IsWaitingForStart() const;

	void Tick(float fNow);

	/// \brief The main scene is let go (back to character selection, or quitting)
	void OnLeaveGame();

	Genie::Settings& Settings();
	void LoadSettings();
	void SaveSettings();

	/// \brief A small "Genie" label on screen while it runs
	void RenderIndicator();

private:
	void SendGenie(uint8_t bySubOpcode);
	void HaltActions();
	void BuildIndicator();

	CGameProcMain* m_pMain = nullptr;
	bool m_bAvailable      = false;
	bool m_bRunning        = false;
	bool m_bWaitingStart   = false;
	float m_fStartSentAt   = 0.0f;
	float m_fNow           = 0.0f;
	__Vector3 m_vOrigin    = {};

	Genie::Settings m_Settings;
	std::string m_szSettingsOwner; // the character m_Settings belong to

	CN3UIBase* m_pIndicator = nullptr;
};

#endif // CLIENT_WARFARE_GENIE_H
