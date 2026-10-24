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
#include "UITransactionDlg.h"
#include "UISkillTreeDlg.h"

#include <FileIO/PathResolver.h>
#include <N3Base/N3UIString.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{

constexpr D3DCOLOR COLOR_STARTED  = 0xff80ff80;
constexpr D3DCOLOR COLOR_STOPPED  = 0xffffff00;

constexpr int INDICATOR_WIDTH     = 110;
constexpr int INDICATOR_TOP       = 64;

constexpr float DECISION_INTERVAL = 0.3f;   // seconds between decisions
constexpr float ATTACKER_MEMORY   = 10.0f;  // how long a monster that attacked counts as attacking
constexpr float SKILL_HOLD        = 3.0f;   // how long a refused skill is held back
constexpr float ORIGIN_SLACK      = 3.0f;   // how close to its start spot Genie stands when idle
constexpr float PROGRESS_STEP     = 0.5f;   // getting this much closer to the target counts as progress
constexpr float LOOT_REPLY_WAIT   = 1.0f;   // a bundle that doesn't open in this long is in another region
constexpr float UPKEEP_REPLY_WAIT = 5.0f;   // a repair or buy not answered in this long is given up on
constexpr int BOW_ATTACK_SKILL    = 102003; // the bow's basic attack, which uses up arrows

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
	m_eTownReason    = Genie::StopReason::None;
	m_iRepairSlot    = -1;
	m_bRepairRefused = false;
	m_PendingBuy     = {};
	m_HpRestock = m_MpRestock = m_ArrowRestock = {};
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

	const std::string_view szMessage = Genie::StopMessage(eReason);
	if (!szMessage.empty() && eReason != Genie::StopReason::LeftGame)
		m_pMain->MsgOutput(std::string(szMessage), COLOR_STOPPED);
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

	Upkeep();

	const Genie::StopReason eTown = m_eTownReason != Genie::StopReason::None ? m_eTownReason : TownReasonNow();
	if (eTown != Genie::StopReason::None)
	{
		GoToTown(eTown);
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

void CGenie::GoToTown(Genie::StopReason eReason)
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	if (Genie::CanTown(pPlayer->m_InfoBase.iHP, pPlayer->m_InfoBase.iHPMax, pPlayer->m_bStun))
	{
		Stop(eReason);

		// As the /town command sends it.
		uint8_t byBuff[4];
		int iOffset = 0;
		CAPISocket::MP_AddWord(byBuff, iOffset, WIZ_HOME);
		CGameProcedure::s_pSocket->Send(byBuff, iOffset);
		return;
	}

	// /town needs half HP: no new fights until then, only healing and fending off what attacks.
	m_eTownReason = eReason;
	if (Survive() || Fight(true))
		return;

	StopMoving();
	if (!pPlayer->m_bSitDown)
		m_pMain->CommandSitDown(false, true);
}

int CGenie::FreeSlots() const
{
	int iFree = 0;
	for (__IconItemSkill* pItem : m_pMain->m_pUIInventory->m_pMyInvWnd)
	{
		if (pItem == nullptr)
			++iFree;
	}

	return iFree;
}

bool CGenie::UsesArrows() const
{
	// A bow, as CPlayerMySelf::Tick tells its archery attack.
	CUIInventory* pInv      = m_pMain->m_pUIInventory;
	__IconItemSkill* pLeft  = pInv->m_pMySlot[ITEM_SLOT_HAND_LEFT];
	__IconItemSkill* pRight = pInv->m_pMySlot[ITEM_SLOT_HAND_RIGHT];
	return (pLeft != nullptr && pLeft->pItemBasic != nullptr
			   && (pLeft->pItemBasic->byClass == ITEM_CLASS_BOW || pLeft->pItemBasic->byClass == ITEM_CLASS_BOW_LONG))
		   || (pRight != nullptr && pRight->pItemBasic != nullptr && pRight->pItemBasic->byClass == ITEM_CLASS_BOW_CROSS);
}

int CGenie::ArrowItemID() const
{
	if (m_Settings.iArrowID > 0)
		return m_Settings.iArrowID;

	// The arrows the bow's attack uses up.
	__TABLE_UPC_SKILL* pSkill = CGameBase::s_pTbl_Skill.Find(BOW_ATTACK_SKILL);
	return pSkill != nullptr ? static_cast<int>(pSkill->dwExhaustItem) : 0;
}

bool CGenie::CanRestock(int iItemID, const Genie::Merchant& merchant, const Genie::RestockState& state, bool bOn) const
{
	return bOn && iItemID > 0 && merchant.IsSet() && !state.bDisabled && CGameProcedure::s_pPlayer->m_InfoExt.iGold > m_Settings.iCoinFloor;
}

Genie::StopReason CGenie::TownReasonNow() const
{
	Genie::Supplies supplies;
	supplies.iFreeSlots    = FreeSlots();
	supplies.bUsesHpPotion = m_Settings.bHpPotion && m_Settings.iHpPotionID > 0;
	supplies.iHpPotions    = CountItem(m_Settings.iHpPotionID);
	supplies.bUsesMpPotion = m_Settings.bMpPotion && m_Settings.iMpPotionID > 0;
	supplies.iMpPotions    = CountItem(m_Settings.iMpPotionID);
	supplies.bUsesArrows   = UsesArrows();
	supplies.iArrows       = CountItem(ArrowItemID());

	// Out of something Genie can still buy isn't a reason to leave.
	if (CanRestock(m_Settings.iHpPotionID, m_Settings.potionMerchant, m_HpRestock, m_Settings.bBuyPotions))
		supplies.bUsesHpPotion = false;
	if (CanRestock(m_Settings.iMpPotionID, m_Settings.potionMerchant, m_MpRestock, m_Settings.bBuyPotions))
		supplies.bUsesMpPotion = false;
	if (CanRestock(ArrowItemID(), m_Settings.arrowMerchant, m_ArrowRestock, m_Settings.bBuyArrows))
		supplies.bUsesArrows = false;

	return Genie::TownReason(supplies, m_Settings.town);
}

void CGenie::Upkeep()
{
	Repair();
	Restock();
}

void CGenie::Repair()
{
	if (!m_Settings.bRepair || m_bRepairRefused)
		return;

	if (m_iRepairSlot >= 0)
	{
		if (m_fNow - m_fRepairSentAt < UPKEEP_REPLY_WAIT)
			return;
		m_iRepairSlot = -1;
	}

	// One equipped item at a time, below the durability percentage. The server repairs without a
	// blacksmith nearby, for the usual coins.
	for (int i = 0; i < ITEM_SLOT_COUNT; i++)
	{
		__IconItemSkill* pItem = m_pMain->m_pUIInventory->m_pMySlot[i];
		if (pItem == nullptr || pItem->pItemBasic == nullptr || pItem->pItemExt == nullptr)
			continue;

		const int iMax = pItem->pItemBasic->siMaxDurability + pItem->pItemExt->siMaxDurability;
		if (iMax <= 1 || !Genie::BelowPercent(pItem->iDurability, iMax, m_Settings.iRepairPercent))
			continue;

		uint8_t byBuff[8];
		int iOffset = 0;
		CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_ITEM_REPAIR);
		CAPISocket::MP_AddByte(byBuff, iOffset, 0x01); // equipped
		CAPISocket::MP_AddByte(byBuff, iOffset, static_cast<uint8_t>(i));
		CAPISocket::MP_AddDword(byBuff, iOffset, static_cast<uint32_t>(pItem->GetItemID()));
		CGameProcedure::s_pSocket->Send(byBuff, iOffset);

		m_iRepairSlot   = i;
		m_fRepairSentAt = m_fNow;
		return;
	}
}

bool CGenie::OnRepairResult(uint8_t byResult, int iGold)
{
	if (m_iRepairSlot < 0)
		return false;

	const int iSlot        = m_iRepairSlot;
	m_iRepairSlot          = -1;

	// The inventory's own handling only knows repairs made in its repair mode: Genie's goes here.
	__IconItemSkill* pItem = m_pMain->m_pUIInventory->m_pMySlot[iSlot];
	if (byResult != 0x01 || pItem == nullptr)
	{
		m_bRepairRefused = true;
		m_pMain->MsgOutput("Genie couldn't repair your gear (not enough coins?).", 0xffff3b3b);
		return true;
	}

	pItem->iDurability = pItem->pItemBasic->siMaxDurability + pItem->pItemExt->siMaxDurability;
	CGameProcedure::s_pPlayer->DurabilitySet(static_cast<e_ItemSlot>(iSlot), pItem->iDurability);
	CGameProcedure::s_pPlayer->m_InfoExt.iGold = iGold;
	m_pMain->m_pUIInventory->GoldUpdate();
	return true;
}

void CGenie::OnShopOpened(int iNpcID, int iTradeID)
{
	// What the shop sells, found as CUITransactionDlg::EnterTransactionState finds it.
	const int iOrg  = iTradeID / 1000;
	const int iExt  = iTradeID % 1000;
	bool bPotions   = false;
	bool bArrows    = false;
	const int iSize = CGameBase::s_pTbl_Items_Basic.GetSize();
	for (int i = 0; i < iSize && !(bPotions && bArrows); i++)
	{
		__TABLE_ITEM_BASIC* pItem = CGameBase::s_pTbl_Items_Basic.GetIndexedData(i);
		if (pItem == nullptr || pItem->bySellGroup != iOrg || pItem->byExtIndex >= MAX_ITEM_EXTENSION)
			continue;

		__TABLE_ITEM_EXT* pItemExt = CGameBase::s_pTbl_Items_Exts[pItem->byExtIndex].Find(iExt);
		if (pItemExt == nullptr || pItemExt->dwID != static_cast<uint32_t>(iExt))
			continue;

		bPotions |= pItem->byClass == ITEM_CLASS_POTION;
		bArrows  |= pItem->byClass == ITEM_CLASS_ARROW;
	}

	if (!bPotions && !bArrows)
		return;

	LoadSettings();
	if (bPotions)
		m_Settings.potionMerchant = { .iNpcID = iNpcID, .iTradeID = iTradeID };
	if (bArrows)
		m_Settings.arrowMerchant = { .iNpcID = iNpcID, .iTradeID = iTradeID };
	m_HpRestock = m_MpRestock = m_ArrowRestock = {};
	SaveSettings();

	m_pMain->MsgOutput(bPotions && bArrows ? "Genie will buy potions and arrows from this merchant."
					   : bPotions          ? "Genie will buy potions from this merchant."
										   : "Genie will buy arrows from this merchant.",
		COLOR_STARTED);
}

void CGenie::Restock()
{
	if (m_PendingBuy.iItemID != 0)
	{
		if (m_fNow - m_fBuySentAt < UPKEEP_REPLY_WAIT)
			return;
		m_PendingBuy = {};
	}

	if (m_Settings.bBuyPotions
		&& (Buy(m_Settings.iHpPotionID, m_Settings.potionMerchant, m_Settings.iKeepHpPotions, m_HpRestock)
			|| Buy(m_Settings.iMpPotionID, m_Settings.potionMerchant, m_Settings.iKeepMpPotions, m_MpRestock)))
		return;

	if (m_Settings.bBuyArrows && UsesArrows())
		Buy(ArrowItemID(), m_Settings.arrowMerchant, m_Settings.iKeepArrows, m_ArrowRestock);
}

bool CGenie::Buy(int iItemID, const Genie::Merchant& merchant, int iKeep, Genie::RestockState& state)
{
	if (iItemID <= 0 || !merchant.IsSet())
		return false;

	const int iHave = CountItem(iItemID);
	if (!Genie::WantsRestock(state, iHave, iKeep))
		return false;

	__TABLE_ITEM_BASIC* pItem  = CGameBase::s_pTbl_Items_Basic.Find(iItemID / 1000 * 1000);
	__TABLE_ITEM_EXT* pItemExt = pItem != nullptr && pItem->byExtIndex < MAX_ITEM_EXTENSION
									 ? CGameBase::s_pTbl_Items_Exts[pItem->byExtIndex].Find(iItemID % 1000)
									 : nullptr;
	if (pItemExt == nullptr)
		return false;

	// Onto its stack, or into an empty slot.
	CUIInventory* pInv = m_pMain->m_pUIInventory;
	int iPos = -1, iRoom = 0;
	for (int i = 0; i < MAX_ITEM_INVENTORY; i++)
	{
		if (pInv->m_pMyInvWnd[i] != nullptr && pInv->m_pMyInvWnd[i]->GetItemID() == iItemID)
		{
			iPos  = i;
			iRoom = MAX_ITEM_COUNT - pInv->m_pMyInvWnd[i]->iCount;
			break;
		}
	}
	for (int i = 0; i < MAX_ITEM_INVENTORY && iPos < 0; i++)
	{
		if (pInv->m_pMyInvWnd[i] == nullptr)
		{
			iPos  = i;
			iRoom = MAX_ITEM_COUNT;
		}
	}
	if (iPos < 0)
		return false;

	const int iPrice = pItem->iPrice * pItemExt->siPriceMultiply;
	const int iCount = Genie::RestockCount(iHave, iKeep, iPrice, CGameProcedure::s_pPlayer->m_InfoExt.iGold, m_Settings.iCoinFloor, iRoom);
	if (iCount <= 0)
		return false;

	// Exactly CUITransactionDlg::SendToServerBuyMsg's message.
	uint8_t byBuff[16];
	int iOffset = 0;
	CAPISocket::MP_AddByte(byBuff, iOffset, WIZ_ITEM_TRADE);
	CAPISocket::MP_AddByte(byBuff, iOffset, 0x01); // buy
	CAPISocket::MP_AddDword(byBuff, iOffset, static_cast<uint32_t>(merchant.iTradeID));
	CAPISocket::MP_AddShort(byBuff, iOffset, static_cast<int16_t>(merchant.iNpcID));
	CAPISocket::MP_AddDword(byBuff, iOffset, static_cast<uint32_t>(iItemID));
	CAPISocket::MP_AddByte(byBuff, iOffset, static_cast<uint8_t>(iPos));
	CAPISocket::MP_AddShort(byBuff, iOffset, static_cast<int16_t>(iCount));
	CGameProcedure::s_pSocket->Send(byBuff, iOffset);

	m_PendingBuy = { .iItemID = iItemID, .iPos = iPos, .iCount = iCount, .pState = &state };
	m_fBuySentAt = m_fNow;
	return true;
}

bool CGenie::OnTradeResult(uint8_t byResult, uint8_t /*byType*/, int iMoney)
{
	if (m_PendingBuy.iItemID == 0 || (m_pMain->m_pUITransactionDlg != nullptr && m_pMain->m_pUITransactionDlg->IsVisible()))
		return false;

	const PendingBuy buy      = m_PendingBuy;
	m_PendingBuy              = {};

	__TABLE_ITEM_BASIC* pItem = CGameBase::s_pTbl_Items_Basic.Find(buy.iItemID / 1000 * 1000);
	const std::string szName  = pItem != nullptr ? pItem->szName : std::string("it");
	if (byResult != 0x01)
	{
		// Most likely the saved merchant is gone (the world changed): no more of this one.
		if (buy.pState != nullptr)
			buy.pState->bDisabled = true;
		m_pMain->MsgOutput(fmt::format("Genie couldn't buy {}: open a shop that sells it to save a new merchant.", szName), 0xffff3b3b);
		return true;
	}

	// The shop window's own handling only knows its own buys: Genie's goes into the bag here.
	CGameProcedure::s_pPlayer->m_InfoExt.iGold = iMoney;
	CUIInventory* pInv                         = m_pMain->m_pUIInventory;
	__IconItemSkill* pStack                    = pInv->m_pMyInvWnd[buy.iPos];
	if (pStack != nullptr && pStack->GetItemID() == buy.iItemID)
		pStack->iCount += buy.iCount;
	else if (pStack == nullptr)
		m_pMain->m_pUIDroppedItemDlg->AddToItemTableToInventory(buy.iItemID, buy.iCount, buy.iPos);
	pInv->GoldUpdate();

	m_pMain->MsgOutput(fmt::format("Genie bought {} {}.", buy.iCount, szName), COLOR_STARTED);
	return true;
}

bool CGenie::Fight(bool bAttackersOnly)
{
	CPlayerMySelf* pPlayer = CGameProcedure::s_pPlayer;
	const __Vector3 vSelf  = pPlayer->Position();

	std::vector<Genie::Monster> monsters;
	for (const auto& [iID, pNPC] : CGameProcedure::s_pOPMgr->m_NPCs)
	{
		if (!IsMonster(pNPC) || m_Progress.IsSkipped(iID, m_fNow))
			continue;

		auto attacker = m_Attackers.find(iID);
		if (bAttackersOnly && (attacker == m_Attackers.end() || m_fNow - attacker->second >= ATTACKER_MEMORY))
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
