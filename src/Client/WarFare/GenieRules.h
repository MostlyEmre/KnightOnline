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
	int iMinSellPrice                        = 0;

	bool operator==(const LootFilter&) const = default;
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

/// \brief A monster by the NPC type the server sends (e_NpcType): monsters, event monsters, bosses,
/// dungeon monsters and the like (types under 10), not traps or the general type. Guards,
/// merchants, objects and camps are 10 and up.
inline bool IsMonsterType(uint8_t byNpcType)
{
	return byNpcType < 10 && byNpcType != 1 && byNpcType != 5;
}

struct Monster
{
	int iID           = -1;
	float fX          = 0.0f;
	float fZ          = 0.0f;
	bool bAttackingMe = false;
	bool bOnList      = true;
	bool bInReach     = true; // can be hit from where the player stands
};

/// \brief The monster to fight: the nearest one attacking the player, wherever it is; otherwise
/// the nearest one on the list within range of where Genie started. -1 for none. Staying put, only
/// monsters in reach count, and the range doesn't apply.
inline int ChooseTarget(
	std::span<const Monster> monsters, float fOriginX, float fOriginZ, float fRange, float fSelfX, float fSelfZ, bool bStayPut = false)
{
	auto distSq   = [](float fAX, float fAZ, float fBX, float fBZ) { return ((fAX - fBX) * (fAX - fBX)) + ((fAZ - fBZ) * (fAZ - fBZ)); };

	int iAttacker = -1, iTarget = -1;
	float fAttackerDist = std::numeric_limits<float>::max(), fTargetDist = std::numeric_limits<float>::max();
	for (const Monster& monster : monsters)
	{
		if (bStayPut && !monster.bInReach)
			continue;

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

		if (!monster.bOnList || (!bStayPut && distSq(monster.fX, monster.fZ, fOriginX, fOriginZ) > fRange * fRange))
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

/// \brief Skills only (no basic attack), with nothing castable this moment: whether to walk up to
/// the target, to get a ready skill within reach or, while all are cooling down, any. A melee
/// skill (no reach of its own) is in range once the target is within attack reach.
inline bool ShouldApproach(std::span<const SkillChoice> skills)
{
	bool bAnyUsable = false, bAnyInRange = false;
	for (const SkillChoice& skill : skills)
	{
		if (!skill.bUsable)
			continue;

		if (skill.bReady && !skill.bHeld && !skill.bInRange)
			return true;

		bAnyUsable   = true;
		bAnyInRange |= skill.bInRange;
	}

	return bAnyUsable && !bAnyInRange;
}

/// \brief Whether any attack skill can be paid for (MP, HP, weapon, items), cooling down or not
inline bool AnyAffordable(std::span<const SkillChoice> skills)
{
	return std::any_of(skills.begin(), skills.end(), [](const SkillChoice& skill) { return skill.bUsable; });
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

	bool operator==(const TownRules&) const = default;
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
	bool bShort    = false; // couldn't buy one last time (and said so)
};

inline bool WantsRestock(const RestockState& state, int iHave, int iKeep)
{
	return !state.bDisabled && iHave < iKeep;
}

/// \brief Whether a merchant sells an item: it's in the merchant's selling group, with the shop's
/// extension (as CUITransactionDlg::EnterTransactionState lists them)
inline bool IsSoldBy(int iItemSellGroup, int iItemID, int iTradeID)
{
	return iItemSellGroup == iTradeID / 1000 && iItemID % 1000 == iTradeID % 1000;
}

/// \brief Buying a supply back from where Genie stands
struct RestockOffer
{
	bool bOn       = false; // buying it is ticked
	bool bMerchant = false; // a merchant is saved for it
	bool bSold     = false; // and the merchant sells it
	int iUnitPrice = 0;
	int iRoom      = 0;     // room on its stack, or in an empty slot
};

/// \brief How many to buy now: back up to iKeep, from a merchant that sells it, within the room
/// and the coins above the floor
inline int RestockNow(const RestockOffer& offer, const RestockState& state, int iHave, int iKeep, int64_t iGold, int64_t iCoinFloor)
{
	if (!offer.bOn || !offer.bMerchant || !offer.bSold || !WantsRestock(state, iHave, iKeep))
		return 0;

	return RestockCount(iHave, iKeep, offer.iUnitPrice, iGold, iCoinFloor, offer.iRoom);
}

/// \brief Whether running out of a supply is no reason to go to town: Genie can buy it back
/// from none
inline bool CanRestockFromEmpty(const RestockOffer& offer, const RestockState& state, int iKeep, int64_t iGold, int64_t iCoinFloor)
{
	return RestockNow(offer, state, 0, iKeep, iGold, iCoinFloor) > 0;
}

/// \brief How long Genie walks to a drop in another region before giving up on it
constexpr float LOOT_WALK_TIME = 15.0f;

enum class DropWalk : uint8_t
{
	Walk,
	Wait,
	GiveUp
};

/// \brief What to do about a drop that didn't open from here (it's in another region): give up on
/// it when staying put, outside the hunting range of where Genie started, or after LOOT_WALK_TIME
/// of walking; wait while something attacks (the fight comes first); otherwise walk to it.
inline DropWalk WalkToDrop(bool bAttacked, float fDropFromOrigin, float fRange, float fWalkingFor, bool bStayPut = false)
{
	if (bStayPut || fDropFromOrigin > fRange || fWalkingFor >= LOOT_WALK_TIME)
		return DropWalk::GiveUp;

	return bAttacked ? DropWalk::Wait : DropWalk::Walk;
}

/// \brief The player's own item handling going on: windows that handle items, and an item move
/// waiting for the server (CN3UIBase::s_bWaitFromServer)
struct ItemWindows
{
	bool bLoot = false, bShop = false, bWarehouse = false, bTrade = false, bRepair = false, bWaiting = false;
};

/// \brief While the player handles items, Genie leaves them alone (no looting, repairs or
/// buying): its results would land in the player's window, or overwrite the move being made.
inline bool PlayerHandlesItems(const ItemWindows& windows)
{
	return windows.bLoot || windows.bShop || windows.bWarehouse || windows.bTrade || windows.bRepair || windows.bWaiting;
}

/// \brief Watches the current target for progress (getting closer, or hurting it). A target with
/// none for 10 s is given up and left alone for 30 s. The time adds up over its turns as the
/// target, so two targets taking turns don't keep each other from being given up.
class TargetProgress
{
public:
	static constexpr float STALL_TIME = 10.0f;
	static constexpr float SKIP_TIME  = 30.0f;

	/// \brief iTargetID is the target from now on
	void Reset(int iTargetID, float fNow)
	{
		if (m_iTargetID >= 0)
			m_Stalled[m_iTargetID] += fNow - m_fSince;

		m_iTargetID = iTargetID;
		m_fSince    = fNow;
	}

	void OnProgress(float fNow)
	{
		m_Stalled.erase(m_iTargetID);
		m_fSince = fNow;
	}

	bool ShouldSkip(float fNow) const
	{
		if (m_iTargetID < 0)
			return false;

		auto it = m_Stalled.find(m_iTargetID);
		return (it != m_Stalled.end() ? it->second : 0.0f) + (fNow - m_fSince) >= STALL_TIME;
	}

	int TargetID() const
	{
		return m_iTargetID;
	}

	void Skip(int iTargetID, float fNow)
	{
		m_SkippedUntil[iTargetID] = fNow + SKIP_TIME;
		m_Stalled.erase(iTargetID);
		if (iTargetID == m_iTargetID)
			m_iTargetID = -1;
	}

	bool IsSkipped(int iTargetID, float fNow) const
	{
		auto it = m_SkippedUntil.find(iTargetID);
		return it != m_SkippedUntil.end() && fNow < it->second;
	}

private:
	int m_iTargetID = -1;
	float m_fSince  = 0.0f;         // when the current target's run without progress started
	std::map<int, float> m_Stalled; // target -> time without progress in its earlier turns
	std::map<int, float> m_SkippedUntil;
};

/// \brief Whose a repair or buy result is
enum class ResultOwner : uint8_t
{
	Player,  // the game's own window handles it
	Genie,   // Genie's request, in time
	Dropped, // Genie's request that ran out of time: nobody expects it any more
};

/// \brief A repair or buy of Genie's, waiting for its result for up to 5 s
class PendingRequest
{
public:
	static constexpr float REPLY_WAIT = 5.0f;

	void Send(float fNow)
	{
		m_bWaiting = true;
		m_bLate    = false;
		m_fSentAt  = fNow;
	}

	/// \brief Gives up on the result after REPLY_WAIT; a result that still comes is dropped
	void Tick(float fNow)
	{
		if (m_bWaiting && fNow - m_fSentAt >= REPLY_WAIT)
		{
			m_bWaiting = false;
			m_bLate    = true;
		}
	}

	/// \brief Leaving the game: nothing waits any more
	void Cancel()
	{
		m_bWaiting = m_bLate = false;
	}

	bool IsWaiting() const
	{
		return m_bWaiting;
	}

	/// \brief Who a result that just came is for. bPlayerWindowOpen: the player has the window open
	/// that would handle one of their own.
	ResultOwner Claim(bool bPlayerWindowOpen)
	{
		if (m_bWaiting)
		{
			m_bWaiting = false;
			return ResultOwner::Genie;
		}

		if (m_bLate && !bPlayerWindowOpen)
		{
			m_bLate = false;
			return ResultOwner::Dropped;
		}

		return ResultOwner::Player;
	}

private:
	bool m_bWaiting = false;
	bool m_bLate    = false;
	float m_fSentAt = 0.0f;
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
