#ifndef SERVER_EBENEZER_ITEMUPGRADERULES_H
#define SERVER_EBENEZER_ITEMUPGRADERULES_H

#pragma once

#include <cstdint>
#include <span>

namespace Ebenezer::ItemUpgradeRules
{

/// \brief A rate of 10000 (100%) always succeeds
inline constexpr int GEN_RATE_CERTAIN          = 10000;

/// \brief The Low Class scrolls (Upgrade Scroll (Low Class item) to Dispell Scroll(Light)), and the
/// Middle Class ones. The 1.298 client sells them at Charon, next to the High Class scrolls.
inline constexpr int LOW_CLASS_SCROLL_FIRST    = 379221000;
inline constexpr int LOW_CLASS_SCROLL_LAST     = 379235000;
inline constexpr int MIDDLE_CLASS_SCROLL_FIRST = 379205000;
inline constexpr int MIDDLE_CLASS_SCROLL_LAST  = 379220000;

/// \brief The highest item class (the client's "Item Grade": 1 low, 2 middle, 3 high) a scroll
/// upgrades; 0 for a scroll without a limit
inline int MaxItemGradeForScroll(int itemId)
{
	if (itemId >= LOW_CLASS_SCROLL_FIRST && itemId <= LOW_CLASS_SCROLL_LAST)
		return 1;

	if (itemId >= MIDDLE_CLASS_SCROLL_FIRST && itemId <= MIDDLE_CLASS_SCROLL_LAST)
		return 2;

	return 0;
}

/// \brief Whether a recipe's scrolls take an item of this class: a Low Class scroll only low class
/// items, a Middle Class one middle and low. An item without a class (0) takes neither.
inline bool ScrollsFitItem(std::span<const int32_t> requiredItems, int itemGrade)
{
	for (int32_t itemId : requiredItems)
	{
		const int maxGrade = MaxItemGradeForScroll(itemId);
		if (maxGrade != 0 && (itemGrade < 1 || itemGrade > maxGrade))
			return false;
	}

	return true;
}

/// \brief [UPGRADE] BOOST's odds: every upgrade that's possible succeeds. 0% means the upgrade isn't
/// possible (past the highest level, or an accessory compound OpenKO-db leaves out), and stays 0;
/// with the boost, the anvil refuses those instead of destroying the item (CUser::ItemUpgrade).
inline int BoostedGenRate(int genRate)
{
	return genRate > 0 ? GEN_RATE_CERTAIN : 0;
}

/// \brief Whether an upgrade with this rate succeeds on this roll (0 to 10000)
inline bool RollSucceeds(int genRate, int roll)
{
	return genRate >= GEN_RATE_CERTAIN || genRate > roll;
}

} // namespace Ebenezer::ItemUpgradeRules

#endif // SERVER_EBENEZER_ITEMUPGRADERULES_H
