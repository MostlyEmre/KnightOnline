#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared-server/utilities.h>

using namespace Ebenezer;

/**
 * Tests the Genie handshake (WIZ_GENIE): the server announces whether Genie may run, and the client
 * starts and stops it only after that.
 */
class GenieTest : public testing::Test
{
protected:
	std::unique_ptr<TestApp> _app   = nullptr;
	std::shared_ptr<TestUser> _user = nullptr;

	static constexpr int REPLY_SIZE = 3;

	void SetUp() override
	{
		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);

		_user->SetState(CONNECTION_STATE_GAMESTART);
		_user->m_pUserData->m_sHp = 100;
		_user->ResetSend();
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
	}

	// A WIZ_GENIE request from the client.
	void Request(uint8_t subOpcode)
	{
		char buffer[4] {};
		int index = 0;
		SetByte(buffer, subOpcode, index);
		_user->Genie(buffer);
	}

	void ExpectReply(uint8_t subOpcode, uint8_t value)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, REPLY_SIZE);
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_GENIE);
				EXPECT_EQ(GetByte(pBuf, index), subOpcode);
				EXPECT_EQ(GetByte(pBuf, index), value);
			});
	}
};

// The announcement follows the server's switch.
TEST_F(GenieTest, AnnouncesAvailability)
{
	ExpectReply(GENIE_AVAILABLE, 1);
	_user->SendGenieAvailable();

	_app->m_bGenieEnabled = false;
	ExpectReply(GENIE_AVAILABLE, 0);
	_user->SendGenieAvailable();

	EXPECT_EQ(_user->GetPacketsSent(), 2u);
}

// Starting is answered; stopping isn't.
TEST_F(GenieTest, StartAndStop)
{
	ExpectReply(GENIE_START, 1);
	Request(GENIE_START);
	EXPECT_TRUE(_user->m_bGenieActive);

	Request(GENIE_STOP);
	EXPECT_FALSE(_user->m_bGenieActive);
	EXPECT_EQ(_user->GetPacketsSent(), 1u);
}

// No Genie when the server has it switched off, for the dead, or before the game has started.
TEST_F(GenieTest, StartRefused)
{
	_app->m_bGenieEnabled = false;
	ExpectReply(GENIE_START, 0);
	Request(GENIE_START);
	EXPECT_FALSE(_user->m_bGenieActive);

	_app->m_bGenieEnabled = true;
	_user->m_bResHpType   = USER_DEAD;
	ExpectReply(GENIE_START, 0);
	Request(GENIE_START);
	EXPECT_FALSE(_user->m_bGenieActive);

	_user->m_bResHpType = USER_STANDING;
	_user->SetState(CONNECTION_STATE_CONNECTED);
	Request(GENIE_START);
	EXPECT_FALSE(_user->m_bGenieActive);
	EXPECT_EQ(_user->GetPacketsSent(), 2u);
}
