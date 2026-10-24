#ifndef CLIENT_WARFARE_GENIERULES_H
#define CLIENT_WARFARE_GENIERULES_H

#pragma once

// Genie's (auto-hunt's) decisions, kept free of the game so they can be tested on their own
// (tests/Client/Genie). CGenie gathers what the game knows into these types and acts on the answers.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <span>

namespace Genie
{

enum class StopReason : uint8_t
{
	None,
	ByPlayer,
	Died,
	BagFull,
	OutOfHpPotions,
	OutOfMpPotions,
	OutOfArrows,
	ServerRefused,
	NoReply,
	ZoneChanged,
	MovedByHand,
	LeftGame
};

/// \brief What the message window says when Genie stops (or couldn't start) for a reason
inline const char* StopMessage(StopReason eReason)
{
	switch (eReason)
	{
		case StopReason::ByPlayer:
		case StopReason::LeftGame:
			return "Genie stopped.";
		case StopReason::Died:
			return "Genie stopped: you died.";
		case StopReason::BagFull:
			return "Genie stopped: your bag is full.";
		case StopReason::OutOfHpPotions:
			return "Genie stopped: out of HP potions.";
		case StopReason::OutOfMpPotions:
			return "Genie stopped: out of MP potions.";
		case StopReason::OutOfArrows:
			return "Genie stopped: out of arrows.";
		case StopReason::ServerRefused:
			return "Genie couldn't start: the server refused.";
		case StopReason::NoReply:
			return "Genie couldn't start: the server didn't answer.";
		case StopReason::ZoneChanged:
			return "Genie stopped: you changed zones.";
		case StopReason::MovedByHand:
			return "Genie stopped: you took control.";
		default:
			return "";
	}
}

enum class ItemKind : uint8_t
{
	Consumable,
	Equipment,
	Other
};

enum class ItemGrade : uint8_t
{
	Normal,
	Magic,
	Rare,
	Unique
};

/// \brief An item's kind, from its e_ItemClass (__TABLE_ITEM_BASIC::byClass)
inline ItemKind KindOfItemClass(int iItemClass)
{
	// Potions, scrolls, other consumables, arrows.
	if (iItemClass == 97 || iItemClass == 98 || iItemClass == 255 || iItemClass == 120)
		return ItemKind::Consumable;

	// Weapons, shields, armor and accessories.
	if ((iItemClass >= 11 && iItemClass <= 80) || (iItemClass >= 91 && iItemClass <= 94) || iItemClass == 100 || iItemClass == 110
		|| iItemClass == 130 || (iItemClass >= 210 && iItemClass <= 240))
		return ItemKind::Equipment;

	return ItemKind::Other;
}

/// \brief An item's grade, from its e_ItemAttrib (__TABLE_ITEM_EXT::byMagicOrRare). Crafted items
/// count as rare, upgraded ones as normal.
inline ItemGrade GradeOfItemAttrib(int iMagicOrRare)
{
	switch (iMagicOrRare)
	{
		case 1:
			return ItemGrade::Magic;
		case 2:
		case 3:
			return ItemGrade::Rare;
		case 4:
			return ItemGrade::Unique;
		default:
			return ItemGrade::Normal;
	}
}

struct LootFilter
{
	bool bConsumables = true, bEquipment = true, bOther = true;
	bool bNormal = true, bMagic = true, bRare = true, bUnique = true;
	int iMinSellPrice = 0;
};

struct LootCandidate
{
	bool bGold       = false;
	ItemKind eKind   = ItemKind::Other;
	ItemGrade eGrade = ItemGrade::Normal;
	int iSellPrice   = 0;
};

/// \brief Whether to pick something up: coins always; an item if its kind is ticked, its grade too
/// (equipment only), and it sells for at least the minimum.
inline bool WantsLoot(const LootFilter& filter, const LootCandidate& item)
{
	if (item.bGold)
		return true;

	switch (item.eKind)
	{
		case ItemKind::Consumable:
			if (!filter.bConsumables)
				return false;
			break;

		case ItemKind::Equipment:
			if (!filter.bEquipment)
				return false;
			break;

		default:
			if (!filter.bOther)
				return false;
			break;
	}

	if (item.eKind == ItemKind::Equipment)
	{
		const bool bGrade = (item.eGrade == ItemGrade::Normal && filter.bNormal) || (item.eGrade == ItemGrade::Magic && filter.bMagic)
							|| (item.eGrade == ItemGrade::Rare && filter.bRare) || (item.eGrade == ItemGrade::Unique && filter.bUnique);
		if (!bGrade)
			return false;
	}

	return item.iSellPrice >= filter.iMinSellPrice;
}

struct Monster
{
	int iID           = -1;
	float fX          = 0.0f;
	float fZ          = 0.0f;
	bool bAttackingMe = false;
	bool bOnList      = true;
};

/// \brief The monster to fight: the nearest one attacking the player, wherever it is; otherwise
/// the nearest one on the list within range of where Genie started. -1 for none.
inline int ChooseTarget(std::span<const Monster> monsters, float fOriginX, float fOriginZ, float fRange, float fSelfX, float fSelfZ)
{
	auto distSq   = [](float fAX, float fAZ, float fBX, float fBZ) { return ((fAX - fBX) * (fAX - fBX)) + ((fAZ - fBZ) * (fAZ - fBZ)); };

	int iAttacker = -1, iTarget = -1;
	float fAttackerDist = std::numeric_limits<float>::max(), fTargetDist = std::numeric_limits<float>::max();
	for (const Monster& monster : monsters)
	{
		const float fDist = distSq(monster.fX, monster.fZ, fSelfX, fSelfZ);
		if (monster.bAttackingMe)
		{
			if (fDist < fAttackerDist)
			{
				fAttackerDist = fDist;
				iAttacker     = monster.iID;
			}
			continue;
		}

		if (!monster.bOnList || distSq(monster.fX, monster.fZ, fOriginX, fOriginZ) > fRange * fRange)
			continue;

		if (fDist < fTargetDist)
		{
			fTargetDist = fDist;
			iTarget     = monster.iID;
		}
	}

	return iAttacker >= 0 ? iAttacker : iTarget;
}

struct SkillChoice
{
	int iID       = 0;
	bool bReady   = false; // off cooldown
	bool bUsable  = false; // learned, and the MP, HP, weapon and items for it are there
	bool bInRange = false;
	bool bHeld    = false; // refused a moment ago
};

/// \brief The first skill in the list that can be cast now, or -1
inline int FirstCastable(std::span<const SkillChoice> skills)
{
	for (const SkillChoice& skill : skills)
	{
		if (skill.bReady && skill.bUsable && skill.bInRange && !skill.bHeld)
			return skill.iID;
	}

	return -1;
}

inline bool BelowPercent(int iValue, int iMax, int iPercent)
{
	return iMax > 0 && static_cast<int64_t>(iValue) * 100 < static_cast<int64_t>(iMax) * iPercent;
}

/// \brief The client's own rule for /town: at least half HP, and not stunned
inline bool CanTown(int iHp, int iHpMax, bool bStunned)
{
	return iHpMax > 0 && iHp * 2 >= iHpMax && !bStunned;
}

struct Supplies
{
	int iFreeSlots     = 28;
	bool bUsesHpPotion = false;
	int iHpPotions     = 0;
	bool bUsesMpPotion = false;
	int iMpPotions     = 0;
	bool bUsesArrows   = false;
	int iArrows        = 0;
};

struct TownRules
{
	bool bBagFull = true, bNoHpPotions = true, bNoMpPotions = true, bNoArrows = true;
};

/// \brief Why Genie has to go to town, if it does: a full bag, then no HP potions, no MP potions,
/// no arrows; each only when its rule is on and the supply is in use.
inline StopReason TownReason(const Supplies& supplies, const TownRules& rules)
{
	if (rules.bBagFull && supplies.iFreeSlots <= 0)
		return StopReason::BagFull;
	if (rules.bNoHpPotions && supplies.bUsesHpPotion && supplies.iHpPotions <= 0)
		return StopReason::OutOfHpPotions;
	if (rules.bNoMpPotions && supplies.bUsesMpPotion && supplies.iMpPotions <= 0)
		return StopReason::OutOfMpPotions;
	if (rules.bNoArrows && supplies.bUsesArrows && supplies.iArrows <= 0)
		return StopReason::OutOfArrows;
	return StopReason::None;
}

/// \brief A loot result (WIZ_ITEM_GET) that means the bag can't take more: 6 too heavy or too
/// many, 7 bag full
inline StopReason LootResultReason(uint8_t byResult)
{
	return byResult == 6 || byResult == 7 ? StopReason::BagFull : StopReason::None;
}

/// \brief How many to buy to get back up to iKeep, within the room on the stack and the coins
/// above the floor
inline int RestockCount(int iHave, int iKeep, int iUnitPrice, int64_t iGold, int64_t iCoinFloor, int iStackRoom)
{
	if (iUnitPrice <= 0 || iGold <= iCoinFloor)
		return 0;

	const int64_t iWanted     = std::max(0, iKeep - iHave);
	const int64_t iAffordable = (iGold - iCoinFloor) / iUnitPrice;
	return static_cast<int>(std::max<int64_t>(0, std::min({ iWanted, iAffordable, static_cast<int64_t>(iStackRoom) })));
}

struct RestockState
{
	bool bDisabled = false; // a buy was refused: no more this session
};

inline bool WantsRestock(const RestockState& state, int iHave, int iKeep)
{
	return !state.bDisabled && iHave < iKeep;
}

/// \brief Watches the current target for progress (getting closer, or hurting it). A target with
/// none for 10 s is given up and left alone for 30 s.
class TargetProgress
{
public:
	static constexpr float STALL_TIME = 10.0f;
	static constexpr float SKIP_TIME  = 30.0f;

	void Reset(int iTargetID, float fNow)
	{
		m_iTargetID     = iTargetID;
		m_fLastProgress = fNow;
	}

	void OnProgress(float fNow)
	{
		m_fLastProgress = fNow;
	}

	bool ShouldSkip(float fNow) const
	{
		return m_iTargetID >= 0 && fNow - m_fLastProgress >= STALL_TIME;
	}

	int TargetID() const
	{
		return m_iTargetID;
	}

	void Skip(int iTargetID, float fNow)
	{
		m_SkippedUntil[iTargetID] = fNow + SKIP_TIME;
		if (iTargetID == m_iTargetID)
			m_iTargetID = -1;
	}

	bool IsSkipped(int iTargetID, float fNow) const
	{
		auto it = m_SkippedUntil.find(iTargetID);
		return it != m_SkippedUntil.end() && fNow < it->second;
	}

private:
	int m_iTargetID       = -1;
	float m_fLastProgress = 0.0f;
	std::map<int, float> m_SkippedUntil;
};

/// \brief The server didn't answer GENIE_START in 5 s
inline bool StartTimedOut(float fSentAt, float fNow)
{
	return fNow - fSentAt >= 5.0f;
}

/// \brief Whether to sit down (or stay sitting) to get MP back: nothing attacking, and nothing to
/// cast, until MP is back to half
inline bool ShouldSitToRecover(bool bAttacked, bool bAnyCastable, int iMsp, int iMspMax, bool bSitting)
{
	if (bAttacked || iMspMax <= 0 || iMsp * 2 >= iMspMax)
		return false;

	return bSitting || !bAnyCastable;
}

} // namespace Genie

#endif // CLIENT_WARFARE_GENIERULES_H
