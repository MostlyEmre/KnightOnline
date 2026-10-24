#ifndef CLIENT_WARFARE_GENIESETTINGS_H
#define CLIENT_WARFARE_GENIESETTINGS_H

#pragma once

// Genie's (auto-hunt's) settings, one file per character (Genie_<name>.ini), in the [Genie] section.

#include "GenieRules.h"

#include <shared/Ini.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace Genie
{

inline constexpr size_t SKILL_SLOTS   = 8;
inline constexpr size_t MONSTER_SLOTS = 8;
inline constexpr int RANGE_MIN        = 3;
inline constexpr int RANGE_MAX        = 80;

/// \brief A merchant Genie buys from: the NPC's server ID and its selling group (the trade ID
/// WIZ_TRADE_NPC brings when its shop opens)
struct Merchant
{
	int iNpcID   = -1;
	int iTradeID = 0;

	bool IsSet() const
	{
		return iNpcID >= 0 && iTradeID > 0;
	}

	bool operator==(const Merchant&) const = default;
};

struct Settings
{
	std::array<int, SKILL_SLOTS> attackSkills {};
	bool bBasicAttack = true;
	std::array<int, SKILL_SLOTS> buffSkills {};
	std::array<int, SKILL_SLOTS> healSkills {};
	int iHealPercent = 30;

	bool bHpPotion   = true;
	int iHpPercent   = 70;
	int iHpPotionID  = 0;
	bool bMpPotion   = true;
	int iMpPercent   = 35;
	int iMpPotionID  = 0;

	int iRange       = 25;
	std::vector<std::string> monsters; // up to MONSTER_SLOTS; empty: any monster

	bool bRepair       = true;
	int iRepairPercent = 10;

	LootFilter loot;

	bool bBuyPotions   = false;
	int iKeepHpPotions = 50;
	int iKeepMpPotions = 50;
	bool bBuyArrows    = false;
	int iArrowID       = 0;
	int iKeepArrows    = 500;
	int64_t iCoinFloor = 0;
	Merchant potionMerchant, arrowMerchant;

	TownRules town;

	bool operator==(const Settings&) const = default;
};

namespace detail
{

inline constexpr std::string_view SECTION = "Genie";

inline int GetClamped(CIni& ini, std::string_view key, int iDefault, int iMin, int iMax)
{
	return std::clamp(ini.GetInt(SECTION, key, iDefault), iMin, iMax);
}

inline void LoadSkills(CIni& ini, const std::string& prefix, std::array<int, SKILL_SLOTS>& skills)
{
	for (size_t i = 0; i < skills.size(); i++)
		skills[i] = std::max(0, ini.GetInt(SECTION, prefix + std::to_string(i), 0));
}

inline void SaveSkills(CIni& ini, const std::string& prefix, const std::array<int, SKILL_SLOTS>& skills)
{
	for (size_t i = 0; i < skills.size(); i++)
		ini.SetInt(SECTION, prefix + std::to_string(i), skills[i]);
}

} // namespace detail

/// \brief Reads the settings; missing keys get their defaults and values out of range are pulled
/// back in (range 3-80, percentages 1-99, at most 8 monsters).
inline void Load(CIni& ini, Settings& out)
{
	using detail::GetClamped;
	using detail::SECTION;
	constexpr int MAX = std::numeric_limits<int>::max();
	const Settings defaults;

	detail::LoadSkills(ini, "AttackSkill", out.attackSkills);
	out.bBasicAttack = ini.GetBool(SECTION, "BasicAttack", defaults.bBasicAttack);
	detail::LoadSkills(ini, "BuffSkill", out.buffSkills);
	detail::LoadSkills(ini, "HealSkill", out.healSkills);
	out.iHealPercent = GetClamped(ini, "HealPercent", defaults.iHealPercent, 1, 99);

	out.bHpPotion    = ini.GetBool(SECTION, "HpPotion", defaults.bHpPotion);
	out.iHpPercent   = GetClamped(ini, "HpPercent", defaults.iHpPercent, 1, 99);
	out.iHpPotionID  = GetClamped(ini, "HpPotionID", 0, 0, MAX);
	out.bMpPotion    = ini.GetBool(SECTION, "MpPotion", defaults.bMpPotion);
	out.iMpPercent   = GetClamped(ini, "MpPercent", defaults.iMpPercent, 1, 99);
	out.iMpPotionID  = GetClamped(ini, "MpPotionID", 0, 0, MAX);

	out.iRange       = GetClamped(ini, "Range", defaults.iRange, RANGE_MIN, RANGE_MAX);
	out.monsters.clear();
	for (size_t i = 0; i < MONSTER_SLOTS; i++)
	{
		std::string szName = ini.GetString(SECTION, "Monster" + std::to_string(i), "");
		if (szName.empty())
			break;
		out.monsters.push_back(std::move(szName));
	}

	out.bRepair               = ini.GetBool(SECTION, "Repair", defaults.bRepair);
	out.iRepairPercent        = GetClamped(ini, "RepairPercent", defaults.iRepairPercent, 1, 99);

	out.loot.bConsumables     = ini.GetBool(SECTION, "LootConsumables", true);
	out.loot.bEquipment       = ini.GetBool(SECTION, "LootEquipment", true);
	out.loot.bOther           = ini.GetBool(SECTION, "LootOther", true);
	out.loot.bNormal          = ini.GetBool(SECTION, "LootNormal", true);
	out.loot.bMagic           = ini.GetBool(SECTION, "LootMagic", true);
	out.loot.bRare            = ini.GetBool(SECTION, "LootRare", true);
	out.loot.bUnique          = ini.GetBool(SECTION, "LootUnique", true);
	out.loot.iMinSellPrice    = GetClamped(ini, "LootMinSellPrice", 0, 0, MAX);

	out.bBuyPotions           = ini.GetBool(SECTION, "BuyPotions", defaults.bBuyPotions);
	out.iKeepHpPotions        = GetClamped(ini, "KeepHpPotions", defaults.iKeepHpPotions, 0, MAX);
	out.iKeepMpPotions        = GetClamped(ini, "KeepMpPotions", defaults.iKeepMpPotions, 0, MAX);
	out.bBuyArrows            = ini.GetBool(SECTION, "BuyArrows", defaults.bBuyArrows);
	out.iArrowID              = GetClamped(ini, "ArrowID", 0, 0, MAX);
	out.iKeepArrows           = GetClamped(ini, "KeepArrows", defaults.iKeepArrows, 0, MAX);

	// Coins can go past an int; the floor is kept as text.
	const std::string szFloor = ini.GetString(SECTION, "CoinFloor", "0");
	int64_t iFloor            = 0;
	std::from_chars(szFloor.data(), szFloor.data() + szFloor.size(), iFloor);
	out.iCoinFloor              = std::max<int64_t>(0, iFloor);

	out.potionMerchant.iNpcID   = ini.GetInt(SECTION, "PotionMerchantNpc", -1);
	out.potionMerchant.iTradeID = ini.GetInt(SECTION, "PotionMerchantTrade", 0);
	out.arrowMerchant.iNpcID    = ini.GetInt(SECTION, "ArrowMerchantNpc", -1);
	out.arrowMerchant.iTradeID  = ini.GetInt(SECTION, "ArrowMerchantTrade", 0);

	out.town.bBagFull           = ini.GetBool(SECTION, "TownBagFull", true);
	out.town.bNoHpPotions       = ini.GetBool(SECTION, "TownNoHpPotions", true);
	out.town.bNoMpPotions       = ini.GetBool(SECTION, "TownNoMpPotions", true);
	out.town.bNoArrows          = ini.GetBool(SECTION, "TownNoArrows", true);
}

inline void Save(CIni& ini, const Settings& in)
{
	using detail::SECTION;

	detail::SaveSkills(ini, "AttackSkill", in.attackSkills);
	ini.SetInt(SECTION, "BasicAttack", in.bBasicAttack);
	detail::SaveSkills(ini, "BuffSkill", in.buffSkills);
	detail::SaveSkills(ini, "HealSkill", in.healSkills);
	ini.SetInt(SECTION, "HealPercent", in.iHealPercent);

	ini.SetInt(SECTION, "HpPotion", in.bHpPotion);
	ini.SetInt(SECTION, "HpPercent", in.iHpPercent);
	ini.SetInt(SECTION, "HpPotionID", in.iHpPotionID);
	ini.SetInt(SECTION, "MpPotion", in.bMpPotion);
	ini.SetInt(SECTION, "MpPercent", in.iMpPercent);
	ini.SetInt(SECTION, "MpPotionID", in.iMpPotionID);

	ini.SetInt(SECTION, "Range", in.iRange);
	for (size_t i = 0; i < MONSTER_SLOTS; i++)
		ini.SetString(SECTION, "Monster" + std::to_string(i), i < in.monsters.size() ? in.monsters[i] : "");

	ini.SetInt(SECTION, "Repair", in.bRepair);
	ini.SetInt(SECTION, "RepairPercent", in.iRepairPercent);

	ini.SetInt(SECTION, "LootConsumables", in.loot.bConsumables);
	ini.SetInt(SECTION, "LootEquipment", in.loot.bEquipment);
	ini.SetInt(SECTION, "LootOther", in.loot.bOther);
	ini.SetInt(SECTION, "LootNormal", in.loot.bNormal);
	ini.SetInt(SECTION, "LootMagic", in.loot.bMagic);
	ini.SetInt(SECTION, "LootRare", in.loot.bRare);
	ini.SetInt(SECTION, "LootUnique", in.loot.bUnique);
	ini.SetInt(SECTION, "LootMinSellPrice", in.loot.iMinSellPrice);

	ini.SetInt(SECTION, "BuyPotions", in.bBuyPotions);
	ini.SetInt(SECTION, "KeepHpPotions", in.iKeepHpPotions);
	ini.SetInt(SECTION, "KeepMpPotions", in.iKeepMpPotions);
	ini.SetInt(SECTION, "BuyArrows", in.bBuyArrows);
	ini.SetInt(SECTION, "ArrowID", in.iArrowID);
	ini.SetInt(SECTION, "KeepArrows", in.iKeepArrows);
	ini.SetString(SECTION, "CoinFloor", std::to_string(in.iCoinFloor));

	ini.SetInt(SECTION, "PotionMerchantNpc", in.potionMerchant.iNpcID);
	ini.SetInt(SECTION, "PotionMerchantTrade", in.potionMerchant.iTradeID);
	ini.SetInt(SECTION, "ArrowMerchantNpc", in.arrowMerchant.iNpcID);
	ini.SetInt(SECTION, "ArrowMerchantTrade", in.arrowMerchant.iTradeID);

	ini.SetInt(SECTION, "TownBagFull", in.town.bBagFull);
	ini.SetInt(SECTION, "TownNoHpPotions", in.town.bNoHpPotions);
	ini.SetInt(SECTION, "TownNoMpPotions", in.town.bNoMpPotions);
	ini.SetInt(SECTION, "TownNoArrows", in.town.bNoArrows);
}

/// \brief Where a character's settings live: <base>/Genie_<name>.ini
inline std::filesystem::path SettingsPath(const std::string& szBaseDir, const std::string& szCharName)
{
	return std::filesystem::path(szBaseDir) / ("Genie_" + szCharName + ".ini");
}

} // namespace Genie

#endif // CLIENT_WARFARE_GENIESETTINGS_H
