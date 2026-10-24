#include "StdAfx.h"
#include "Genie.h"
#include "APISocket.h"
#include "GameProcMain.h"
#include "PacketDef.h"
#include "MagicSkillMng.h"
#include "PlayerMySelf.h"
#include "PlayerOtherMgr.h"
#include "SubProcPerTrade.h"
#include "UIArt.h"
#include "UIDroppedItemDlg.h"
#include "UIManager.h"
#include "UIInventory.h"
#include "UISkillTreeDlg.h"

#include <FileIO/PathResolver.h>
#include <N3Base/N3UIString.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

constexpr D3DCOLOR COLOR_STARTED  = 0xff80ff80;
constexpr D3DCOLOR COLOR_STOPPED  = 0xffffff00;

constexpr int INDICATOR_WIDTH     = 110;
constexpr int INDICATOR_TOP       = 64;

constexpr float DECISION_INTERVAL = 0.3f;  // seconds between decisions
constexpr float ATTACKER_MEMORY   = 10.0f; // how long a monster that attacked counts as attacking
constexpr float SKILL_HOLD        = 3.0f;  // how long a refused skill is held back
constexpr float ORIGIN_SLACK      = 3.0f;  // how close to its start spot Genie stands when idle
constexpr float PROGRESS_STEP     = 0.5f;  // getting this much closer to the target counts as progress
constexpr float LOOT_REPLY_WAIT   = 1.0f;  // a bundle that doesn't open in this long is in another region

float DistanceXZ(const __Vector3& vA, const __Vector3& vB)
{
	const float fX = vA.x - vB.x, fZ = vA.z - vB.z;
	return std::sqrt((fX * fX) + (fZ * fZ));
}

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

	m_bRunning      = true;
	m_vOrigin       = CGameProcedure::s_pPlayer->Position();
	m_fNextDecision = m_fNow;
	m_iTarget       = -1;
	m_Progress      = {};
	m_Attackers.clear();
	m_HeldSkills.clear();
	m_iLootBundle = -1;
	m_DoneBundles.clear();
	m_eTownReason = Genie::StopReason::None;
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
	// Stopping sends packets: not once the connection is gone (the game is closing).
	if (CGameProcedure::s_pSocket == nullptr || !CGameProcedure::s_pSocket->IsConnected())
		return;

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

	if (!m_bRunning || fNow < m_fNextDecision)
		return;

	m_fNextDecision = fNow + DECISION_INTERVAL;
	Decide();
}

void CGenie::OnAttackedBy(int iAttackerID)
{
	if (m_bRunning)
		m_Attackers[iAttackerID] = m_fNow;
}

void CGenie::OnHit(int iTargetID)
{
	if (m_bRunning && iTargetID == m_iTarget)
		m_Progress.OnProgress(m_fNow);
}

void CGenie::OnManualControl()
{
	if (m_bRunning)
		Stop(Genie::StopReason::MovedByHand);
}

void CGenie::OnDied()
{
	if (m_bRunning)
		Stop(Genie::StopReason::Died);
}

void CGenie::OnZoneChanged()
{
	if (m_bRunning)
		Stop(Genie::StopReason::ZoneChanged);
}

bool CGenie::IsMonster(const CPlayerNPC* pNPC)
{
	// Monsters have no nation; town NPCs and guards have one. Objects (gates, levers) have a shape.
	return pNPC != nullptr && pNPC->IsAlive() && pNPC->m_fTimeAfterDeath <= 0.0f && pNPC->m_InfoBase.eNation == NATION_NOTSELECTED
		   && pNPC->m_pShapeExtraRef == nullptr && pNPC->m_InfoBase.iAuthority != AUTHORITY_MANAGER;
}

bool CGenie::IsAttackedRecently() const
{
	for (const auto& [iID, fWhen] : m_Attackers)
	{
		if (m_fNow - fWhen < ATTACKER_MEMORY && IsMonster(CGameProcedure::s_pOPMgr->NPCGetByID(iID, true)))
			return true;
	}

	return false;
}

void CGenie::Decide()
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	if (pPlayer == nullptr || pPlayer->IsDead())
	{
		Stop(Genie::StopReason::Died);
		return;
	}

	// A town reason (Task 8 takes the player there; until then, Genie stops).
	if (m_eTownReason != Genie::StopReason::None)
	{
		Stop(m_eTownReason);
		return;
	}

	if (Survive() || Loot() || Fight())
		return;

	ReturnToOrigin();
}

void CGenie::OnBundleOpened()
{
	if (m_iLootBundle >= 0)
		m_bLootOpened = true;
}

void CGenie::OnLootResult(uint8_t byResult)
{
	if (!m_bRunning)
		return;

	const Genie::StopReason eReason = Genie::LootResultReason(byResult);
	if (eReason != Genie::StopReason::None && m_Settings.town.bBagFull)
		m_eTownReason = eReason;
}

CPlayerBase* CGenie::FindCorpse(int iID) const
{
	CPlayerBase* pCorpse = CGameProcedure::s_pOPMgr->CorpseGetByID(iID);
	return pCorpse != nullptr ? pCorpse : CGameProcedure::s_pOPMgr->NPCGetByID(iID, false);
}

bool CGenie::WantsItem(const __IconItemSkill& item) const
{
	if (item.pItemBasic == nullptr || item.pItemExt == nullptr)
		return false;

	Genie::LootCandidate candidate;
	candidate.bGold      = item.pItemBasic->dwID == dwGold;
	candidate.eKind      = Genie::KindOfItemClass(item.pItemBasic->byClass);
	candidate.eGrade     = Genie::GradeOfItemAttrib(item.pItemExt->byMagicOrRare);
	candidate.iSellPrice = item.GetSellPrice();
	return Genie::WantsLoot(m_Settings.loot, candidate);
}

void CGenie::SendBundleOpen(int iBundleID)
{
	// Genie's own request: the client's (MsgSend_RequestItemBundleOpen) refuses corpses out of
	// reach, while the server only wants them in the player's region.
	m_pMain->m_pUIDroppedItemDlg->BeginQuietLoot(iBundleID, [this](const __IconItemSkill& item) { return WantsItem(item); });

	uint8_t byBuff[8];
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_BUNDLE_OPEN_REQ);
	CAPISocket::MP_AddDword(byBuff, iOffset, static_cast<uint32_t>(iBundleID));
	CGameProcedure::s_pSocket->Send(byBuff, iOffset);

	m_fLootSentAt = m_fNow;
	m_bLootOpened = false;
}

bool CGenie::Loot()
{
	CUIDroppedItemDlg* pDlg = m_pMain->m_pUIDroppedItemDlg;
	if (m_iLootBundle >= 0)
	{
		if (m_bLootOpened)
		{
			// Picking up, one item after another; done once the window leaves the dropped state.
			if (pDlg->IsQuietLooting())
				return false;

			m_DoneBundles.insert(m_iLootBundle);
			m_iLootBundle = -1;
		}
		else if (m_fNow - m_fLootSentAt < LOOT_REPLY_WAIT)
		{
			return false;
		}
		else
		{
			// No answer: the drop is in another region. Walk to it and ask once more.
			CPlayerBase* pCorpse = FindCorpse(m_iLootCorpse);
			if (m_bLootRetried || pCorpse == nullptr)
			{
				pDlg->CancelQuietLoot();
				m_DoneBundles.insert(m_iLootBundle);
				m_iLootBundle = -1;
				return false;
			}

			if (DistanceXZ(CGameProcedure::s_pPlayer->Position(), pCorpse->Position()) > ORIGIN_SLACK)
			{
				StandUp();
				if (!CGameProcedure::s_pPlayer->m_bTargetOrPosMove)
					m_pMain->CommandMove(MD_FORWARD, true);
				CGameProcedure::s_pPlayer->SetMoveTargetPos(pCorpse->Position());
				return true;
			}

			StopMoving();
			m_bLootRetried = true;
			SendBundleOpen(m_iLootBundle);
			return true;
		}
	}

	// The nearest drop not looted yet.
	const __Vector3 vSelf = CGameProcedure::s_pPlayer->Position();
	CPlayerNPC* pNearest  = nullptr;
	float fNearest        = 0.0f;
	auto consider         = [&](CPlayerNPC* pNPC)
	{
		if (pNPC == nullptr || pNPC->m_iDroppedItemID <= 0 || !pNPC->IsDead() || m_DoneBundles.contains(pNPC->m_iDroppedItemID))
			return;

		const float fDistance = DistanceXZ(vSelf, pNPC->Position());
		if (pNearest == nullptr || fDistance < fNearest)
		{
			pNearest = pNPC;
			fNearest = fDistance;
		}
	};
	for (const auto& [iID, pCorpse] : CGameProcedure::s_pOPMgr->m_Corpses)
		consider(pCorpse);
	for (const auto& [iID, pNPC] : CGameProcedure::s_pOPMgr->m_NPCs)
		consider(pNPC);

	if (pNearest == nullptr)
		return false;

	m_iLootBundle  = pNearest->m_iDroppedItemID;
	m_iLootCorpse  = pNearest->IDNumber();
	m_bLootRetried = false;
	SendBundleOpen(m_iLootBundle);
	return false;
}

int CGenie::CountItem(int iItemID) const
{
	return iItemID > 0 ? m_pMain->m_pUIInventory->GetIndexItemCount(static_cast<uint32_t>(iItemID / 1000 * 1000)) : 0;
}

bool CGenie::UsePotion(int iItemID)
{
	// A potion is used by casting its item's effect skill, as the hotkey bar does.
	__TABLE_ITEM_BASIC* pItem = iItemID > 0 ? CGameBase::s_pTbl_Items_Basic.Find(iItemID / 1000 * 1000) : nullptr;
	__TABLE_UPC_SKILL* pSkill = pItem != nullptr ? CGameBase::s_pTbl_Skill.Find(pItem->dwEffectID1) : nullptr;
	if (pSkill == nullptr || CountItem(iItemID) <= 0 || m_pMain->m_pMagicSkillMng->GetCooldown(pSkill) >= 0.0f)
		return false;

	return m_pMain->m_pMagicSkillMng->MsgSend_MagicProcess(CGameProcedure::s_pPlayer->IDNumber(), pSkill);
}

bool CGenie::IsSkillReady(int iSkillID) const
{
	__TABLE_UPC_SKILL* pSkill = iSkillID > 0 ? CGameBase::s_pTbl_Skill.Find(iSkillID) : nullptr;
	if (pSkill == nullptr)
		return false;

	auto held = m_HeldSkills.find(iSkillID);
	return m_pMain->m_pUISkillTreeDlg->HasIDSkill(iSkillID) && m_pMain->m_pMagicSkillMng->GetCooldown(pSkill) < 0.0f
		   && m_pMain->m_pMagicSkillMng->CheckValidSkillMagic(pSkill) && (held == m_HeldSkills.end() || m_fNow >= held->second);
}

bool CGenie::CastOnSelf(int iSkillID)
{
	if (!IsSkillReady(iSkillID) || m_pMain->m_pMagicSkillMng->IsCasting())
		return false;

	StopMoving();
	if (!m_pMain->m_pMagicSkillMng->MsgSend_MagicProcess(CGameProcedure::s_pPlayer->IDNumber(), CGameBase::s_pTbl_Skill.Find(iSkillID)))
	{
		m_HeldSkills[iSkillID] = m_fNow + SKILL_HOLD;
		return false;
	}

	return true;
}

bool CGenie::AnyAttackCastable() const
{
	if (m_Settings.bBasicAttack)
		return true;

	return std::any_of(
		m_Settings.attackSkills.begin(), m_Settings.attackSkills.end(), [this](int iSkillID) { return IsSkillReady(iSkillID); });
}

bool CGenie::Survive()
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	const int iHp = pPlayer->m_InfoBase.iHP, iHpMax = pPlayer->m_InfoBase.iHPMax;
	const int iMp = pPlayer->m_InfoExt.iMSP, iMpMax = pPlayer->m_InfoExt.iMSPMax;

	// Potions don't take the turn: they're instant.
	if (m_Settings.bHpPotion && Genie::BelowPercent(iHp, iHpMax, m_Settings.iHpPercent))
		UsePotion(m_Settings.iHpPotionID);
	if (m_Settings.bMpPotion && Genie::BelowPercent(iMp, iMpMax, m_Settings.iMpPercent))
		UsePotion(m_Settings.iMpPotionID);

	if (Genie::BelowPercent(iHp, iHpMax, m_Settings.iHealPercent))
	{
		for (int iSkillID : m_Settings.healSkills)
		{
			if (CastOnSelf(iSkillID))
				return true;
		}
	}

	// A buff is cast again once its effect is gone from the player.
	CMagicSkillMng* pMagic = m_pMain->m_pMagicSkillMng;
	for (int iSkillID : m_Settings.buffSkills)
	{
		__TABLE_UPC_SKILL_TYPE_4* pBuff = iSkillID > 0 ? pMagic->m_pTbl_Type_4->Find(iSkillID) : nullptr;
		if (pBuff != nullptr && pMagic->m_ListBuffTypeID.count(pBuff->iBuffType) == 0 && CastOnSelf(iSkillID))
			return true;
	}

	// Nothing to attack with until MP comes back: sit for it, unless something is attacking.
	if (Genie::ShouldSitToRecover(IsAttackedRecently(), AnyAttackCastable(), iMp, iMpMax, pPlayer->m_bSitDown))
	{
		if (!pPlayer->m_bSitDown)
		{
			m_pMain->CommandEnableAttackContinous(false, nullptr);
			StopMoving();
			m_pMain->CommandSitDown(false, true);
		}
		return true;
	}

	return false;
}

bool CGenie::Fight()
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	const __Vector3 vSelf  = pPlayer->Position();

	std::vector<Genie::Monster> monsters;
	for (const auto& [iID, pNPC] : CGameProcedure::s_pOPMgr->m_NPCs)
	{
		if (!IsMonster(pNPC) || m_Progress.IsSkipped(iID, m_fNow))
			continue;

		Genie::Monster monster;
		monster.iID          = iID;
		monster.fX           = pNPC->Position().x;
		monster.fZ           = pNPC->Position().z;
		auto it              = m_Attackers.find(iID);
		monster.bAttackingMe = it != m_Attackers.end() && m_fNow - it->second < ATTACKER_MEMORY;
		monster.bOnList      = m_Settings.monsters.empty()
						  || std::find(m_Settings.monsters.begin(), m_Settings.monsters.end(), pNPC->IDString())
								 != m_Settings.monsters.end();
		monsters.push_back(monster);
	}

	const int iTarget   = Genie::ChooseTarget(monsters, m_vOrigin.x, m_vOrigin.z, static_cast<float>(m_Settings.iRange), vSelf.x, vSelf.z);
	CPlayerNPC* pTarget = iTarget >= 0 ? CGameProcedure::s_pOPMgr->NPCGetByID(iTarget, true) : nullptr;
	if (pTarget == nullptr)
	{
		if (m_iTarget >= 0)
		{
			m_iTarget = -1;
			m_pMain->CommandEnableAttackContinous(false, nullptr);
		}
		return false;
	}

	StandUp();
	const float fDistance = DistanceXZ(vSelf, pTarget->Position());
	if (iTarget != m_iTarget)
	{
		m_iTarget         = iTarget;
		m_fTargetDistance = fDistance;
		m_Progress.Reset(iTarget, m_fNow);
		m_pMain->TargetSelect(iTarget, true);
	}
	else if (fDistance < m_fTargetDistance - PROGRESS_STEP)
	{
		m_fTargetDistance = fDistance;
		m_Progress.OnProgress(m_fNow);
	}

	// Stuck on it (out of reach, or not getting hurt): leave it alone for a while.
	if (m_Progress.ShouldSkip(m_fNow))
	{
		m_Progress.Skip(iTarget, m_fNow);
		m_iTarget = -1;
		HaltActions();
		return true;
	}

	if (pPlayer->m_iIDTarget != iTarget)
		m_pMain->TargetSelect(iTarget, true);

	// The first attack skill that can go now.
	CMagicSkillMng* pMagic = m_pMain->m_pMagicSkillMng;
	std::vector<Genie::SkillChoice> skills;
	float fSkillRange = 0.0f; // the reach of the skills that could go if Genie got closer
	for (int iSkillID : m_Settings.attackSkills)
	{
		__TABLE_UPC_SKILL* pSkill = iSkillID > 0 ? CGameBase::s_pTbl_Skill.Find(iSkillID) : nullptr;
		if (pSkill == nullptr)
			continue;

		Genie::SkillChoice choice;
		choice.iID      = iSkillID;
		choice.bReady   = pMagic->GetCooldown(pSkill) < 0.0f;
		choice.bUsable  = m_pMain->m_pUISkillTreeDlg->HasIDSkill(iSkillID) && pMagic->CheckValidSkillMagic(pSkill);
		choice.bInRange = pSkill->iValidDist > 0 ? fDistance <= pSkill->iValidDist + pTarget->Radius() + 1.0f
												 : pPlayer->IsAttackableTarget(pTarget, false);
		auto held       = m_HeldSkills.find(iSkillID);
		choice.bHeld    = held != m_HeldSkills.end() && m_fNow < held->second;
		skills.push_back(choice);

		if (choice.bUsable && pSkill->iValidDist > 0)
			fSkillRange = std::max(fSkillRange, static_cast<float>(pSkill->iValidDist));
	}

	const int iSkillID = pMagic->IsCasting() ? -1 : Genie::FirstCastable(skills);
	if (iSkillID > 0)
	{
		StopMoving();
		CastAttack(pTarget, CGameBase::s_pTbl_Skill.Find(iSkillID));
		return true;
	}

	if (m_Settings.bBasicAttack)
	{
		if (pPlayer->IsAttackableTarget(pTarget, false))
		{
			StopMoving();
			if (!pPlayer->m_bAttackContinous)
				m_pMain->CommandEnableAttackContinous(true, pTarget);
		}
		else
		{
			MoveToTarget(pTarget);
		}
		return true;
	}

	// Skills only: get into their reach, then wait for one to be ready.
	if (fSkillRange > 0.0f && fDistance > fSkillRange)
		MoveToTarget(pTarget);
	else
		StopMoving();
	return true;
}

void CGenie::CastAttack(CPlayerNPC* pTarget, __TABLE_UPC_SKILL* pSkill)
{
	CMagicSkillMng* pMagic = m_pMain->m_pMagicSkillMng;
	const bool bArea       = pSkill->iTarget == SKILLMAGIC_TARGET_AREA_ENEMY || pSkill->iTarget == SKILLMAGIC_TARGET_AREA_FRIEND
					   || pSkill->iTarget == SKILLMAGIC_TARGET_AREA_ALL;

	// An area skill waits for a spot on the ground; Genie picks the target's, as a click would.
	if (bArea)
		m_pMain->m_vMouseSkillPos = pTarget->Position();

	if (!pMagic->MsgSend_MagicProcess(pTarget->IDNumber(), pSkill))
	{
		m_HeldSkills[static_cast<int>(pSkill->dwID)] = m_fNow + SKILL_HOLD;
		return;
	}

	if (bArea && pMagic->m_dwRegionMagicState == 1)
		pMagic->m_dwRegionMagicState = 2;
}

void CGenie::MoveToTarget(CPlayerNPC* pTarget)
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	StandUp();
	if (pPlayer->m_bTargetOrPosMove && pPlayer->m_iMoveTarget == pTarget->IDNumber())
		return;

	m_pMain->CommandMove(MD_FORWARD, true);
	pPlayer->SetMoveTargetID(pTarget->IDNumber());
}

void CGenie::StopMoving()
{
	if (CGameProcedure::s_pPlayer->m_bTargetOrPosMove || CGameProcedure::s_pPlayer->IsMovingNow())
		m_pMain->CommandMove(MD_STOP, true);
}

void CGenie::StandUp()
{
	if (CGameProcedure::s_pPlayer->m_bSitDown)
		m_pMain->CommandSitDown(false, false);
}

void CGenie::ReturnToOrigin()
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	if (DistanceXZ(pPlayer->Position(), m_vOrigin) <= ORIGIN_SLACK)
	{
		StopMoving();
		return;
	}

	StandUp();
	if (!pPlayer->m_bTargetOrPosMove || pPlayer->m_iMoveTarget >= 0)
		m_pMain->CommandMove(MD_FORWARD, true);
	pPlayer->SetMoveTargetPos(m_vOrigin);
}

void CGenie::OnLeaveGame()
{
	// The main scene is going (and when the game closes, the connection with it): nothing to halt,
	// only the server to tell.
	if (m_bRunning)
		SendGenie(GENIE_STOP);

	m_bRunning      = false;
	m_bWaitingStart = false;
	m_bAvailable    = false;
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
