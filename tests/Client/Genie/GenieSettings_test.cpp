#include <gtest/gtest.h>

#include <Client/WarFare/GenieSettings.h>

#include <filesystem>
#include <string>

using namespace Genie;

// Every default the design names.
TEST(GenieSettings, DefaultsWhenMissing)
{
	CIni ini;
	Settings settings;
	settings.iRange = 0; // Load() must put it back
	Load(ini, settings);

	EXPECT_EQ(settings.attackSkills, (std::array<int, 8> {}));
	EXPECT_TRUE(settings.bBasicAttack);
	EXPECT_EQ(settings.buffSkills, (std::array<int, 8> {}));
	EXPECT_EQ(settings.healSkills, (std::array<int, 8> {}));
	EXPECT_EQ(settings.iHealPercent, 30);
	EXPECT_TRUE(settings.bHpPotion);
	EXPECT_EQ(settings.iHpPercent, 70);
	EXPECT_EQ(settings.iHpPotionID, 0);
	EXPECT_TRUE(settings.bMpPotion);
	EXPECT_EQ(settings.iMpPercent, 35);
	EXPECT_EQ(settings.iMpPotionID, 0);
	EXPECT_FALSE(settings.bStayPut);
	EXPECT_EQ(settings.iRange, 25);
	EXPECT_TRUE(settings.monsters.empty());
	EXPECT_TRUE(settings.bRepair);
	EXPECT_EQ(settings.iRepairPercent, 10);
	EXPECT_EQ(settings.loot, LootFilter {});
	EXPECT_FALSE(settings.bBuyPotions);
	EXPECT_EQ(settings.iKeepHpPotions, 50);
	EXPECT_EQ(settings.iKeepMpPotions, 50);
	EXPECT_FALSE(settings.bBuyArrows);
	EXPECT_EQ(settings.iArrowID, 0);
	EXPECT_EQ(settings.iKeepArrows, 500);
	EXPECT_EQ(settings.iCoinFloor, 0);
	EXPECT_FALSE(settings.potionMerchant.IsSet());
	EXPECT_FALSE(settings.arrowMerchant.IsSet());
	EXPECT_EQ(settings.town, TownRules {});
}

// Everything saved comes back the same.
TEST(GenieSettings, RoundTrip)
{
	Settings saved;
	saved.attackSkills   = { 105505, 105510, 0, 0, 0, 0, 0, 105560 };
	saved.bBasicAttack   = false;
	saved.buffSkills     = { 105520, 0, 0, 0, 0, 0, 0, 0 };
	saved.healSkills     = { 0, 0, 0, 0, 0, 0, 0, 112545 };
	saved.iHealPercent   = 45;
	saved.bHpPotion      = false;
	saved.iHpPercent     = 55;
	saved.iHpPotionID    = 389011000;
	saved.bMpPotion      = false;
	saved.iMpPercent     = 20;
	saved.iMpPotionID    = 389012000;
	saved.bStayPut       = true;
	saved.iRange         = 40;
	saved.monsters       = { "Worm", "Blood worm", "Bandicoot" };
	saved.bRepair        = false;
	saved.iRepairPercent = 25;
	saved.loot           = { .bConsumables = false,
				  .bEquipment              = true,
				  .bOther                  = false,
				  .bNormal                 = false,
				  .bMagic                  = true,
				  .bRare                   = false,
				  .bUnique                 = true,
				  .iMinSellPrice           = 1500 };
	saved.bBuyPotions    = true;
	saved.iKeepHpPotions = 120;
	saved.iKeepMpPotions = 80;
	saved.bBuyArrows     = true;
	saved.iArrowID       = 391010000;
	saved.iKeepArrows    = 2000;
	saved.iCoinFloor     = 1'500'000'000;
	saved.potionMerchant = { .iNpcID = 10432, .iTradeID = 255000 };
	saved.arrowMerchant  = { .iNpcID = 10433, .iTradeID = 253000 };
	saved.town           = {
				  .bBagFull = false, .bNoHpPotions = true, .bNoMpPotions = false, .bNoArrows = true
	};

	const std::filesystem::path path = std::filesystem::temp_directory_path()
									   / "GenieSettings_RoundTrip.ini";
	{
		CIni ini;
		Save(ini, saved);
		ini.Save(path);
	}

	CIni ini(path);
	Settings loaded;
	Load(ini, loaded);
	std::filesystem::remove(path);

	EXPECT_EQ(loaded, saved);
}

// Staying put defaults to on for a bow user (the caller says), until the player changes it.
TEST(GenieSettings, StayPutDefaultsByWeapon)
{
	// Separate inis: CIni writes a missing key's default into itself.
	Settings settings;
	CIni bowUser;
	Load(bowUser, settings, true);
	EXPECT_TRUE(settings.bStayPut);
	CIni other;
	Load(other, settings, false);
	EXPECT_FALSE(settings.bStayPut);

	CIni chosen;
	chosen.SetInt("Genie", "StayPut", 0);
	Load(chosen, settings, true);
	EXPECT_FALSE(settings.bStayPut);
}

// Values edited by hand out of range are pulled back in.
TEST(GenieSettings, ClampsOutOfRange)
{
	CIni ini;
	ini.SetInt("Genie", "Range", 200);
	ini.SetInt("Genie", "HpPercent", 0);
	ini.SetInt("Genie", "MpPercent", 150);
	for (int i = 0; i < 9; i++)
		ini.SetString("Genie", "Monster" + std::to_string(i), "Monster " + std::to_string(i));

	Settings settings;
	Load(ini, settings);
	EXPECT_EQ(settings.iRange, 80);
	EXPECT_EQ(settings.iHpPercent, 1);
	EXPECT_EQ(settings.iMpPercent, 99);
	EXPECT_EQ(settings.monsters.size(), 8u);

	ini.SetInt("Genie", "Range", 1);
	Load(ini, settings);
	EXPECT_EQ(settings.iRange, 3);
}

TEST(GenieSettings, PathPerCharacter)
{
	EXPECT_EQ(
		SettingsPath("base", "AppleOrc"), std::filesystem::path("base") / "Genie_AppleOrc.ini");
}
