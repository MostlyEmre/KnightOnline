#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared/CheatSettings.h>
#include <shared-server/utilities.h>

#include <filesystem>

using namespace Ebenezer;

/**
 * Tests the cheats menu's character cheats: setting the level, adding points, free resets,
 * refilling HP and MP, and setting coins, National Points and Knight Cash.
 */
class CheatCharacterTest : public testing::Test
{
protected:
	static constexpr int16_t CLASS_ID = 101;
	static constexpr uint8_t ZONE_ID  = 21;
	static constexpr int ITEM_BOW     = 156210005;

	std::unique_ptr<TestApp> _app     = nullptr;
	std::shared_ptr<TestUser> _user   = nullptr;
	TestMap* _map                     = nullptr;
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "CheatCharacterTest.ini";
		std::filesystem::remove(_path);

		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);
		_app->_cheatSettingsPath = _path;
		_app->m_bCheatsAllowed   = true;
		ASSERT_TRUE(_app->AddCoefficientEntry(CLASS_ID));
		_app->AddLevelUpEntries(Cheats::MAX_LEVEL, 1000);
		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_BOW, .Kind = 70, .Slot = 1, .Weight = 10, .Durability = 5000 }));

		_map = _app->CreateMap(ZONE_ID);
		ASSERT_NE(_map, nullptr);

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);
		_user->SetState(CONNECTION_STATE_GAMESTART);
		ASSERT_TRUE(_map->Add(_user.get(), 1, 1));

		auto* data           = _user->m_pUserData;
		data->m_sClass       = CLASS_ID;
		data->m_bRace        = KARUS_BIG;
		data->m_bLevel       = 10;
		// The race's stats (290) plus all 37 points of level 10 spent on strength
		data->m_bStr         = 65 + 37;
		data->m_bSta         = 65;
		data->m_bDex         = 60;
		data->m_bIntel       = 50;
		data->m_bCha         = 50;
		data->m_bPoints      = 0;
		data->m_bstrSkill[1] = 2;
		data->m_sHp          = 100;
		data->m_iExp         = 500;
		_user->m_iMaxHp      = 100;
		_user->m_iMaxExp     = 1000;
		_user->ClearInventory();

		Cheats::Settings settings;
		settings.bOn = true;
		_user->RecordSends();
		ASSERT_TRUE(_app->SetCheatSettings(settings));
		_user->RecordSends();
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
		std::filesystem::remove(_path);
	}

	int StatTotal() const
	{
		const auto* data = _user->m_pUserData;
		return data->m_bStr + data->m_bSta + data->m_bDex + data->m_bIntel + data->m_bCha;
	}
};

// Going up grants the points the game gives for those levels, puts EXP at the start of the new
// level, and gives no Knight Cash.
TEST_F(CheatCharacterTest, SetLevelUpGrantsPoints)
{
	EXPECT_EQ(_user->CheatSetLevel(30), CHEAT_OK);

	const auto* data = _user->m_pUserData;
	EXPECT_EQ(data->m_bLevel, 30);
	EXPECT_EQ(data->m_bPoints, 60);
	EXPECT_EQ(data->m_bstrSkill[0], 40);
	EXPECT_EQ(data->m_iExp, 0);
	EXPECT_EQ(StatTotal(), 327);
	EXPECT_EQ(_user->SentWith(WIZ_LEVEL_CHANGE).size(), 1u);
}

// Going down resets stats and skills for free, so the points fit the new level.
TEST_F(CheatCharacterTest, SetLevelDownResets)
{
	auto* data           = _user->m_pUserData;
	data->m_bLevel       = 60;
	data->m_bStr         = 65 + 187;
	data->m_bstrSkill[1] = 60;
	data->m_bstrSkill[2] = 42;

	EXPECT_EQ(_user->CheatSetLevel(20), CHEAT_OK);

	EXPECT_EQ(data->m_bLevel, 20);
	EXPECT_EQ(StatTotal(), 290);
	EXPECT_EQ(data->m_bStr, 65);
	EXPECT_EQ(data->m_bPoints, 19 * 3 + 10);
	EXPECT_EQ(data->m_bstrSkill[0], 22);
	for (int i = 1; i < 9; i++)
		EXPECT_EQ(data->m_bstrSkill[i], 0);
	EXPECT_EQ(data->m_iExp, 0);
}

TEST_F(CheatCharacterTest, SetLevelRefusesOutOfRange)
{
	EXPECT_EQ(_user->CheatSetLevel(0), CHEAT_BAD_VALUE);
	EXPECT_EQ(_user->CheatSetLevel(81), CHEAT_BAD_VALUE);
	EXPECT_EQ(_user->m_pUserData->m_bLevel, 10);

	Cheats::Settings settings;
	ASSERT_TRUE(_app->SetCheatSettings(settings));
	EXPECT_EQ(_user->CheatSetLevel(30), CHEAT_OFF);
	EXPECT_EQ(_user->m_pUserData->m_bLevel, 10);
}

TEST_F(CheatCharacterTest, AddPointsCapsAt255)
{
	_user->m_pUserData->m_bPoints = 250;
	EXPECT_EQ(_user->CheatAddPoints(0), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bPoints, 255);
	EXPECT_EQ(_user->CheatAddPoints(0), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bPoints, 255);

	EXPECT_EQ(_user->CheatAddPoints(1), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bstrSkill[0], 10);
	EXPECT_EQ(_user->CheatAddPoints(2), CHEAT_BAD_VALUE);
}

// The free reset works with gear on, where the normal request refuses.
TEST_F(CheatCharacterTest, ResetStatsWithGearOn)
{
	_user->m_pUserData->m_sItemArray[RIGHTHAND].nNum      = ITEM_BOW;
	_user->m_pUserData->m_sItemArray[RIGHTHAND].sDuration = 5000;

	_user->StatPointResetRequest(true);
	EXPECT_EQ(_user->m_pUserData->m_bStr, 65 + 37);

	EXPECT_EQ(_user->CheatResetPoints(0), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bStr, 65);
	EXPECT_EQ(_user->m_pUserData->m_bPoints, 9 * 3 + 10);

	EXPECT_EQ(_user->CheatResetPoints(1), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bstrSkill[0], 2);
	EXPECT_EQ(_user->m_pUserData->m_bstrSkill[1], 0);
}

TEST_F(CheatCharacterTest, Refill)
{
	auto* data      = _user->m_pUserData;
	_user->m_iMaxHp = 500;
	_user->m_iMaxMp = 300;
	data->m_sHp     = 1;
	data->m_sMp     = 0;

	EXPECT_EQ(_user->CheatRefill(), CHEAT_OK);
	EXPECT_EQ(data->m_sHp, 500);
	EXPECT_EQ(data->m_sMp, 300);

	_user->m_bResHpType = USER_DEAD;
	EXPECT_EQ(_user->CheatRefill(), CHEAT_DEAD);
}

TEST_F(CheatCharacterTest, SetCurrency)
{
	_user->m_pUserData->m_iGold = 5000;
	EXPECT_EQ(_user->CheatSetCurrency(0, 1'000'000), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_iGold, 1'000'000);

	auto packets = _user->SentWith(WIZ_GOLD_CHANGE);
	ASSERT_EQ(packets.size(), 1u);
	int index = 1;
	EXPECT_EQ(GetByte(packets[0].data(), index), GOLD_CHANGE_GAIN);
	EXPECT_EQ(GetDWORD(packets[0].data(), index), 995'000u);

	EXPECT_EQ(_user->CheatSetCurrency(0, 0), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_iGold, 0);
	EXPECT_EQ(_user->CheatSetCurrency(0, 777), CHEAT_BAD_VALUE);

	EXPECT_EQ(_user->CheatSetCurrency(1, 10'000), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_iLoyalty, 10'000);
	packets = _user->SentWith(WIZ_LOYALTY_CHANGE);
	ASSERT_EQ(packets.size(), 1u);
	index = 2;
	EXPECT_EQ(GetDWORD(packets[0].data(), index), 10'000u);

	EXPECT_EQ(_user->CheatSetCurrency(2, 1'000), CHEAT_OK);
	EXPECT_EQ(_user->CheatSetCurrency(3, 1'000), CHEAT_BAD_VALUE);
}
