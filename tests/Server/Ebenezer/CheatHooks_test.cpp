#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared/CheatSettings.h>
#include <shared-server/utilities.h>

#include <climits>
#include <filesystem>

using namespace Ebenezer;

/**
 * Tests the cheat multipliers and toggles Ebenezer applies: EXP, NP and coin gains, monster spell
 * damage, MP costs, consumables, durability, shop prices and the weight limit.
 */
class CheatHooksTest : public testing::Test
{
protected:
	static constexpr int16_t CLASS_ID  = 101;
	static constexpr int ITEM_SCROLL   = 379021000;
	static constexpr int ITEM_ARROW    = 391010000;
	static constexpr int ITEM_BOW      = 156210005;
	static constexpr int ITEM_POTION   = 389010000;
	static constexpr int NPC_ID        = 10001;
	static constexpr int SELLING_GROUP = 254;

	std::unique_ptr<TestApp> _app      = nullptr;
	std::shared_ptr<TestUser> _user    = nullptr;
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "CheatHooksTest.ini";
		std::filesystem::remove(_path);

		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);
		_app->_cheatSettingsPath = _path;
		_app->m_bCheatsAllowed   = true;
		ASSERT_TRUE(_app->AddCoefficientEntry(CLASS_ID));
		_app->AddLevelUpEntries(10, 100);

		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_SCROLL, .Weight = 1, .Durability = 1, .BuyPrice = 1000, .Countable = 1 }));
		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_ARROW, .Weight = 0, .Durability = 1, .BuyPrice = 10, .Countable = 1 }));
		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_POTION, .Weight = 100, .Durability = 1, .BuyPrice = 50, .Countable = 1 }));
		ASSERT_TRUE(_app->AddItemEntry({ .ID = ITEM_BOW,
			.Kind                            = 70,
			.Slot                            = 1,
			.Weight                          = 10,
			.Durability                      = 5000,
			.BuyPrice                        = 100000,
			.Countable                       = 0 }));

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);
		_user->SetState(CONNECTION_STATE_GAMESTART);
		_user->m_pUserData->m_sClass = CLASS_ID;
		_user->m_pUserData->m_bLevel = 1;
		_user->m_pUserData->m_bStr   = 60;
		_user->m_pUserData->m_bSta   = 60;
		_user->m_pUserData->m_sHp    = 100;
		_user->m_iMaxHp              = 100;
		_user->m_iMaxExp             = 100;
		_user->ClearInventory();
		_user->RecordSends();
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
		std::filesystem::remove(_path);
	}

	void SetCheats(Cheats::Settings settings)
	{
		ASSERT_TRUE(_app->SetCheatSettings(settings));
	}

	static Cheats::Settings On()
	{
		Cheats::Settings settings;
		settings.bOn = true;
		return settings;
	}

	// A WIZ_ITEM_TRADE buy request from the client, without the opcode byte.
	void Buy(int itemId, uint8_t pos, int16_t count)
	{
		char buffer[32] {};
		int index = 0;
		SetByte(buffer, 0x01, index);
		SetDWORD(buffer, SELLING_GROUP, index);
		SetShort(buffer, NPC_ID, index);
		SetDWORD(buffer, itemId, index);
		SetByte(buffer, pos, index);
		SetShort(buffer, count, index);
		_user->ItemTrade(buffer);
	}
};

// ×100 EXP carries a character through several levels at once, ending inside the last one.
TEST_F(CheatHooksTest, ExpMultiplierLevelsUpSeveralTimes)
{
	Cheats::Settings settings = On();
	settings.sExpPct          = 10000;
	SetCheats(settings);

	_user->ExpGain(5);

	EXPECT_EQ(_user->m_pUserData->m_bLevel, 6);
	EXPECT_EQ(_user->m_pUserData->m_iExp, 0);
}

// With cheats off, a gain is just the gain.
TEST_F(CheatHooksTest, ExpOffIsNormal)
{
	Cheats::Settings settings;
	settings.sExpPct = 10000;
	SetCheats(settings);

	_user->ExpGain(50);

	EXPECT_EQ(_user->m_pUserData->m_bLevel, 1);
	EXPECT_EQ(_user->m_pUserData->m_iExp, 50);
}

// NP gains are multiplied, and the client gets an NP update it can read.
TEST_F(CheatHooksTest, LoyaltyGainSendsTheClientFormat)
{
	Cheats::Settings settings = On();
	settings.sLoyaltyPct      = 1000;
	SetCheats(settings);

	_user->m_pUserData->m_iLoyalty        = 100;
	_user->m_pUserData->m_iLoyaltyMonthly = 7;
	_user->LoyaltyGain(3);

	EXPECT_EQ(_user->m_pUserData->m_iLoyalty, 130);

	const auto packets = _user->SentWith(WIZ_LOYALTY_CHANGE);
	ASSERT_EQ(packets.size(), 1u);
	ASSERT_EQ(packets[0].size(), 10u);
	int index = 1;
	EXPECT_EQ(GetByte(packets[0].data(), index), LOYALTY_CHANGE_NATIONAL);
	EXPECT_EQ(GetDWORD(packets[0].data(), index), 130u);
	EXPECT_EQ(GetDWORD(packets[0].data(), index), 7u);
}

TEST_F(CheatHooksTest, CoinPickupScales)
{
	Cheats::Settings settings = On();
	settings.sCoinPct         = 2500;
	SetCheats(settings);
	EXPECT_EQ(_user->CoinPickupAmount(32000), 800000);

	settings.bOn = false;
	SetCheats(settings);
	EXPECT_EQ(_user->CoinPickupAmount(32000), 32000);
}

// A monster's spell damage follows "damage taken"; heals and everything with cheats off don't.
TEST_F(CheatHooksTest, MonsterSpellDamageScales)
{
	Cheats::Settings settings = On();
	settings.sDamageTakenPct  = 200;
	SetCheats(settings);
	EXPECT_EQ(_app->CheatDamageTaken(-50), -100);
	EXPECT_EQ(_app->CheatDamageTaken(40), 40);

	settings.sDamageTakenPct = 0;
	SetCheats(settings);
	EXPECT_EQ(_app->CheatDamageTaken(-50), 0);

	settings.bOn = false;
	SetCheats(settings);
	EXPECT_EQ(_app->CheatDamageTaken(-50), -50);
}

// Free shops sell without taking coins; with it off, the coins are needed.
TEST_F(CheatHooksTest, FreeShopsChargeNothing)
{
	CNpc* npc = _app->CreateNPC(NPC_ID);
	ASSERT_NE(npc, nullptr);
	npc->m_iSellingGroup        = SELLING_GROUP;
	_app->m_bPointCheckFlag     = true;
	_user->m_iMaxWeight         = 1000;
	_user->m_pUserData->m_iGold = 0;

	Cheats::Settings settings   = On();
	settings.bFreeShops         = true;
	SetCheats(settings);

	Buy(ITEM_SCROLL, 0, 1);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum, ITEM_SCROLL);
	EXPECT_EQ(_user->m_pUserData->m_iGold, 0);

	settings.bFreeShops = false;
	SetCheats(settings);
	Buy(ITEM_SCROLL, 1, 1);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX + 1].nNum, 0);
}

TEST_F(CheatHooksTest, NoDurabilityLoss)
{
	auto& bow                  = _user->m_pUserData->m_sItemArray[RIGHTHAND];
	bow.nNum                   = ITEM_BOW;
	bow.sDuration              = 5000;

	Cheats::Settings settings  = On();
	settings.bNoDurabilityLoss = true;
	SetCheats(settings);
	_user->ItemWoreOut(DURABILITY_TYPE_ATTACK, 900);
	EXPECT_EQ(bow.sDuration, 5000);

	settings.bNoDurabilityLoss = false;
	SetCheats(settings);
	_user->ItemWoreOut(DURABILITY_TYPE_ATTACK, 900);
	EXPECT_LT(bow.sDuration, 5000);
}

// No weight limit raises the maximum; turning cheats off brings it back, keeps the bag, and tells
// the client.
TEST_F(CheatHooksTest, WeightLimitRaisedAndReturnsWhenOff)
{
	_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum   = ITEM_SCROLL;
	_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount = 500;

	Cheats::Settings settings                         = On();
	settings.bNoWeightLimit                           = true;
	SetCheats(settings);
	EXPECT_GT(_user->m_iMaxWeight, 1'000'000);

	_user->RecordSends();
	settings.bOn = false;
	SetCheats(settings);

	EXPECT_EQ(_user->m_iMaxWeight, (_user->m_pUserData->m_bStr + _user->m_sItemStr) * 50);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount, 500);
	EXPECT_EQ(_user->SentWith(WIZ_ITEM_MOVE).size(), 1u);
}

// No weight limit means none: a stack far heavier than the 32767 the client can show still fits.
TEST_F(CheatHooksTest, NoWeightLimitTakesHeavyStacks)
{
	CNpc* npc = _app->CreateNPC(NPC_ID);
	ASSERT_NE(npc, nullptr);
	npc->m_iSellingGroup      = SELLING_GROUP;
	_app->m_bPointCheckFlag   = true;

	Cheats::Settings settings = On();
	settings.bNoWeightLimit   = true;
	settings.bFreeShops       = true;
	SetCheats(settings);

	// 500 x 100 = 50,000
	Buy(ITEM_POTION, 0, 500);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum, ITEM_POTION);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount, 500);
	EXPECT_EQ(_user->GetMaxWeightForClient(), SHRT_MAX);
}

// Infinite MP: a skill needs and costs no MP. Off: it needs and costs its MP.
TEST_F(CheatHooksTest, InfiniteMpPaysNothing)
{
	_user->m_iMaxMp           = 300;
	_user->m_pUserData->m_sMp = 0;

	Cheats::Settings settings = On();
	settings.bInfiniteMp      = true;
	SetCheats(settings);
	EXPECT_TRUE(_user->CanPayMana(100));
	_user->PayMana(100);
	EXPECT_EQ(_user->m_pUserData->m_sMp, 0);

	settings.bInfiniteMp = false;
	SetCheats(settings);
	EXPECT_FALSE(_user->CanPayMana(100));
	_user->m_pUserData->m_sMp = 200;
	EXPECT_TRUE(_user->CanPayMana(100));
	_user->PayMana(100);
	EXPECT_EQ(_user->m_pUserData->m_sMp, 100);
}

// Kept consumables still have to be there; they just aren't used up.
TEST_F(CheatHooksTest, KeptConsumablesStillNeeded)
{
	auto& arrows              = _user->m_pUserData->m_sItemArray[SLOT_MAX];
	arrows.nNum               = ITEM_ARROW;
	arrows.sCount             = 50;

	Cheats::Settings settings = On();
	settings.bKeepConsumables = true;
	SetCheats(settings);
	EXPECT_TRUE(_user->UseConsumable(ITEM_ARROW, 1));
	EXPECT_EQ(arrows.sCount, 50);
	EXPECT_FALSE(_user->UseConsumable(ITEM_ARROW, 51));

	settings.bKeepConsumables = false;
	SetCheats(settings);
	EXPECT_TRUE(_user->UseConsumable(ITEM_ARROW, 1));
	EXPECT_EQ(arrows.sCount, 49);
}
