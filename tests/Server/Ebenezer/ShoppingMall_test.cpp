#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"
#include "packet_structs.h"

#include <shared-server/utilities.h>

#include <cstring>

using namespace Ebenezer;

/**
 * Tests the Power-up Store (WIZ_SHOPPING_MALL): the official server's checks, and the Knight Cash
 * answers from Aujard, which the tests hand the user themselves.
 */
class ShoppingMallTest : public testing::Test
{
protected:
	std::unique_ptr<TestApp> _app                = nullptr;
	std::shared_ptr<TestUser> _user              = nullptr;

	static constexpr int ITEM_SCROLL             = 800003000; // not countable
	static constexpr int ITEM_RESURRECTION       = 800041000; // countable
	static constexpr int ITEM_NOT_FOR_SALE       = 389011000;
	static constexpr int16_t RESURRECTION_COUNT  = 5;
	static constexpr int PRICE_SCROLL            = 300;
	static constexpr int PRICE_RESURRECTION      = 200;
	static constexpr char ACCOUNT[]              = "testaccount";
	static constexpr int OPEN_FAILED_SIZE        = 4;
	static constexpr int BUY_RESULT_SIZE         = 9;
	static constexpr int KNIGHT_CASH_PACKET_SIZE = 10;
	static constexpr int STORE_ITEM_SIZE         = 10;
	static constexpr int INVENTORY_SLOT_SIZE     = 11;

	void SetUp() override
	{
		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);

		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_SCROLL, .Weight = 1, .Durability = 30, .Countable = 0 }));
		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_RESURRECTION, .Weight = 1, .Durability = 1, .Countable = 1 }));
		ASSERT_TRUE(_app->AddItemEntry(
			{ .ID = ITEM_NOT_FOR_SALE, .Weight = 1, .Durability = 1, .Countable = 1 }));
		_app->m_ShoppingMallItems = {
			{ .itemId = ITEM_SCROLL, .count = 1, .price = PRICE_SCROLL },
			{ .itemId  = ITEM_RESURRECTION,
				.count = RESURRECTION_COUNT,
				.price = PRICE_RESURRECTION },
		};

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);

		_user->SetState(CONNECTION_STATE_GAMESTART);
		std::memcpy(_user->m_strAccountID, ACCOUNT, sizeof(ACCOUNT));
		_user->m_pUserData->m_sHp   = 100;
		_user->m_pUserData->m_bZone = 21;
		_user->m_iMaxWeight         = 1000;
		_user->ClearInventory();
		_user->ResetSend();
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
	}

	// A WIZ_SHOPPING_MALL request from the client.
	void Request(uint8_t subOpcode, int itemId = 0, int16_t count = 0)
	{
		char buffer[16] {};
		int index = 0;
		SetByte(buffer, subOpcode, index);
		if (subOpcode == STORE_BUY)
		{
			SetInt(buffer, itemId, index);
			SetShort(buffer, count, index);
		}

		_user->ShoppingMall(buffer);
	}

	void ExpectOpenFailed(int16_t result)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, OPEN_FAILED_SIZE);
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_SHOPPING_MALL);
				EXPECT_EQ(GetByte(pBuf, index), STORE_OPEN);
				EXPECT_EQ(static_cast<int16_t>(GetShort(pBuf, index)), result);
			});
		Request(STORE_OPEN);
	}

	void ExpectBuyResult(uint8_t result, int knightCash)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, BUY_RESULT_SIZE);
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_SHOPPING_MALL);
				EXPECT_EQ(GetByte(pBuf, index), STORE_BUY);
				EXPECT_EQ(GetByte(pBuf, index), result);
				EXPECT_EQ(GetInt(pBuf, index), knightCash);
			});
	}

	// Aujard's answer to DB_CUSTOM_KNIGHT_CASH_LOAD_REQUEST.
	void KnightCashLoaded(
		int knightCash, const char* accountId = ACCOUNT, uint8_t result = KNIGHT_CASH_SUCCESS)
	{
		char buffer[64] {};
		int index = 0;
		SetString1(buffer, accountId, index);
		SetByte(buffer, result, index);
		SetInt(buffer, knightCash, index);
		_user->RecvKnightCashLoad(buffer);
	}

	// Aujard's answer to DB_CUSTOM_KNIGHT_CASH_ADD_REQUEST.
	void KnightCashAdded(
		int amount, uint8_t notify, int knightCash, const char* accountId = ACCOUNT)
	{
		char buffer[64] {};
		int index = 0;
		SetString1(buffer, accountId, index);
		SetByte(buffer, KNIGHT_CASH_SUCCESS, index);
		SetInt(buffer, amount, index);
		SetByte(buffer, notify, index);
		SetInt(buffer, knightCash, index);
		_user->RecvKnightCashAdd(buffer);
	}

	void Open(int knightCash)
	{
		Request(STORE_OPEN);
		ASSERT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_OPENING);
		_user->AddSendCallback(nullptr);
		KnightCashLoaded(knightCash);
		ASSERT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_OPEN);
	}

	void FillInventory(int itemId)
	{
		for (int i = SLOT_MAX; i < SLOT_MAX + HAVE_MAX; i++)
		{
			_user->m_pUserData->m_sItemArray[i].nNum   = itemId;
			_user->m_pUserData->m_sItemArray[i].sCount = 1;
		}
	}
};

// The official server's reasons for keeping the store closed, and OpenKO's (nothing for sale).
TEST_F(ShoppingMallTest, OpenChecks)
{
	_user->m_bResHpType = USER_DEAD;
	ExpectOpenFailed(STORE_OPEN_DEAD);
	_user->m_bResHpType    = USER_STANDING;

	_user->m_sExchangeUser = 1;
	ExpectOpenFailed(STORE_OPEN_TRADING);
	_user->m_sExchangeUser      = -1;

	_user->m_pUserData->m_bZone = 40;
	ExpectOpenFailed(STORE_OPEN_ZONE);
	_user->m_pUserData->m_bZone = 21;

	FillInventory(ITEM_SCROLL);
	ExpectOpenFailed(STORE_OPEN_INVENTORY_FULL);
	_user->ClearInventory();

	_app->m_ShoppingMallItems.clear();
	ExpectOpenFailed(STORE_OPEN_UNAVAILABLE);

	EXPECT_EQ(_user->GetPacketsSent(), 5u);
	EXPECT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_CLOSED);
}

// Opening: the Knight Cash from Aujard first, then the store with the items for sale.
TEST_F(ShoppingMallTest, Open)
{
	Request(STORE_OPEN);
	EXPECT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_OPENING);
	EXPECT_EQ(_user->GetPacketsSent(), 0u);

	// Still waiting on Aujard.
	ExpectOpenFailed(STORE_OPEN_ALREADY_OPEN);

	// For someone else, who was on the socket before.
	KnightCashLoaded(1234, "someoneelse");
	EXPECT_EQ(_user->GetPacketsSent(), 1u);

	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, 12 + 2 * STORE_ITEM_SIZE);
			int index = 0;
			EXPECT_EQ(GetByte(pBuf, index), WIZ_SHOPPING_MALL);
			EXPECT_EQ(GetByte(pBuf, index), STORE_OPEN);
			EXPECT_EQ(GetShort(pBuf, index), STORE_OPEN_SUCCESS);
			EXPECT_EQ(GetShort(pBuf, index), HAVE_MAX);
			EXPECT_EQ(GetInt(pBuf, index), 1234);
			EXPECT_EQ(GetShort(pBuf, index), 2);
			EXPECT_EQ(GetInt(pBuf, index), ITEM_SCROLL);
			EXPECT_EQ(GetShort(pBuf, index), 1);
			EXPECT_EQ(GetInt(pBuf, index), PRICE_SCROLL);
			EXPECT_EQ(GetInt(pBuf, index), ITEM_RESURRECTION);
			EXPECT_EQ(GetShort(pBuf, index), RESURRECTION_COUNT);
			EXPECT_EQ(GetInt(pBuf, index), PRICE_RESURRECTION);
		});
	KnightCashLoaded(1234);
	EXPECT_EQ(_user->GetPacketsSent(), 2u);
	EXPECT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_OPEN);
	EXPECT_EQ(_user->m_iKnightCash, 1234);
}

// Aujard couldn't load the Knight Cash.
TEST_F(ShoppingMallTest, OpenUnavailable)
{
	Request(STORE_OPEN);
	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, OPEN_FAILED_SIZE);
			int index = 2;
			EXPECT_EQ(static_cast<int16_t>(GetShort(pBuf, index)), STORE_OPEN_UNAVAILABLE);
		});
	KnightCashLoaded(0, ACCOUNT, KNIGHT_CASH_ERROR);
	EXPECT_EQ(_user->GetPacketsSent(), 1u);
	EXPECT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_CLOSED);
}

// Buying: Aujard takes the price, then the item is given.
TEST_F(ShoppingMallTest, Buy)
{
	Open(1000);
	Request(STORE_BUY, ITEM_RESURRECTION, RESURRECTION_COUNT);
	EXPECT_TRUE(_user->m_bShoppingMallBuyPending);
	EXPECT_EQ(_user->GetPacketsSent(), 1u);

	// One at a time.
	ExpectBuyResult(STORE_BUY_INVALID, 1000);
	Request(STORE_BUY, ITEM_SCROLL, 1);

	_user->AddSendCallback(
		[](const char* /*pBuf*/, int len) { ASSERT_EQ(len, sizeof(WeightChangePacket)); });
	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, sizeof(ItemCountChangePacket));
			auto packet = reinterpret_cast<const ItemCountChangePacket*>(pBuf);
			EXPECT_EQ(packet->Opcode, WIZ_ITEM_COUNT_CHANGE);
			EXPECT_EQ(packet->ItemId, ITEM_RESURRECTION);
			EXPECT_EQ(packet->ItemCount, RESURRECTION_COUNT);
		});
	ExpectBuyResult(STORE_BUY_SUCCESS, 800);
	_user->ShoppingMallBuyResult(
		KNIGHT_CASH_SUCCESS, ITEM_RESURRECTION, RESURRECTION_COUNT, PRICE_RESURRECTION, 800);

	EXPECT_FALSE(_user->m_bShoppingMallBuyPending);
	EXPECT_EQ(_user->m_iKnightCash, 800);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum, ITEM_RESURRECTION);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount, RESURRECTION_COUNT);
	EXPECT_EQ(_user->GetPacketsSent(), 5u);
}

// Not enough Knight Cash: nothing is given.
TEST_F(ShoppingMallTest, BuyWithoutEnoughKnightCash)
{
	Open(100);
	Request(STORE_BUY, ITEM_SCROLL, 1);
	ASSERT_TRUE(_user->m_bShoppingMallBuyPending);

	ExpectBuyResult(STORE_BUY_NO_CASH, 100);
	_user->ShoppingMallBuyResult(KNIGHT_CASH_NOT_ENOUGH, ITEM_SCROLL, 1, PRICE_SCROLL, 100);
	EXPECT_FALSE(_user->m_bShoppingMallBuyPending);
	EXPECT_EQ(_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum, 0);
	EXPECT_EQ(_user->GetPacketsSent(), 2u);
}

// What's turned down without asking Aujard.
TEST_F(ShoppingMallTest, BuyChecks)
{
	// The store isn't open.
	ExpectBuyResult(STORE_BUY_INVALID, 0);
	Request(STORE_BUY, ITEM_SCROLL, 1);

	Open(1000);

	// Not for sale, or not that many.
	ExpectBuyResult(STORE_BUY_INVALID, 1000);
	Request(STORE_BUY, ITEM_NOT_FOR_SALE, 1);
	ExpectBuyResult(STORE_BUY_INVALID, 1000);
	Request(STORE_BUY, ITEM_RESURRECTION, 1);

	// Too heavy.
	_user->m_iItemWeight = _user->m_iMaxWeight;
	ExpectBuyResult(STORE_BUY_TOO_HEAVY, 1000);
	Request(STORE_BUY, ITEM_SCROLL, 1);
	_user->m_iItemWeight = 0;

	// No room: a full inventory, or a stack that can't take that many more.
	FillInventory(ITEM_SCROLL);
	ExpectBuyResult(STORE_BUY_NO_ROOM, 1000);
	Request(STORE_BUY, ITEM_SCROLL, 1);

	_user->m_pUserData->m_sItemArray[SLOT_MAX].nNum   = ITEM_RESURRECTION;
	_user->m_pUserData->m_sItemArray[SLOT_MAX].sCount = MAX_ITEM_COUNT - 1;
	ExpectBuyResult(STORE_BUY_NO_ROOM, 1000);
	Request(STORE_BUY, ITEM_RESURRECTION, RESURRECTION_COUNT);

	EXPECT_FALSE(_user->m_bShoppingMallBuyPending);
	EXPECT_EQ(_user->GetPacketsSent(), 7u);
}

// It no longer fits when Aujard answers: the Knight Cash goes back.
TEST_F(ShoppingMallTest, BuyGivenBackWithoutRoom)
{
	Open(1000);
	Request(STORE_BUY, ITEM_SCROLL, 1);
	ASSERT_TRUE(_user->m_bShoppingMallBuyPending);

	FillInventory(ITEM_RESURRECTION);
	ExpectBuyResult(STORE_BUY_NO_ROOM, 1000);
	_user->ShoppingMallBuyResult(
		KNIGHT_CASH_SUCCESS, ITEM_SCROLL, 1, PRICE_SCROLL, 1000 - PRICE_SCROLL);
	EXPECT_FALSE(_user->m_bShoppingMallBuyPending);
	EXPECT_EQ(_user->m_iKnightCash, 1000);
	EXPECT_EQ(_user->GetPacketsSent(), 2u);
}

// Closing: the official answer, the inventory.
TEST_F(ShoppingMallTest, Close)
{
	Open(1000);
	_user->TestGiveItem(ITEM_RESURRECTION, 3);

	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, 2 + HAVE_MAX * INVENTORY_SLOT_SIZE);
			int index = 0;
			EXPECT_EQ(GetByte(pBuf, index), WIZ_SHOPPING_MALL);
			EXPECT_EQ(GetByte(pBuf, index), STORE_CLOSE);
			EXPECT_EQ(GetInt(pBuf, index), ITEM_RESURRECTION);
			EXPECT_EQ(GetShort(pBuf, index), 1);
			EXPECT_EQ(GetShort(pBuf, index), 3);
		});
	Request(STORE_CLOSE);
	EXPECT_EQ(_user->m_bShoppingMallState, SHOPPING_MALL_CLOSED);
	EXPECT_EQ(_user->GetPacketsSent(), 4u);

	// Closed already: nothing to answer.
	Request(STORE_CLOSE);
	EXPECT_EQ(_user->GetPacketsSent(), 4u);
}

// Knight Cash earned: the user is told, unless it went back quietly (a refund).
TEST_F(ShoppingMallTest, KnightCashEarned)
{
	_user->AddSendCallback(
		[](const char* pBuf, int len)
		{
			ASSERT_EQ(len, KNIGHT_CASH_PACKET_SIZE);
			int index = 0;
			EXPECT_EQ(GetByte(pBuf, index), WIZ_SHOPPING_MALL);
			EXPECT_EQ(GetByte(pBuf, index), STORE_KNIGHT_CASH);
			EXPECT_EQ(GetInt(pBuf, index), 120);
			EXPECT_EQ(GetInt(pBuf, index), 1120);
		});
	KnightCashAdded(120, 1, 1120);
	EXPECT_EQ(_user->m_iKnightCash, 1120);

	KnightCashAdded(300, 0, 1420);
	KnightCashAdded(5, 1, 99, "someoneelse");
	EXPECT_EQ(_user->m_iKnightCash, 1420);
	EXPECT_EQ(_user->GetPacketsSent(), 1u);
}
