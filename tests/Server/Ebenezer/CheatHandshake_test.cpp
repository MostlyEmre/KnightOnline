#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared/CheatSettings.h>
#include <shared-server/utilities.h>

#include <array>
#include <filesystem>
#include <fstream>

using namespace Ebenezer;

/**
 * Tests the cheats menu's handshake (WIZ_CHEAT): the server announces whether cheats are allowed
 * along with the current settings, accepts new settings only when allowed and valid, saves them in
 * cheats.ini, and hands AIServer a snapshot.
 */
class CheatHandshakeTest : public testing::Test
{
protected:
	std::unique_ptr<TestApp> _app   = nullptr;
	std::shared_ptr<TestUser> _user = nullptr;
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "CheatHandshakeTest.ini";
		std::filesystem::remove(_path);

		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);
		_app->_cheatSettingsPath = _path;
		_app->m_bCheatsAllowed   = true;

		_user                    = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);

		_user->SetState(CONNECTION_STATE_GAMESTART);
		_user->m_pUserData->m_sHp = 100;
		_user->ResetSend();
	}

	void TearDown() override
	{
		_user.reset();
		_app.reset();
		std::filesystem::remove(_path);
	}

	static Cheats::Settings Sample()
	{
		Cheats::Settings settings;
		settings.bOn        = true;
		settings.sExpPct    = 1000;
		settings.sDropPct   = 300;
		settings.bFreeShops = true;
		return settings;
	}

	// A WIZ_CHEAT request from the client, without the opcode byte.
	void Request(
		uint8_t subOpcode, const Cheats::Settings& settings, size_t size = Cheats::SETTINGS_SIZE)
	{
		char buffer[64] {};
		int index = 0;
		SetByte(buffer, subOpcode, index);

		std::array<uint8_t, Cheats::SETTINGS_SIZE> bytes {};
		Cheats::Write(settings, bytes);
		memcpy(buffer + index, bytes.data(), size);
		_user->Cheat(buffer, index + static_cast<int>(size));
	}

	void ExpectAvailable(uint8_t allowed)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, 3);
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_CHEAT);
				EXPECT_EQ(GetByte(pBuf, index), CHEAT_AVAILABLE);
				EXPECT_EQ(GetByte(pBuf, index), allowed);
			});
	}

	void ExpectSettings(const Cheats::Settings& expected)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, 2 + static_cast<int>(Cheats::SETTINGS_SIZE));
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_CHEAT);
				EXPECT_EQ(GetByte(pBuf, index), CHEAT_SETTINGS);
				auto read = Cheats::Read(std::span<const uint8_t>(
					reinterpret_cast<const uint8_t*>(pBuf + index), Cheats::SETTINGS_SIZE));
				ASSERT_TRUE(read.has_value());
				EXPECT_EQ(read.value_or(Cheats::Settings {}), expected);
			});
	}

	void ExpectResult(uint8_t request, uint8_t result)
	{
		_user->AddSendCallback(
			[=](const char* pBuf, int len)
			{
				ASSERT_EQ(len, 4);
				int index = 0;
				EXPECT_EQ(GetByte(pBuf, index), WIZ_CHEAT);
				EXPECT_EQ(GetByte(pBuf, index), CHEAT_RESULT);
				EXPECT_EQ(GetByte(pBuf, index), request);
				EXPECT_EQ(GetByte(pBuf, index), result);
			});
	}
};

// Allowed: the announcement and the settings; not allowed: just the announcement.
TEST_F(CheatHandshakeTest, AnnouncesAvailability)
{
	ExpectAvailable(1);
	ExpectSettings(Cheats::Settings {});
	_user->SendCheatAvailable();

	_app->m_bCheatsAllowed = false;
	ExpectAvailable(0);
	_user->SendCheatAvailable();

	EXPECT_EQ(_user->GetPacketsSent(), 3u);
}

// Settings saved before a restart are announced at login.
TEST_F(CheatHandshakeTest, AnnouncesLoadedSettings)
{
	{
		std::ofstream file(_path);
		file << "[CHEATS]\nON=1\nEXP=1000\n";
	}
	_app->LoadCheatSettings();

	Cheats::Settings expected;
	expected.bOn     = true;
	expected.sExpPct = 1000;

	ExpectAvailable(1);
	ExpectSettings(expected);
	_user->SendCheatAvailable();
	EXPECT_EQ(_user->GetPacketsSent(), 2u);
}

// A server that doesn't allow cheats doesn't apply saved ones either: the player couldn't see or
// change them.
TEST_F(CheatHandshakeTest, SavedCheatsIgnoredWhenNotAllowed)
{
	{
		std::ofstream file(_path);
		file << "[CHEATS]\nON=1\nEXP=1000\n";
	}

	_app->m_bCheatsAllowed = false;
	_app->LoadCheatSettings();

	EXPECT_FALSE(_app->CheatSettings().bOn);
	EXPECT_EQ(_app->CheatPct(&Cheats::Settings::sExpPct), 100);
}

// Valid settings are kept and echoed to the players in the game.
TEST_F(CheatHandshakeTest, SetsAndEchoesSettings)
{
	ExpectSettings(Sample());
	Request(CHEAT_SETTINGS, Sample());

	EXPECT_EQ(_app->CheatSettings(), Sample());
	EXPECT_EQ(_user->GetPacketsSent(), 1u);
}

// A server that doesn't allow cheats refuses everything.
TEST_F(CheatHandshakeTest, RefusesWhenNotAllowed)
{
	_app->m_bCheatsAllowed = false;

	ExpectResult(CHEAT_SETTINGS, CHEAT_NOT_ALLOWED);
	Request(CHEAT_SETTINGS, Sample());

	EXPECT_EQ(_app->CheatSettings(), Cheats::Settings {});
	EXPECT_EQ(_user->GetPacketsSent(), 1u);
}

// A value outside the steps, or a short payload, changes nothing.
TEST_F(CheatHandshakeTest, RefusesBadSettings)
{
	Cheats::Settings bad = Sample();
	bad.sExpPct          = 150;
	ExpectResult(CHEAT_SETTINGS, CHEAT_BAD_VALUE);
	Request(CHEAT_SETTINGS, bad);

	ExpectResult(CHEAT_SETTINGS, CHEAT_BAD_VALUE);
	Request(CHEAT_SETTINGS, Sample(), 5);

	ExpectResult(99, CHEAT_BAD_VALUE);
	Request(99, Sample(), 0);

	EXPECT_EQ(_app->CheatSettings(), Cheats::Settings {});
	EXPECT_EQ(_user->GetPacketsSent(), 3u);
}

// Nothing before the game has started.
TEST_F(CheatHandshakeTest, IgnoredOutsideTheGame)
{
	_user->SetState(CONNECTION_STATE_CONNECTED);
	Request(CHEAT_SETTINGS, Sample());

	EXPECT_EQ(_app->CheatSettings(), Cheats::Settings {});
	EXPECT_EQ(_user->GetPacketsSent(), 0u);
}

// The settings survive a restart; a file with a value outside the steps loads the defaults.
TEST_F(CheatHandshakeTest, SettingsFileRoundTrip)
{
	_user->AddSendCallback(nullptr);
	ASSERT_TRUE(_app->SetCheatSettings(Sample()));

	// One app at a time.
	_user.reset();
	_app.reset();

	TestApp restarted;
	restarted._cheatSettingsPath = _path;
	restarted.m_bCheatsAllowed   = true;
	restarted.LoadCheatSettings();
	EXPECT_EQ(restarted.CheatSettings(), Sample());

	{
		std::ofstream file(_path);
		file << "[CHEATS]\nON=1\nEXP=150\n";
	}
	restarted.LoadCheatSettings();
	EXPECT_EQ(restarted.CheatSettings(), Cheats::Settings {});
}

// AIServer's snapshot: the packet, a sequence that rises with each change, and a re-send whenever
// AIServer is handed everything again.
TEST_F(CheatHandshakeTest, AiSnapshot)
{
	char buffer[64] {};
	int len = _app->BuildCheatSettingsAI(buffer);
	ASSERT_EQ(len, 1 + static_cast<int>(Cheats::AG_SETTINGS_SIZE));

	int index = 0;
	EXPECT_EQ(GetByte(buffer, index), AG_CHEAT_SETTINGS);
	const uint32_t seq = static_cast<uint32_t>(GetDWORD(buffer, index));

	_user->AddSendCallback(nullptr);
	ASSERT_TRUE(_app->SetCheatSettings(Sample()));

	ASSERT_EQ(_app->BuildCheatSettingsAI(buffer), len);
	index = 1;
	EXPECT_TRUE(Cheats::IsNewer(static_cast<uint32_t>(GetDWORD(buffer, index)), seq));
	auto read = Cheats::Read(std::span<const uint8_t>(
		reinterpret_cast<const uint8_t*>(buffer + index), Cheats::SETTINGS_SIZE));
	EXPECT_EQ(read.value_or(Cheats::Settings {}), Sample());

	const int sentBefore = _app->_cheatSnapshotsSent;
	_app->SendAllUserInfo();
	EXPECT_EQ(_app->_cheatSnapshotsSent, sentBefore + 1);
}
