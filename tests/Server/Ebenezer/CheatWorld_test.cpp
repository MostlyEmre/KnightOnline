#include <gtest/gtest.h>
#include "TestApp.h"
#include "TestUser.h"

#include <shared/CheatSettings.h>
#include <shared-server/utilities.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string_view>

using namespace Ebenezer;

/**
 * Tests the cheats menu's world cheats: teleporting to a zone or an NPC, NPC search, the weather
 * and time overrides, and the kill-around request to AIServer.
 */
class CheatWorldTest : public testing::Test
{
protected:
	std::unique_ptr<TestApp> _app   = nullptr;
	std::shared_ptr<TestUser> _user = nullptr;
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "CheatWorldTest.ini";
		std::filesystem::remove(_path);

		_app = std::make_unique<TestApp>();
		ASSERT_TRUE(_app != nullptr);
		_app->_cheatSettingsPath = _path;
		_app->m_bCheatsAllowed   = true;
		_app->m_nServerNo        = 1;
		_app->m_nHour            = 15;
		_app->m_nMin             = 30;
		_app->m_nWeather         = 1;
		_app->m_nAmount          = 0;

		// Luferson (Karus' home), El Morad's home, and Moradon
		AddMap(1, 1);
		AddMap(2, 1);
		AddMap(21, 1);
		AddStartPosition(2, 1859, 170);

		_user = _app->AddUser();
		ASSERT_TRUE(_user != nullptr);
		_user->SetState(CONNECTION_STATE_GAMESTART);
		_user->m_pUserData->m_bNation = NATION_KARUS;
		_user->m_pUserData->m_bLevel  = 10;
		_user->m_pUserData->m_sHp     = 100;
		auto* home                    = static_cast<TestMap*>(_app->GetMapByID(1));
		ASSERT_NE(home, nullptr);
		ASSERT_TRUE(home->Add(_user.get(), 1, 1));

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

	void AddMap(uint8_t zoneId, uint8_t type)
	{
		TestMap* map = _app->CreateMap(zoneId);
		ASSERT_NE(map, nullptr);
		map->m_bType     = type;
		map->m_nServerNo = _app->m_nServerNo;
	}

	void AddStartPosition(int16_t zoneId, int16_t elmoX, int16_t elmoZ)
	{
		auto* position   = new model::StartPosition {};
		position->ZoneId = zoneId;
		position->KarusX = elmoX;
		position->KarusZ = elmoZ;
		position->ElmoX  = elmoX;
		position->ElmoZ  = elmoZ;
		ASSERT_TRUE(_app->m_StartPositionTableMap.PutData(zoneId, position));
	}

	CNpc* AddNpc(uint16_t npcId, const char* name, uint8_t type, int16_t zone, float x, float z)
	{
		CNpc* npc = _app->CreateNPC(npcId);
		if (npc == nullptr)
			return nullptr;

		std::string_view(name).copy(npc->m_strName, sizeof(npc->m_strName) - 1);
		npc->m_tNpcType = type;
		npc->m_sCurZone = zone;
		npc->m_fCurX    = x;
		npc->m_fCurZ    = z;
		npc->m_NpcState = NPC_LIVE;
		return npc;
	}

	void SetCheats(const Cheats::Settings& settings)
	{
		ASSERT_TRUE(_app->SetCheatSettings(settings));
	}
};

// The enemy's home is closed to a Karus player, but not to the cheat; zones off the list are.
TEST_F(CheatWorldTest, TeleportZoneSkipsRules)
{
	_user->ZoneChange(2, 0.0f, 0.0f);
	EXPECT_EQ(_user->m_pUserData->m_bZone, 1);

	EXPECT_EQ(_user->CheatTeleportZone(2), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bZone, 2);
	EXPECT_FLOAT_EQ(_user->m_pUserData->m_curx, 1859.0f);

	EXPECT_EQ(_user->CheatTeleportZone(51), CHEAT_BAD_VALUE);

	// Still loading the last zone
	EXPECT_EQ(_user->CheatTeleportZone(21), CHEAT_BUSY);
}

TEST_F(CheatWorldTest, NpcSearch)
{
	ASSERT_NE(AddNpc(10001, "Charon Guard", 11, 21, 100, 100), nullptr);
	ASSERT_NE(AddNpc(10002, "Charon", 21, 21, 200, 200), nullptr);
	ASSERT_NE(AddNpc(10003, "Charon Rat", 0, 21, 300, 300), nullptr);

	_user->SendCheatNpcSearch("charon");

	const auto packets = _user->SentWith(WIZ_CHEAT);
	ASSERT_EQ(packets.size(), 1u);
	const char* packet = packets[0].data();
	int index          = 1;
	EXPECT_EQ(GetByte(packet, index), CHEAT_FIND_NPC);
	ASSERT_EQ(GetByte(packet, index), 2);

	char name[64] {};
	EXPECT_EQ(GetShort(packet, index), 10002);
	EXPECT_EQ(GetShort(packet, index), 21);
	int len = GetByte(packet, index);
	GetString(name, packet, len, index);
	EXPECT_STREQ(name, "Charon");

	memset(name, 0, sizeof(name));
	EXPECT_EQ(GetShort(packet, index), 10001);
	EXPECT_EQ(GetShort(packet, index), 21);
	len = GetByte(packet, index);
	GetString(name, packet, len, index);
	EXPECT_STREQ(name, "Charon Guard");
}

TEST_F(CheatWorldTest, TeleportNpc)
{
	ASSERT_NE(AddNpc(10002, "Charon", 21, 21, 500, 600), nullptr);

	EXPECT_EQ(_user->CheatTeleportNpc(10002), CHEAT_OK);
	EXPECT_EQ(_user->m_pUserData->m_bZone, 21);
	EXPECT_LE(std::fabs(_user->m_pUserData->m_curx - 500.0f), 3.0f);
	EXPECT_LE(std::fabs(_user->m_pUserData->m_curz - 600.0f), 3.0f);

	_user->m_bWarp = 0;
	EXPECT_EQ(_user->CheatTeleportNpc(12345), CHEAT_NOT_FOUND);
}

TEST_F(CheatWorldTest, KillAroundPacket)
{
	char buffer[16] {};
	ASSERT_EQ(_user->BuildKillAroundAI(buffer), 3);
	int index = 0;
	EXPECT_EQ(GetByte(buffer, index), AG_CHEAT_KILL_AROUND);
	EXPECT_EQ(GetShort(buffer, index), _user->GetSocketID());

	EXPECT_EQ(_user->CheatKillAround(), CHEAT_NO_AI);
}

TEST_F(CheatWorldTest, WeatherOverride)
{
	Cheats::Settings settings;
	settings.bOn      = true;
	settings.eWeather = Cheats::Weather::HeavySnow;
	SetCheats(settings);

	const auto weather = _app->CheatWeather();
	ASSERT_TRUE(weather.has_value());
	EXPECT_EQ(weather.value_or(Cheats::WeatherOverride {}), (Cheats::WeatherOverride { 3, 100 }));

	const auto packets = _user->SentWith(WIZ_WEATHER);
	ASSERT_EQ(packets.size(), 1u);
	int index = 1;
	EXPECT_EQ(GetByte(packets[0].data(), index), 3);
	EXPECT_EQ(GetShort(packets[0].data(), index), 100);

	settings.bOn = false;
	SetCheats(settings);
	EXPECT_FALSE(_app->CheatWeather().has_value());
}

TEST_F(CheatWorldTest, HourOverride)
{
	Cheats::Settings settings;
	settings.bOn   = true;
	settings.cHour = 6;
	SetCheats(settings);
	EXPECT_EQ(_app->CheatHour(), 6);

	_user->RecordSends();
	_user->SendTimeStatus();
	auto packets = _user->SentWith(WIZ_TIME);
	ASSERT_EQ(packets.size(), 1u);
	int index = 7;
	EXPECT_EQ(GetShort(packets[0].data(), index), 6);

	_user->RecordSends();
	settings.cHour = Cheats::HOUR_NORMAL;
	SetCheats(settings);
	packets = _user->SentWith(WIZ_TIME);
	ASSERT_EQ(packets.size(), 1u);
	index = 7;
	EXPECT_EQ(GetShort(packets[0].data(), index), 15);
}
