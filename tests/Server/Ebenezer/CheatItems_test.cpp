#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared/CheatSettings.h>
#include <shared-server/utilities.h>

#include <filesystem>

using namespace Ebenezer;

/**
 * Tests the cheats menu's item cheats: which +levels of an item exist, giving items, setting an
 * equipped item's +level, and repairing everything.
 */
class CheatItemsTest : public testing::Test
{
protected:
	static constexpr int16_t CLASS_ID = 101;
	static constexpr int ITEM_SCROLL  = 379021000;
	static constexpr int RAPTOR       = 156210000;

	std::unique_ptr<TestApp> _app     = nullptr;
	std::shared_ptr<TestUser> _user   = nullptr;
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "CheatItemsTest.ini";
		std::filesystem::remove(_path);

		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);
		_app->_cheatSettingsPath = _path;
		_app->m_bCheatsAllowed   = true;
		ASSERT_TRUE(_app->AddCoefficientEntry(CLASS_ID));

		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_SCROLL, .Weight = 1, .Durability = 1, .Countable = 1 }));
		AddRaptor(0, 5000, 0);
		AddRaptor(5, 5000, 5);
		AddRaptor(6, 5000, 5);
		AddRaptor(15, 5000, 5);
		AddRaptor(16, 6000, 5);
		AddRaptor(230, 5000, 5);

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);
		_user->SetState(CONNECTION_STATE_GAMESTART);
		_user->m_pUserData->m_sClass = CLASS_ID;
		_user->m_pUserData->m_sHp    = 100;
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

	void AddRaptor(int ext, int16_t durability, uint8_t itemType)
	{
		ASSERT_TRUE(_app->AddItemEntry({ .ID = RAPTOR + ext,
			.Kind                            = 70,
			.Slot                            = 1,
			.Weight                          = 10,
			.Durability                      = durability,
			.Countable                       = 0,
			.Type                            = itemType }));
	}

	void Equip(int itemId, int16_t durability)
	{
		_user->m_pUserData->m_sItemArray[RIGHTHAND].nNum      = itemId;
		_user->m_pUserData->m_sItemArray[RIGHTHAND].sDuration = durability;
	}

	void FillBag(int freeSlots)
	{
		for (int i = 0; i < HAVE_MAX - freeSlots; i++)
		{
			_user->m_pUserData->m_sItemArray[SLOT_MAX + i].nNum   = RAPTOR;
			_user->m_pUserData->m_sItemArray[SLOT_MAX + i].sCount = 1;
		}
	}

	int CountInBag(int itemId) const
	{
		int count = 0;
		for (int i = SLOT_MAX; i < SLOT_MAX + HAVE_MAX; i++)
		{
			if (_user->m_pUserData->m_sItemArray[i].nNum == itemId)
				count += _user->m_pUserData->m_sItemArray[i].sCount;
		}

		return count;
	}
};

// A bit per existing ID of the item.
TEST_F(CheatItemsTest, VariantsBitmap)
{
	_user->SendCheatItemVariants(RAPTOR);

	const auto packets = _user->SentWith(WIZ_CHEAT);
	ASSERT_EQ(packets.size(), 1u);
	ASSERT_EQ(packets[0].size(), 2u + 4u + 125u);
	int index = 1;
	EXPECT_EQ(GetByte(packets[0].data(), index), CHEAT_ITEM_VARIANTS);
	EXPECT_EQ(GetDWORD(packets[0].data(), index), static_cast<uint32_t>(RAPTOR));

	auto bit = [&](int ext)
	{ return (static_cast<uint8_t>(packets[0][6 + ext / 8]) >> (ext % 8)) & 1; };
	EXPECT_EQ(bit(0), 1);
	EXPECT_EQ(bit(5), 1);
	EXPECT_EQ(bit(230), 1);
	EXPECT_EQ(bit(1), 0);
	EXPECT_EQ(bit(7), 0);
	EXPECT_EQ(bit(999), 0);
}

TEST_F(CheatItemsTest, GiveCountable)
{
	EXPECT_EQ(_user->CheatGiveItem(ITEM_SCROLL, 50), CHEAT_OK);
	EXPECT_EQ(CountInBag(ITEM_SCROLL), 50);

	EXPECT_EQ(_user->CheatGiveItem(ITEM_SCROLL, 777), CHEAT_BAD_VALUE);
	EXPECT_EQ(CountInBag(ITEM_SCROLL), 50);
}

TEST_F(CheatItemsTest, GiveSingles)
{
	FillBag(3);
	EXPECT_EQ(_user->CheatGiveItem(RAPTOR + 5, 5), CHEAT_OK);
	EXPECT_EQ(CountInBag(RAPTOR + 5), 3);
	EXPECT_NE(_user->m_pUserData->m_sItemArray[SLOT_MAX + HAVE_MAX - 1].nSerialNum, 0);

	EXPECT_EQ(_user->CheatGiveItem(RAPTOR + 5, 1), CHEAT_BAG_FULL);
	EXPECT_EQ(_user->CheatGiveItem(RAPTOR + 7, 1), CHEAT_NO_SUCH_ITEM);
	EXPECT_EQ(_user->CheatGiveItem(900000000, 1), CHEAT_NO_SUCH_ITEM);
}

// The equipped Raptor +5 becomes +6 at the same share of its durability; another line keeps its
// line.
TEST_F(CheatItemsTest, SetPlus)
{
	Equip(RAPTOR + 5, 5000);
	EXPECT_EQ(_user->CheatSetPlus(RIGHTHAND, 6), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[RIGHTHAND].nNum, RAPTOR + 6);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[RIGHTHAND].sDuration, 5000);

	const auto packets = _user->SentWith(WIZ_CHEAT);
	ASSERT_EQ(packets.size(), 1u);
	int index = 1;
	EXPECT_EQ(GetByte(packets[0].data(), index), CHEAT_SET_PLUS);
	EXPECT_EQ(GetByte(packets[0].data(), index), RIGHTHAND);
	EXPECT_EQ(GetDWORD(packets[0].data(), index), static_cast<uint32_t>(RAPTOR + 6));
	EXPECT_EQ(GetShort(packets[0].data(), index), 5000);

	Equip(RAPTOR + 15, 2500);
	EXPECT_EQ(_user->CheatSetPlus(RIGHTHAND, 6), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[RIGHTHAND].nNum, RAPTOR + 16);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[RIGHTHAND].sDuration, 3000);

	EXPECT_EQ(_user->CheatSetPlus(RIGHTHAND, 7), CHEAT_NO_PLUS);
	EXPECT_EQ(_user->CheatSetPlus(RIGHTHAND, 0), CHEAT_NO_PLUS);
	EXPECT_EQ(_user->CheatSetPlus(RIGHTHAND, 11), CHEAT_BAD_VALUE);
	EXPECT_EQ(_user->CheatSetPlus(HEAD, 6), CHEAT_EMPTY_SLOT);
	EXPECT_EQ(_user->CheatSetPlus(SLOT_MAX, 6), CHEAT_BAD_VALUE);
}

TEST_F(CheatItemsTest, RepairAll)
{
	Equip(RAPTOR + 5, 0);
	_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum      = RAPTOR + 6;
	_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount    = 1;
	_user->m_pUserData->m_sItemArray[SLOT_MAX].sDuration = 100;

	EXPECT_EQ(_user->CheatRepairAll(), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[RIGHTHAND].sDuration, 5000);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].sDuration, 5000);
	EXPECT_EQ(_user->SentWith(WIZ_DURATION).size(), 1u);
	EXPECT_EQ(_user->SentWith(WIZ_ITEM_COUNT_CHANGE).size(), 1u);
	EXPECT_EQ(_user->SentWith(WIZ_CHEAT).size(), 1u);
}
