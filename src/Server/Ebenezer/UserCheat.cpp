#include "pch.h"
#include "EbenezerApp.h"
#include "User.h"

#include <shared/CheatSettings.h>
#include <shared/globals.h>
#include <shared/packets.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// OpenKO: the cheats menu (WIZ_CHEAT). The settings live in EbenezerApp; this file handles the
// client's requests.

namespace Ebenezer
{

static_assert(Cheats::MAX_GAIN == MAX_CURRENCY);
static_assert(Cheats::MAX_LEVEL == MAX_LEVEL);

namespace
{

// ITEM.ItemType of an item on a +level line (the client's "upgrade" attribute)
constexpr uint8_t ITEM_TYPE_UPGRADE = 5;

constexpr int MAX_POINTS            = 255;

bool CheatsOn(const EbenezerApp* main)
{
	return main->CheatSettings().bOn;
}

// Whether the user is in the middle of something a teleport would break.
bool IsBusy(const CUser* user)
{
	return user->m_bWarp != 0 || user->m_bZoneChangeFlag || user->m_sExchangeUser != -1;
}

std::string Lowercase(std::string_view text)
{
	std::string lower(text);
	std::ranges::transform(
		lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower;
}

// An NPC the player may teleport to: alive, in a teleport zone, and not a monster or an object.
bool IsTeleportNpc(const CNpc* npc)
{
	return npc != nullptr && npc->m_NpcState != NPC_DEAD
		   && Cheats::IsTeleportNpcType(npc->m_tNpcType) && Cheats::IsTeleportZone(npc->m_sCurZone);
}

constexpr size_t MAX_NPC_HITS = 9;

// Whether an item is on a +level line: the +0 or an upgrade.
bool OnPlusLine(const model::Item* item)
{
	return item->ID % 1000 == 0 || item->Type == ITEM_TYPE_UPGRADE;
}

} // namespace

std::optional<std::array<uint8_t, 5>> CUser::RaceBaseStats(uint8_t race)
{
	switch (race)
	{
		case KARUS_BIG:
		case KARUS_MIDDLE:
		case BABARIAN:
			return std::array<uint8_t, 5> { 65, 65, 60, 50, 50 };

		case KARUS_SMALL:
		case ELMORAD_WOMAN:
			return std::array<uint8_t, 5> { 50, 50, 70, 70, 50 };

		case KARUS_WOMAN:
			return std::array<uint8_t, 5> { 50, 60, 60, 70, 50 };

		case ELMORAD_MAN:
			return std::array<uint8_t, 5> { 60, 60, 70, 50, 50 };

		default:
			return std::nullopt;
	}
}

void CUser::ApplyStatPointReset()
{
	if (const auto baseStats = RaceBaseStats(m_pUserData->m_bRace); baseStats.has_value())
	{
		m_pUserData->m_bStr   = (*baseStats)[0];
		m_pUserData->m_bSta   = (*baseStats)[1];
		m_pUserData->m_bDex   = (*baseStats)[2];
		m_pUserData->m_bIntel = (*baseStats)[3];
		m_pUserData->m_bCha   = (*baseStats)[4];
	}

	m_pUserData->m_bPoints = static_cast<uint8_t>((m_pUserData->m_bLevel - 1) * 3 + 10);

	SetUserAbility();
	Send2AI_UserUpdateInfo();

	char sendBuffer[128] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CLASS_CHANGE, sendIndex);
	SetByte(sendBuffer, CLASS_RESET_STAT_REQ, sendIndex);
	SetByte(sendBuffer, CLASS_CHANGE_SUCCESS, sendIndex);
	SetDWORD(sendBuffer, m_pUserData->m_iGold, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bStr, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bSta, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bDex, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bIntel, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bCha, sendIndex);
	SetShort(sendBuffer, m_iMaxHp, sendIndex);
	SetShort(sendBuffer, m_iMaxMp, sendIndex);
	SetShort(sendBuffer, m_sTotalHit, sendIndex);
	SetShort(sendBuffer, GetMaxWeightForClient(), sendIndex);
	SetShort(sendBuffer, m_pUserData->m_bPoints, sendIndex);
	Send(sendBuffer, sendIndex);
}

void CUser::ApplySkillPointReset()
{
	m_pUserData->m_bstrSkill[0] = static_cast<uint8_t>(
		std::clamp((m_pUserData->m_bLevel - 9) * 2, 0, MAX_POINTS));
	for (int i = 1; i < 9; i++)
		m_pUserData->m_bstrSkill[i] = 0;

	char sendBuffer[128] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CLASS_CHANGE, sendIndex);
	SetByte(sendBuffer, CLASS_RESET_SKILL_REQ, sendIndex);
	SetByte(sendBuffer, CLASS_CHANGE_SUCCESS, sendIndex);
	SetDWORD(sendBuffer, m_pUserData->m_iGold, sendIndex);
	SetByte(sendBuffer, m_pUserData->m_bstrSkill[0], sendIndex);
	Send(sendBuffer, sendIndex);
}

void CUser::SendLevelChangeInfo()
{
	char sendBuffer[128] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_LEVEL_CHANGE, sendIndex);
	SetShort(sendBuffer, _socketId, sendIndex);
	SetByte(sendBuffer, m_pUserData->m_bLevel, sendIndex);
	SetByte(sendBuffer, m_pUserData->m_bPoints, sendIndex);
	SetByte(sendBuffer, m_pUserData->m_bstrSkill[0], sendIndex);
	SetDWORD(sendBuffer, m_iMaxExp, sendIndex);
	SetDWORD(sendBuffer, m_pUserData->m_iExp, sendIndex);
	SetShort(sendBuffer, m_iMaxHp, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_sHp, sendIndex);
	SetShort(sendBuffer, m_iMaxMp, sendIndex);
	SetShort(sendBuffer, m_pUserData->m_sMp, sendIndex);
	SetShort(sendBuffer, GetMaxWeightForClient(), sendIndex);
	SetShort(sendBuffer, GetCurrentWeightForClient(), sendIndex);
	m_pMain->Send_Region(sendBuffer, sendIndex, m_pUserData->m_bZone, m_RegionX, m_RegionZ);
}

uint8_t CUser::CheatSetLevel(int level)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (level < Cheats::MIN_LEVEL || level > Cheats::MAX_LEVEL)
		return CHEAT_BAD_VALUE;

	if (m_bResHpType == USER_DEAD)
		return CHEAT_DEAD;

	const int current = m_pUserData->m_bLevel;
	if (level > current)
	{
		// The points the game would have given on the way up (LevelChange's rule).
		const int statTotal = m_pUserData->m_bPoints + m_pUserData->m_bStr + m_pUserData->m_bSta
							  + m_pUserData->m_bDex + m_pUserData->m_bIntel + m_pUserData->m_bCha;
		const int statGrant    = std::max(0, 300 + 3 * (level - 1) - statTotal);
		m_pUserData->m_bPoints = static_cast<uint8_t>(
			std::min(MAX_POINTS, m_pUserData->m_bPoints + statGrant));

		int skillTotal = 0;
		for (int i = 0; i < 9; i++)
			skillTotal += m_pUserData->m_bstrSkill[i];

		const int skillGrant        = std::max(0, 2 * std::max(0, level - 9) - skillTotal);
		m_pUserData->m_bstrSkill[0] = static_cast<uint8_t>(
			std::min(MAX_POINTS, m_pUserData->m_bstrSkill[0] + skillGrant));

		m_pUserData->m_bLevel = static_cast<uint8_t>(level);
	}
	else if (level < current)
	{
		m_pUserData->m_bLevel = static_cast<uint8_t>(level);
		ApplyStatPointReset();
		ApplySkillPointReset();
	}

	m_pUserData->m_iExp = 0;

	// Recomputes everything, fills HP (telling AIServer) and MP, and tells the region. No Knight
	// Cash: that's earned by playing.
	LevelChange(static_cast<int16_t>(level), false);
	return CHEAT_OK;
}

uint8_t CUser::CheatAddPoints(uint8_t kind)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	uint8_t* points = nullptr;
	if (kind == 0)
		points = &m_pUserData->m_bPoints;
	else if (kind == 1)
		points = &m_pUserData->m_bstrSkill[0];
	else
		return CHEAT_BAD_VALUE;

	*points = static_cast<uint8_t>(std::min(MAX_POINTS, *points + Cheats::POINTS_PER_PRESS));
	SendLevelChangeInfo();
	return CHEAT_OK;
}

uint8_t CUser::CheatResetPoints(uint8_t kind)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (kind == 0)
		ApplyStatPointReset();
	else if (kind == 1)
		ApplySkillPointReset();
	else
		return CHEAT_BAD_VALUE;

	return CHEAT_OK;
}

uint8_t CUser::CheatRefill()
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (m_bResHpType == USER_DEAD || m_pUserData->m_sHp <= 0)
		return CHEAT_DEAD;

	HpChange(m_iMaxHp - m_pUserData->m_sHp);
	MSpChange(m_iMaxMp - m_pUserData->m_sMp);
	return CHEAT_OK;
}

uint8_t CUser::CheatSetCurrency(uint8_t kind, uint32_t value)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	switch (kind)
	{
		case 0:
		{
			if (!Cheats::IsStep(Cheats::COIN_STEPS, value))
				return CHEAT_BAD_VALUE;

			const int64_t delta = static_cast<int64_t>(value) - m_pUserData->m_iGold;
			if (delta > 0)
				GoldGain(static_cast<int>(delta));
			else if (delta < 0)
				GoldLose(static_cast<int>(-delta));
		}
		break;

		case 1:
			if (!Cheats::IsStep(Cheats::POINT_STEPS, value))
				return CHEAT_BAD_VALUE;

			m_pUserData->m_iLoyalty = static_cast<int32_t>(value);
			SendLoyaltyChange();
			break;

		case 2:
			if (!Cheats::IsStep(Cheats::POINT_STEPS, value))
				return CHEAT_BAD_VALUE;

			// Aujard keeps Knight Cash and never takes it below 0: take it all, then add the value.
			m_pMain->KnightCashAddToAgent(_socketId, m_strAccountID, -MAX_CURRENCY, false);
			if (value > 0)
				m_pMain->KnightCashAddToAgent(
					_socketId, m_strAccountID, static_cast<int>(value), true);
			break;

		default:
			return CHEAT_BAD_VALUE;
	}

	return CHEAT_OK;
}

void CUser::SendCheatItemVariants(int baseId)
{
	if (baseId <= 0 || baseId % 1000 != 0)
	{
		SendCheatResult(CHEAT_ITEM_VARIANTS, CHEAT_BAD_VALUE);
		return;
	}

	std::array<uint8_t, 125> bits {};
	for (int ext = 0; ext < 1000; ext++)
	{
		if (m_pMain->m_ItemTableMap.GetData(baseId + ext) != nullptr)
			bits.at(ext / 8) |= static_cast<uint8_t>(1 << (ext % 8));
	}

	char sendBuffer[160] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_ITEM_VARIANTS, sendIndex);
	SetDWORD(sendBuffer, baseId, sendIndex);
	for (uint8_t byte : bits)
		SetByte(sendBuffer, byte, sendIndex);
	Send(sendBuffer, sendIndex);
}

uint8_t CUser::CheatGiveItem(int itemId, int count)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (itemId == ITEM_GOLD || itemId >= ITEM_NO_TRADE)
		return CHEAT_NO_SUCH_ITEM;

	const model::Item* pTable = m_pMain->m_ItemTableMap.GetData(itemId);
	if (pTable == nullptr)
		return CHEAT_NO_SUCH_ITEM;

	if (pTable->Countable != 0)
	{
		if (count < 0 || count > UINT16_MAX
			|| !Cheats::IsStep(Cheats::COUNTABLE_STEPS, static_cast<uint16_t>(count)))
			return CHEAT_BAD_VALUE;

		return GiveItem(itemId, static_cast<int16_t>(count)) ? CHEAT_OK : CHEAT_BAG_FULL;
	}

	if (count < 0 || count > UINT16_MAX
		|| !Cheats::IsStep(Cheats::SINGLE_STEPS, static_cast<uint16_t>(count)))
		return CHEAT_BAD_VALUE;

	int given = 0;
	for (; given < count; given++)
	{
		const int pos = GetEmptySlot(itemId, 0);
		if (pos < 0 || pos >= HAVE_MAX || !GiveItem(itemId, 1))
			break;

		m_pUserData->m_sItemArray[SLOT_MAX + pos].nSerialNum = m_pMain->GenerateItemSerial();
	}

	return given > 0 ? CHEAT_OK : CHEAT_BAG_FULL;
}

uint8_t CUser::CheatSetPlus(uint8_t slot, int plus)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (slot >= SLOT_MAX || plus < 0 || plus > Cheats::MAX_PLUS)
		return CHEAT_BAD_VALUE;

	_ITEM_DATA& item = m_pUserData->m_sItemArray[slot];
	if (item.nNum == 0)
		return CHEAT_EMPTY_SLOT;

	const model::Item* pOld = m_pMain->m_ItemTableMap.GetData(item.nNum);
	const int ext           = item.nNum % 1000;
	const int newExt        = Cheats::ExtWithPlus(ext, plus);
	if (pOld == nullptr || newExt < 0)
		return CHEAT_NO_PLUS;

	const int newId         = item.nNum - ext + newExt;
	const model::Item* pNew = m_pMain->m_ItemTableMap.GetData(newId);
	if (pNew == nullptr || pNew->Kind != pOld->Kind || pNew->Slot != pOld->Slot || !OnPlusLine(pOld)
		|| !OnPlusLine(pNew))
		return CHEAT_NO_PLUS;

	// The same share of the new item's durability.
	int64_t durability = pNew->Durability;
	if (pOld->Durability > 0)
		durability = static_cast<int64_t>(item.sDuration) * pNew->Durability / pOld->Durability;

	item.nNum      = newId;
	item.sDuration = static_cast<int16_t>(std::clamp<int64_t>(durability, 0, pNew->Durability));

	SetSlotItemValue();
	SetUserAbility();
	SendStatsUpdate();

	char sendBuffer[32] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_DURATION, sendIndex);
	SetByte(sendBuffer, slot, sendIndex);
	SetShort(sendBuffer, item.sDuration, sendIndex);
	Send(sendBuffer, sendIndex);

	UserLookChange(slot, item.nNum, item.sDuration);
	Send2AI_UserUpdateInfo();

	sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_SET_PLUS, sendIndex);
	SetByte(sendBuffer, slot, sendIndex);
	SetDWORD(sendBuffer, item.nNum, sendIndex);
	SetShort(sendBuffer, item.sDuration, sendIndex);
	Send(sendBuffer, sendIndex);
	return CHEAT_OK;
}

uint8_t CUser::CheatRepairAll()
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	char sendBuffer[64] {};
	int sendIndex  = 0;
	bool wasBroken = false;

	for (int slot = 0; slot < SLOT_MAX; slot++)
	{
		_ITEM_DATA& item          = m_pUserData->m_sItemArray[slot];
		const model::Item* pTable = item.nNum != 0 ? m_pMain->m_ItemTableMap.GetData(item.nNum)
												   : nullptr;
		if (pTable == nullptr || item.sDuration == pTable->Durability)
			continue;

		wasBroken      = wasBroken || item.sDuration == 0;
		item.sDuration = pTable->Durability;

		sendIndex      = 0;
		SetByte(sendBuffer, WIZ_DURATION, sendIndex);
		SetByte(sendBuffer, slot, sendIndex);
		SetShort(sendBuffer, item.sDuration, sendIndex);
		Send(sendBuffer, sendIndex);
	}

	for (int pos = 0; pos < HAVE_MAX; pos++)
	{
		_ITEM_DATA& item          = m_pUserData->m_sItemArray[SLOT_MAX + pos];
		const model::Item* pTable = item.nNum != 0 ? m_pMain->m_ItemTableMap.GetData(item.nNum)
												   : nullptr;
		if (pTable == nullptr || pTable->Countable != 0 || item.sDuration == pTable->Durability)
			continue;

		item.sDuration = pTable->Durability;

		sendIndex      = 0;
		SetByte(sendBuffer, WIZ_ITEM_COUNT_CHANGE, sendIndex);
		SetShort(sendBuffer, 1, sendIndex); // one item
		SetByte(sendBuffer, 1, sendIndex);  // the bag
		SetByte(sendBuffer, pos, sendIndex);
		SetDWORD(sendBuffer, item.nNum, sendIndex);
		SetDWORD(sendBuffer, item.sCount, sendIndex);
		SetByte(sendBuffer, ITEM_COUNT_CHANGE_EXISTING, sendIndex);
		SetShort(sendBuffer, item.sDuration, sendIndex);
		Send(sendBuffer, sendIndex);
	}

	// A broken item adds nothing to the stats until it's repaired.
	if (wasBroken)
	{
		SetSlotItemValue();
		SetUserAbility();
		SendStatsUpdate();
		Send2AI_UserUpdateInfo();
	}

	sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_REPAIR_ALL, sendIndex);
	Send(sendBuffer, sendIndex);
	return CHEAT_OK;
}

void CUser::SendCheatAvailable()
{
	char sendBuffer[8] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_AVAILABLE, sendIndex);
	SetByte(sendBuffer, m_pMain->m_bCheatsAllowed ? 1 : 0, sendIndex);
	Send(sendBuffer, sendIndex);

	if (!m_pMain->m_bCheatsAllowed)
		return;

	char settingsBuffer[32] {};
	sendIndex = 0;
	SetByte(settingsBuffer, WIZ_CHEAT, sendIndex);
	SetByte(settingsBuffer, CHEAT_SETTINGS, sendIndex);
	EbenezerApp::WriteCheatSettings(settingsBuffer, m_pMain->CheatSettings(), sendIndex);
	Send(settingsBuffer, sendIndex);
}

void CUser::SendCheatResult(uint8_t opcode, uint8_t result)
{
	char sendBuffer[8] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_RESULT, sendIndex);
	SetByte(sendBuffer, opcode, sendIndex);
	SetByte(sendBuffer, result, sendIndex);
	Send(sendBuffer, sendIndex);
}

void CUser::ExpGain(int base)
{
	ExpChange(
		static_cast<int>(Cheats::ScaleGain(base, m_pMain->CheatPct(&Cheats::Settings::sExpPct))));

	// ExpChange levels up once per call.
	while (m_pUserData->m_iExp >= m_iMaxExp && m_pUserData->m_bLevel < MAX_LEVEL)
		ExpChange(0);
}

void CUser::LoyaltyGain(int base)
{
	CurrencyChange(m_pUserData->m_iLoyalty,
		static_cast<int>(
			Cheats::ScaleGain(base, m_pMain->CheatPct(&Cheats::Settings::sLoyaltyPct))));
	SendLoyaltyChange();
}

void CUser::SendLoyaltyChange()
{
	char sendBuffer[16] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_LOYALTY_CHANGE, sendIndex);
	SetByte(sendBuffer, LOYALTY_CHANGE_NATIONAL, sendIndex);
	SetInt(sendBuffer, m_pUserData->m_iLoyalty, sendIndex);
	SetInt(sendBuffer, m_pUserData->m_iLoyaltyMonthly, sendIndex);
	Send(sendBuffer, sendIndex);
}

int CUser::CoinPickupAmount(int count) const
{
	return static_cast<int>(
		Cheats::ScaleGain(count, m_pMain->CheatPct(&Cheats::Settings::sCoinPct)));
}

void CUser::SendStatsUpdate()
{
	char sendBuffer[64] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_ITEM_MOVE, sendIndex);
	SetByte(sendBuffer, 0x01, sendIndex);
	SetShort(sendBuffer, m_sTotalHit, sendIndex);
	SetShort(sendBuffer, m_sTotalAc, sendIndex);
	SetShort(sendBuffer, GetMaxWeightForClient(), sendIndex);
	SetShort(sendBuffer, m_iMaxHp, sendIndex);
	SetShort(sendBuffer, m_iMaxMp, sendIndex);
	SetShort(sendBuffer, m_sItemStr + m_sStrAmount, sendIndex);
	SetShort(sendBuffer, m_sItemSta + m_sStaAmount, sendIndex);
	SetShort(sendBuffer, m_sItemDex + m_sDexAmount, sendIndex);
	SetShort(sendBuffer, m_sItemIntel + m_sIntelAmount, sendIndex);
	SetShort(sendBuffer, m_sItemCham + m_sChaAmount, sendIndex);
	SetShort(sendBuffer, m_bFireR, sendIndex);
	SetShort(sendBuffer, m_bColdR, sendIndex);
	SetShort(sendBuffer, m_bLightningR, sendIndex);
	SetShort(sendBuffer, m_bMagicR, sendIndex);
	SetShort(sendBuffer, m_bDiseaseR, sendIndex);
	SetShort(sendBuffer, m_bPoisonR, sendIndex);
	Send(sendBuffer, sendIndex);
}

bool CUser::CanPayMana(int cost) const
{
	return m_pMain->CheatFlag(&Cheats::Settings::bInfiniteMp) || cost <= m_pUserData->m_sMp;
}

void CUser::PayMana(int cost)
{
	if (!m_pMain->CheatFlag(&Cheats::Settings::bInfiniteMp))
		MSpChange(-cost);
}

bool CUser::UseConsumable(int itemId, int count)
{
	if (m_pMain->CheatFlag(&Cheats::Settings::bKeepConsumables))
		return CheckExistItem(itemId, static_cast<int16_t>(count));

	return ItemCountChange(itemId, 1, count) >= 2;
}

uint8_t CUser::CheatTeleportZone(int zone)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (!Cheats::IsTeleportZone(zone))
		return CHEAT_BAD_VALUE;

	if (m_bResHpType == USER_DEAD)
		return CHEAT_DEAD;

	if (IsBusy(this))
		return CHEAT_BUSY;

	int16_t x = 0, z = 0;
	if (m_pMain->GetMapByID(zone) == nullptr || !GetStartPosition(&x, &z, zone))
		return CHEAT_NOT_FOUND;

	if (zone == m_pUserData->m_bZone)
		Warp(x, z);
	else
		ZoneChangeUnchecked(zone, x, z);

	return CHEAT_OK;
}

void CUser::SendCheatNpcSearch(std::string_view query)
{
	struct Hit
	{
		int16_t sId   = 0;
		int16_t sZone = 0;
		std::string name;
	};

	std::vector<Hit> hits;
	const std::string needle = Lowercase(query);
	if (needle.find_first_not_of(' ') != std::string::npos)
	{
		for (const auto& [npcId, pNpc] : m_pMain->m_NpcMap.m_UserTypeMap)
		{
			if (!IsTeleportNpc(pNpc))
				continue;

			std::string name(pNpc->m_strName);
			if (Lowercase(name).find(needle) != std::string::npos)
				hits.push_back({ pNpc->m_sNid, pNpc->m_sCurZone, std::move(name) });
		}
	}

	std::ranges::sort(hits, [](const Hit& a, const Hit& b)
		{ return a.name != b.name ? a.name < b.name : a.sId < b.sId; });
	if (hits.size() > MAX_NPC_HITS)
		hits.resize(MAX_NPC_HITS);

	char sendBuffer[512] {};
	int sendIndex = 0;
	SetByte(sendBuffer, WIZ_CHEAT, sendIndex);
	SetByte(sendBuffer, CHEAT_FIND_NPC, sendIndex);
	SetByte(sendBuffer, static_cast<uint8_t>(hits.size()), sendIndex);
	for (const Hit& hit : hits)
	{
		SetShort(sendBuffer, hit.sId, sendIndex);
		SetShort(sendBuffer, hit.sZone, sendIndex);
		SetString1(sendBuffer, hit.name, sendIndex);
	}

	Send(sendBuffer, sendIndex);
}

uint8_t CUser::CheatTeleportNpc(int npcId)
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (m_bResHpType == USER_DEAD)
		return CHEAT_DEAD;

	if (IsBusy(this))
		return CHEAT_BUSY;

	const CNpc* pNpc = m_pMain->m_NpcMap.GetData(npcId);
	if (!IsTeleportNpc(pNpc))
		return CHEAT_NOT_FOUND;

	// Beside the NPC, not on top of it.
	const float x = pNpc->m_fCurX + 2.0f;
	const float z = pNpc->m_fCurZ;
	if (pNpc->m_sCurZone == m_pUserData->m_bZone)
		Warp(x, z);
	else
		ZoneChangeUnchecked(pNpc->m_sCurZone, x, z);

	return CHEAT_OK;
}

int CUser::BuildKillAroundAI(char* buf) const
{
	int sendIndex = 0;
	SetByte(buf, AG_CHEAT_KILL_AROUND, sendIndex);
	SetShort(buf, _socketId, sendIndex);
	return sendIndex;
}

uint8_t CUser::CheatKillAround()
{
	if (!CheatsOn(m_pMain))
		return CHEAT_OFF;

	if (m_bResHpType == USER_DEAD)
		return CHEAT_DEAD;

	if (!m_pMain->IsAIServerConnected())
		return CHEAT_NO_AI;

	char sendBuffer[8] {};
	const int sendIndex = BuildKillAroundAI(sendBuffer);
	m_pMain->Send_AIServer(m_pUserData->m_bZone, sendBuffer, sendIndex);
	return CHEAT_OK;
}

void CUser::Cheat(char* pBuf, int len)
{
	if (GetState() != CONNECTION_STATE_GAMESTART || len < 1)
		return;

	int index            = 0;
	const uint8_t opcode = GetByte(pBuf, index);

	if (!m_pMain->m_bCheatsAllowed)
	{
		SendCheatResult(opcode, CHEAT_NOT_ALLOWED);
		return;
	}

	// Whether the request carries at least this many more bytes.
	auto has = [&](int bytes) { return len - index >= bytes; };

	switch (opcode)
	{
		case CHEAT_SETTINGS:
		{
			// Accepted settings are echoed to every player in the game.
			auto settings = EbenezerApp::ReadCheatSettings(pBuf, len, index);
			if (!settings.has_value() || !m_pMain->SetCheatSettings(*settings))
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
		}
		break;

		case CHEAT_SET_LEVEL:
			SendCheatResult(opcode, has(1) ? CheatSetLevel(GetByte(pBuf, index)) : CHEAT_BAD_VALUE);
			break;

		case CHEAT_ADD_POINTS:
			SendCheatResult(
				opcode, has(1) ? CheatAddPoints(GetByte(pBuf, index)) : CHEAT_BAD_VALUE);
			break;

		case CHEAT_RESET_POINTS:
			SendCheatResult(
				opcode, has(1) ? CheatResetPoints(GetByte(pBuf, index)) : CHEAT_BAD_VALUE);
			break;

		case CHEAT_REFILL:
			SendCheatResult(opcode, CheatRefill());
			break;

		case CHEAT_SET_CURRENCY:
		{
			if (!has(5))
			{
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
				break;
			}

			const uint8_t kind   = GetByte(pBuf, index);
			const uint32_t value = GetDWORD(pBuf, index);
			SendCheatResult(opcode, CheatSetCurrency(kind, value));
		}
		break;

		case CHEAT_ITEM_VARIANTS:
			if (has(4))
				SendCheatItemVariants(static_cast<int>(GetDWORD(pBuf, index)));
			else
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
			break;

		case CHEAT_GIVE_ITEM:
		{
			if (!has(6))
			{
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
				break;
			}

			const int itemId = static_cast<int>(GetDWORD(pBuf, index));
			const int count  = static_cast<uint16_t>(GetShort(pBuf, index));
			SendCheatResult(opcode, CheatGiveItem(itemId, count));
		}
		break;

		case CHEAT_SET_PLUS:
		{
			if (!has(2))
			{
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
				break;
			}

			const uint8_t slot = GetByte(pBuf, index);
			const uint8_t plus = GetByte(pBuf, index);
			SendCheatResult(opcode, CheatSetPlus(slot, plus));
		}
		break;

		case CHEAT_REPAIR_ALL:
			SendCheatResult(opcode, CheatRepairAll());
			break;

		case CHEAT_TELEPORT_ZONE:
			SendCheatResult(
				opcode, has(2) ? CheatTeleportZone(GetShort(pBuf, index)) : CHEAT_BAD_VALUE);
			break;

		case CHEAT_FIND_NPC:
		{
			const int queryLen = has(1) ? GetByte(pBuf, index) : -1;
			if (queryLen < 0 || queryLen > MAX_NPC_NAME_SIZE || !has(queryLen))
			{
				SendCheatResult(opcode, CHEAT_BAD_VALUE);
				break;
			}

			char query[MAX_NPC_NAME_SIZE + 1] {};
			GetString(query, pBuf, queryLen, index);
			SendCheatNpcSearch(query);
		}
		break;

		case CHEAT_TELEPORT_NPC:
			SendCheatResult(
				opcode, has(2) ? CheatTeleportNpc(GetShort(pBuf, index)) : CHEAT_BAD_VALUE);
			break;

		case CHEAT_KILL_AROUND:
			SendCheatResult(opcode, CheatKillAround());
			break;

		default:
			SendCheatResult(opcode, CHEAT_BAD_VALUE);
			break;
	}
}

} // namespace Ebenezer
