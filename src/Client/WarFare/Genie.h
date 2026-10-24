#ifndef CLIENT_WARFARE_GENIE_H
#define CLIENT_WARFARE_GENIE_H

#pragma once

#include "GenieRules.h"
#include "GenieSettings.h"

#include <N3Base/My_3DStruct.h>

#include <map>
#include <set>
#include <string>

class CGameProcMain;
class CN3UIBase;
class CPlayerBase;
class CPlayerNPC;
struct __TABLE_UPC_SKILL;

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

	/// \brief A monster attacked the player (an attack, or a spell cast at the player)
	void OnAttackedBy(int iAttackerID);

	/// \brief The player hit something: progress, if it's Genie's target
	void OnHit(int iTargetID);

	/// \brief The player moved or attacked by hand: they're back, so Genie stops
	void OnManualControl();

	void OnDied();
	void OnZoneChanged();

	/// \brief The bundle Genie asked for opened (its window stays hidden)
	void OnBundleOpened();

	/// \brief A pick-up result (WIZ_ITEM_GET): a full bag means going to town
	void OnLootResult(uint8_t byResult);

	Genie::Settings& Settings();
	void LoadSettings();
	void SaveSettings();

	/// \brief A small "Genie" label on screen while it runs
	void RenderIndicator();

private:
	void SendGenie(uint8_t bySubOpcode);
	void HaltActions();
	void BuildIndicator();

	/// \brief One decision, every DECISION_INTERVAL while running
	void Decide();

	/// \brief Potions, heals, buffs and sitting for MP; true when that took the turn
	bool Survive();
	bool UsePotion(int iItemID);
	bool CastOnSelf(int iSkillID);
	bool IsSkillReady(int iSkillID) const;
	int CountItem(int iItemID) const;
	bool AnyAttackCastable() const;

	/// \brief Loots what monsters dropped, from where the player stands; true when walking to a drop
	bool Loot();
	void SendBundleOpen(int iBundleID);
	CPlayerBase* FindCorpse(int iID) const;
	bool WantsItem(const struct __IconItemSkill& item) const;

	/// \brief Fights the monster ChooseTarget() picks; false when there's none
	bool Fight();
	void CastAttack(CPlayerNPC* pTarget, __TABLE_UPC_SKILL* pSkill);
	void MoveToTarget(CPlayerNPC* pTarget);
	void StopMoving();
	void StandUp();
	void ReturnToOrigin();
	bool IsAttackedRecently() const;
	static bool IsMonster(const CPlayerNPC* pNPC);

	CGameProcMain* m_pMain  = nullptr;
	bool m_bAvailable       = false;
	bool m_bRunning         = false;
	bool m_bWaitingStart    = false;
	float m_fStartSentAt    = 0.0f;
	float m_fNow            = 0.0f;
	float m_fNextDecision   = 0.0f;
	__Vector3 m_vOrigin     = {};

	int m_iTarget           = -1;
	float m_fTargetDistance = 0.0f;
	Genie::TargetProgress m_Progress;
	std::map<int, float> m_Attackers;  // monster ID -> when it last attacked the player
	std::map<int, float> m_HeldSkills; // skill ID -> until when it's held back after a refusal

	// Looting: the bundle asked for, from which corpse, and the bundles done with.
	int m_iLootBundle   = -1;
	int m_iLootCorpse   = -1;
	float m_fLootSentAt = 0.0f;
	bool m_bLootOpened  = false;
	bool m_bLootRetried = false;
	std::set<int> m_DoneBundles;

	/// \brief Why Genie has to go to town, once it can (a full bag, out of potions or arrows)
	Genie::StopReason m_eTownReason = Genie::StopReason::None;

	Genie::Settings m_Settings;
	std::string m_szSettingsOwner; // the character m_Settings belong to

	CN3UIBase* m_pIndicator = nullptr;
};

#endif // CLIENT_WARFARE_GENIE_H
