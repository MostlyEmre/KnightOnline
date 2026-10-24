#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"
#include "packet_structs.h"

#include "data/Item_test_data.h"

#include <Ebenezer/EXEC.h>

using namespace Ebenezer;

/**
 * Tests CUser .evt execution events (E) that follow the official server's implementation:
 * STATE_CHANGE, ZONE_CHANGE_PARTY and ROB_ALLITEM_PARTY.
 */
class EventExecTest : public testing::Test
{
protected:
	std::unique_ptr<TestApp> _app       = nullptr;
	std::shared_ptr<TestUser> _user     = nullptr;

	static constexpr int ITEM_HP_POTION = 389011000;

	void SetUp() override
	{
		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);

		for (const auto& itemModel : s_itemData)
			ASSERT_TRUE(_app->AddItemEntry(itemModel));

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);

		_user->SetState(CONNECTION_STATE_GAMESTART);
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
	}
};

// The opcodes parse with the official argument counts.
TEST_F(EventExecTest, Parse)
{
	EXEC stateChange;
	stateChange.Init();
	ASSERT_TRUE(stateChange.Parse("STATE_CHANGE 8 1", "test.evt", 1));
	EXPECT_EQ(stateChange.m_Exec, EXEC_STATE_CHANGE);
	EXPECT_EQ(stateChange.m_ExecInt[0], 8);
	EXPECT_EQ(stateChange.m_ExecInt[1], 1);

	EXEC zoneChangeParty;
	zoneChangeParty.Init();
	ASSERT_TRUE(zoneChangeParty.Parse("ZONE_CHANGE_PARTY 32 50 197", "test.evt", 2));
	EXPECT_EQ(zoneChangeParty.m_Exec, EXEC_ZONE_CHANGE_PARTY);
	EXPECT_EQ(zoneChangeParty.m_ExecInt[0], 32);
	EXPECT_EQ(zoneChangeParty.m_ExecInt[1], 50);
	EXPECT_EQ(zoneChangeParty.m_ExecInt[2], 197);

	EXEC robAllItemParty;
	robAllItemParty.Init();
	ASSERT_TRUE(robAllItemParty.Parse("ROB_ALLITEM_PARTY 379080000", "test.evt", 3));
	EXPECT_EQ(robAllItemParty.m_Exec, EXEC_ROB_ALLITEM_PARTY);
	EXPECT_EQ(robAllItemParty.m_ExecInt[0], 379080000);
}

// STATE_CHANGE 8 is the beginner helper event's sign-up (the "chicken" flag).
TEST_F(EventExecTest, StateChangeChicken)
{
	EXPECT_FALSE(_user->m_bIsChicken);

	_user->StateChangeServerDirect(8, 1);
	EXPECT_TRUE(_user->m_bIsChicken);

	_user->StateChangeServerDirect(8, 0);
	EXPECT_FALSE(_user->m_bIsChicken);

	// The official server knows state types 0 to 9 only.
	_user->StateChangeServerDirect(10, 1);
	EXPECT_FALSE(_user->m_bIsChicken);
}

// The official checks on the other state types.
TEST_F(EventExecTest, StateChangeChecks)
{
	_user->m_pUserData->m_sHp = 100;
	_user->m_bResHpType       = USER_STANDING;
	_user->StateChangeServerDirect(1, USER_SITDOWN);
	EXPECT_EQ(_user->m_bResHpType, USER_SITDOWN);

	// The dead can't sit down.
	_user->m_bResHpType = USER_DEAD;
	_user->StateChangeServerDirect(1, USER_SITDOWN);
	EXPECT_EQ(_user->m_bResHpType, USER_DEAD);

	// Abnormal state 5 is for operators only.
	_user->m_bAbnormalType = ABNORMAL_NORMAL;
	_user->StateChangeServerDirect(3, 5);
	EXPECT_EQ(_user->m_bAbnormalType, ABNORMAL_NORMAL);

	_user->m_pUserData->m_bAuthority = AUTHORITY_MANAGER;
	_user->StateChangeServerDirect(3, 5);
	EXPECT_EQ(_user->m_bAbnormalType, 5);
}

// ROB_ALLITEM_PARTY, without a party: the user loses the whole stack.
TEST_F(EventExecTest, RobAllItemParty)
{
	_user->ClearInventory();
	_user->ResetSend();
	_user->TestGiveItem(ITEM_HP_POTION, 5);

	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, sizeof(WeightChangePacket));
			auto packet = reinterpret_cast<const WeightChangePacket*>(pBuf);
			EXPECT_EQ(packet->Opcode, WIZ_WEIGHT_CHANGE);
		});
	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, sizeof(ItemCountChangePacket));
			auto packet = reinterpret_cast<const ItemCountChangePacket*>(pBuf);
			EXPECT_EQ(packet->Opcode, WIZ_ITEM_COUNT_CHANGE);
			EXPECT_EQ(packet->ItemId, ITEM_HP_POTION);
			EXPECT_EQ(packet->ItemCount, 0);
		});
	_user->RobAllItemParty(ITEM_HP_POTION);
	EXPECT_EQ(_user->GetPacketsSent(), 4u);

	for (int i = SLOT_MAX; i < SLOT_MAX + HAVE_MAX; i++)
		EXPECT_NE(_user->m_pUserData->m_sItemArray[i].nNum, ITEM_HP_POTION);

	// Nothing left to take: nothing is sent.
	EXPECT_FALSE(_user->RobAllItem(ITEM_HP_POTION));
	EXPECT_EQ(_user->GetPacketsSent(), 4u);
}
