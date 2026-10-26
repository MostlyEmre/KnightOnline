#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"
#include "packet_structs.h"

#include "data/Item_test_data.h"
#include "data/ItemUpgrade_test_data.h"

#include <Ebenezer/ItemUpgradeRules.h>
#include <shared-server/utilities.h>

#include <array>
#include <cstring>

using namespace Ebenezer;

/**
 * Tests the Low and Middle Class scrolls, which only upgrade items of their class (the client's
 * "Item Grade"), and the boosted odds ([UPGRADE] BOOST: every possible upgrade succeeds).
 */

TEST(ItemUpgradeRules, ScrollClassLimits)
{
	EXPECT_EQ(
		ItemUpgradeRules::MaxItemGradeForScroll(379221000), 1); // Upgrade Scroll (Low Class item)
	EXPECT_EQ(ItemUpgradeRules::MaxItemGradeForScroll(379235000),
		1); // Dispell Scroll(Light) (Low Class item)
	EXPECT_EQ(ItemUpgradeRules::MaxItemGradeForScroll(379205000),
		2); // Upgrade Scroll (Middle Class item)
	EXPECT_EQ(ItemUpgradeRules::MaxItemGradeForScroll(379220000),
		2); // Dispell Scroll(Light) (Middle Class item)
	EXPECT_EQ(
		ItemUpgradeRules::MaxItemGradeForScroll(379016000), 0); // Upgrade Scroll (High Class Item)
	EXPECT_EQ(ItemUpgradeRules::MaxItemGradeForScroll(379021000), 0); // Blessed Upgrade Scroll
	EXPECT_EQ(ItemUpgradeRules::MaxItemGradeForScroll(0), 0);
}

TEST(ItemUpgradeRules, ScrollsFitItemClass)
{
	using ItemUpgradeRules::ScrollsFitItem;
	const std::array<int32_t, 8> low { 379221000 }, middle { 379205000 }, high { 379016000 },
		none {};

	EXPECT_TRUE(ScrollsFitItem(low, 1));
	EXPECT_FALSE(ScrollsFitItem(low, 2));
	EXPECT_FALSE(ScrollsFitItem(low, 0)); // no class: not a Low Class item

	EXPECT_TRUE(ScrollsFitItem(middle, 1));
	EXPECT_TRUE(ScrollsFitItem(middle, 2));
	EXPECT_FALSE(ScrollsFitItem(middle, 3));
	EXPECT_FALSE(ScrollsFitItem(middle, 0));

	EXPECT_TRUE(ScrollsFitItem(high, 3));
	EXPECT_TRUE(ScrollsFitItem(high, 0));
	EXPECT_TRUE(ScrollsFitItem(none, 0));
}

// Every chance becomes certain. 0% means the upgrade isn't possible (past the highest level),
// and stays that way.
TEST(ItemUpgradeRules, BoostedRates)
{
	using ItemUpgradeRules::BoostedGenRate;
	EXPECT_EQ(BoostedGenRate(0), 0);
	EXPECT_EQ(BoostedGenRate(1), 10000);
	EXPECT_EQ(BoostedGenRate(1999), 10000);
	EXPECT_EQ(BoostedGenRate(2000), 10000);
	EXPECT_EQ(BoostedGenRate(4999), 10000);
	EXPECT_EQ(BoostedGenRate(5000), 10000);
	EXPECT_EQ(BoostedGenRate(10000), 10000);
}

TEST(ItemUpgradeRules, FullRateIsCertain)
{
	using ItemUpgradeRules::RollSucceeds;
	EXPECT_TRUE(RollSucceeds(10000, 10000)); // the roll can reach 10000
	EXPECT_TRUE(RollSucceeds(8500, 8499));
	EXPECT_FALSE(RollSucceeds(8500, 8500));
	EXPECT_FALSE(RollSucceeds(0, 0));
}

class UpgradeScrollsTest : public testing::Test
{
protected:
	static constexpr uint16_t ANVIL_NPC_ID = 10001;
	static constexpr uint8_t ZONE_ID       = 0;

	static constexpr int HIGH_SCROLL       = 379016000; // Upgrade Scroll (High Class Item)
	static constexpr int LOW_SCROLL        = 379221000; // Upgrade Scroll (Low Class item)
	static constexpr int MIDDLE_SCROLL     = 379205000; // Upgrade Scroll (Middle Class item)
	static constexpr int DAGGER_1          = 110110001; // its recipe: 95%
	static constexpr int DAGGER_2          = 110110002; // 50%
	static constexpr int DAGGER_3          = 110110003; // 40%
	static constexpr int DAGGER_4          = 110110004;
	static constexpr int DAGGER_10         = 110110010; // the top level: its recipe is 0%
	static constexpr int BLESSED_SCROLL    = 379021000; // Blessed Upgrade Scroll

	std::unique_ptr<TestApp> _app;
	TestMap* _map                   = nullptr;
	std::shared_ptr<TestUser> _user = nullptr;

	void SetUp() override
	{
		_app = std::make_unique<TestApp>();

		for (const auto& itemModel : s_itemData)
			ASSERT_TRUE(_app->AddItemEntry(itemModel));

		// The Low and Middle Class scrolls, and Dagger (+4), as the port's game data adds them.
		model::Item scroll = *_app->m_ItemTableMap.GetData(HIGH_SCROLL);
		scroll.ID          = LOW_SCROLL;
		ASSERT_TRUE(_app->AddItemEntry(scroll));
		scroll.ID = MIDDLE_SCROLL;
		ASSERT_TRUE(_app->AddItemEntry(scroll));
		model::Item dagger = *_app->m_ItemTableMap.GetData(DAGGER_3);
		dagger.ID          = DAGGER_4;
		ASSERT_TRUE(_app->AddItemEntry(dagger));
		dagger.ID = DAGGER_10;
		ASSERT_TRUE(_app->AddItemEntry(dagger));

		// The whole table, plus the High Class upgrade recipes copied for the Low (+5000) and
		// Middle (+6000) Class scrolls.
		for (const auto& itemUpgradeModel : s_itemUpgradeData)
			ASSERT_TRUE(_app->AddItemUpgradeEntry(itemUpgradeModel));

		for (const auto& itemUpgradeModel : s_itemUpgradeData)
		{
			if (itemUpgradeModel.RequiredItem[0] != HIGH_SCROLL)
				continue;

			model::ItemUpgrade copy = itemUpgradeModel;
			copy.Index              = itemUpgradeModel.Index + 5000;
			copy.RequiredItem[0]    = LOW_SCROLL;
			ASSERT_TRUE(_app->AddItemUpgradeEntry(copy));
			copy.Index           = itemUpgradeModel.Index + 6000;
			copy.RequiredItem[0] = MIDDLE_SCROLL;
			ASSERT_TRUE(_app->AddItemUpgradeEntry(copy));
		}

		_map = _app->CreateMap(ZONE_ID);
		ASSERT_TRUE(_map != nullptr);

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);
		_user->SetState(CONNECTION_STATE_GAMESTART);
		ASSERT_TRUE(_map->Add(_user.get(), 0, 0));

		CNpc* anvil = _app->CreateNPC(ANVIL_NPC_ID);
		ASSERT_TRUE(anvil != nullptr);
		anvil->m_tNpcType = NPC_ANVIL;
		ASSERT_TRUE(_map->Add(anvil, 0, 0));

		_user->m_pUserData->m_iGold = 100'000'000;
	}

	void TearDown() override
	{
		_user.reset();
		_map = nullptr;
		_app.reset();
	}

	/// \brief The roll is myrand(0, myrand(9000, 10000)): the outer one gives iRoll
	static void SetRoll(int iRoll)
	{
		static int s_iRoll = 0;
		s_iRoll            = iRoll;
		myrand             = [](int min, int max) { return min == 0 ? s_iRoll : max; };
	}

	/// \brief Upgrades the item in slot 0 with the scroll in slot 1; returns the result byte
	uint8_t Upgrade(int iItemID, int iScrollID)
	{
		_ITEM_DATA& item   = _user->m_pUserData->m_sItemArray[SLOT_MAX + 0];
		_ITEM_DATA& scroll = _user->m_pUserData->m_sItemArray[SLOT_MAX + 1];
		item               = { .nNum = iItemID, .sDuration = 1, .sCount = 1 };
		scroll             = { .nNum = iScrollID, .sCount = 1 };

		ItemUpgradeProcessPacket packet {};
		packet.NpcID     = ANVIL_NPC_ID;
		packet.Item[0]   = { .ID = iItemID, .Pos = 0 };
		packet.Item[1]   = { .ID = iScrollID, .Pos = 1 };

		uint8_t byResult = 0xff;
		_user->ResetSend();
		_user->AddSendCallback(
			[&byResult](const char* pBuf, int len)
			{
				// A refusal comes alone; a roll comes after the gold change.
				if (len == sizeof(ItemUpgradeProcessErrorResponsePacket))
					byResult = reinterpret_cast<const ItemUpgradeProcessErrorResponsePacket*>(pBuf)
								   ->Result;
			});
		_user->AddSendCallback(
			[&byResult](const char* pBuf, int len)
			{
				ASSERT_EQ(len, sizeof(ItemUpgradeProcessResponseSuccessPacket));
				byResult = reinterpret_cast<const ItemUpgradeProcessResponseSuccessPacket*>(pBuf)
							   ->Result;
			});
		_user->AddSendCallback([](const char*, int) {});

		char sendBuffer[128] {};
		std::memcpy(sendBuffer, &packet, sizeof(packet));
		_user->ItemUpgradeProcess(sendBuffer);
		return byResult;
	}

	int ItemInSlot0() const
	{
		return _user->m_pUserData->m_sItemArray[SLOT_MAX + 0].nNum;
	}
};

TEST_F(UpgradeScrollsTest, LowClassScrollUpgradesLowClassItem)
{
	_app->m_ItemGradeMap[DAGGER_1] = 1;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, LOW_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
	EXPECT_EQ(ItemInSlot0(), DAGGER_2);
}

TEST_F(UpgradeScrollsTest, LowClassScrollRefusesMiddleClassItem)
{
	_app->m_ItemGradeMap[DAGGER_1] = 2;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, LOW_SCROLL), ITEM_UPGRADE_RESULT_NO_MATCH);
	EXPECT_EQ(ItemInSlot0(), DAGGER_1); // kept
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX + 1].nNum, LOW_SCROLL);
}

TEST_F(UpgradeScrollsTest, LowClassScrollRefusesItemWithoutClass)
{
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, LOW_SCROLL), ITEM_UPGRADE_RESULT_NO_MATCH);
	EXPECT_EQ(ItemInSlot0(), DAGGER_1);
}

TEST_F(UpgradeScrollsTest, MiddleClassScrollUpgradesMiddleClassItem)
{
	_app->m_ItemGradeMap[DAGGER_1] = 2;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, MIDDLE_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
	EXPECT_EQ(ItemInSlot0(), DAGGER_2);
}

TEST_F(UpgradeScrollsTest, MiddleClassScrollRefusesHighClassItem)
{
	_app->m_ItemGradeMap[DAGGER_1] = 3;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, MIDDLE_SCROLL), ITEM_UPGRADE_RESULT_NO_MATCH);
	EXPECT_EQ(ItemInSlot0(), DAGGER_1);
}

TEST_F(UpgradeScrollsTest, HighClassScrollTakesAnyClass)
{
	_app->m_ItemGradeMap[DAGGER_1] = 3;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_1, HIGH_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
	EXPECT_EQ(ItemInSlot0(), DAGGER_2);
}

// Dagger (+3) is a 40% recipe: boosted, it succeeds even on the highest roll.
TEST_F(UpgradeScrollsTest, BoostMakesFortyPercentCertain)
{
	_app->m_bUpgradeBoost = true;
	SetRoll(10000);
	EXPECT_EQ(Upgrade(DAGGER_3, HIGH_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
	EXPECT_EQ(ItemInSlot0(), DAGGER_4);
}

// Dagger (+2) is a 50% recipe.
TEST_F(UpgradeScrollsTest, BoostMakesFiftyPercentCertain)
{
	_app->m_bUpgradeBoost = true;
	SetRoll(10000);
	EXPECT_EQ(Upgrade(DAGGER_2, HIGH_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
	EXPECT_EQ(ItemInSlot0(), DAGGER_3);
}

TEST_F(UpgradeScrollsTest, WithoutBoostOfficialOdds)
{
	_app->m_bUpgradeBoost = false;
	SetRoll(4000);
	EXPECT_EQ(Upgrade(DAGGER_3, HIGH_SCROLL), ITEM_UPGRADE_RESULT_FAILED);

	SetRoll(3999);
	EXPECT_EQ(Upgrade(DAGGER_3, HIGH_SCROLL), ITEM_UPGRADE_RESULT_SUCCEEDED);
}

// An upgrade the game marks impossible (0%: Dagger (+10) is the top level) is refused: the item,
// the scroll and the coins stay.
TEST_F(UpgradeScrollsTest, BoostRefusesImpossibleUpgrade)
{
	_app->m_bUpgradeBoost = true;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_10, BLESSED_SCROLL), ITEM_UPGRADE_RESULT_NO_MATCH);
	EXPECT_EQ(ItemInSlot0(), DAGGER_10);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX + 1].nNum, BLESSED_SCROLL);
	EXPECT_EQ(_user->m_pUserData->m_iGold, 100'000'000);
}

// Without the boost, as officially: a certain failure, and the item is gone.
TEST_F(UpgradeScrollsTest, WithoutBoostImpossibleUpgradeBurns)
{
	_app->m_bUpgradeBoost = false;
	SetRoll(0);
	EXPECT_EQ(Upgrade(DAGGER_10, BLESSED_SCROLL), ITEM_UPGRADE_RESULT_FAILED);
	EXPECT_EQ(ItemInSlot0(), 0);
}
