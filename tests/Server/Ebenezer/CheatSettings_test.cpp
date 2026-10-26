#include <gtest/gtest.h>

#include <shared/CheatSettings.h>

#include <array>
#include <cstdint>
#include <vector>

/**
 * Tests the shared cheat model: the settings' byte layout and allowed values, the master switch,
 * and the scaling and +level arithmetic both servers and the client rely on.
 */
namespace
{

Cheats::Settings EveryFieldSet()
{
	Cheats::Settings settings;
	settings.bOn               = true;
	settings.sExpPct           = 1000;
	settings.sDamageDealtPct   = 10000;
	settings.sDamageTakenPct   = 50;
	settings.sCoinPct          = 2500;
	settings.sDropPct          = 300;
	settings.sLoyaltyPct       = 200;
	settings.sRespawnPct       = 500;
	settings.bInfiniteMp       = true;
	settings.bNoDurabilityLoss = true;
	settings.bKeepConsumables  = true;
	settings.bNoWeightLimit    = true;
	settings.bFreeShops        = true;
	settings.eWeather          = Cheats::Weather::HeavySnow;
	settings.cHour             = 23;
	return settings;
}

std::array<uint8_t, Cheats::SETTINGS_SIZE> Bytes(const Cheats::Settings& settings)
{
	std::array<uint8_t, Cheats::SETTINGS_SIZE> bytes {};
	Cheats::Write(settings, bytes);
	return bytes;
}

} // namespace

TEST(CheatSettings, RoundTripsEveryField)
{
	const Cheats::Settings settings = EveryFieldSet();
	const auto bytes                = Bytes(settings);

	EXPECT_EQ(bytes[0], 1);
	// sExpPct = 1000 = 0x03E8, little-endian
	EXPECT_EQ(bytes[1], 0xE8);
	EXPECT_EQ(bytes[2], 0x03);

	const auto read = Cheats::Read(bytes);
	ASSERT_TRUE(read.has_value());
	EXPECT_EQ(read.value_or(Cheats::Settings {}), settings);
}

TEST(CheatSettings, ReadRefusesBadValues)
{
	const auto good = Bytes(EveryFieldSet());

	EXPECT_FALSE(Cheats::Read(std::span<const uint8_t>(good.data(), good.size() - 1)).has_value());

	Cheats::Settings settings = EveryFieldSet();
	settings.sExpPct          = 150;
	EXPECT_FALSE(Cheats::Read(Bytes(settings)).has_value());

	settings         = EveryFieldSet();
	settings.sExpPct = 0;
	EXPECT_FALSE(Cheats::Read(Bytes(settings)).has_value());

	settings                 = EveryFieldSet();
	settings.sDamageTakenPct = 0;
	EXPECT_TRUE(Cheats::Read(Bytes(settings)).has_value());
	settings.sDamageTakenPct = 50;
	EXPECT_TRUE(Cheats::Read(Bytes(settings)).has_value());

	settings       = EveryFieldSet();
	settings.cHour = 24;
	EXPECT_FALSE(Cheats::Read(Bytes(settings)).has_value());

	auto bytes                       = Bytes(EveryFieldSet());
	bytes[Cheats::SETTINGS_SIZE - 2] = 6; // weather
	EXPECT_FALSE(Cheats::Read(bytes).has_value());
}

TEST(CheatSettings, OffMeansNormal)
{
	Cheats::Settings settings = EveryFieldSet();
	settings.bOn              = false;
	EXPECT_EQ(Cheats::Effective(settings, &Cheats::Settings::sDamageDealtPct), 100);
	EXPECT_FALSE(Cheats::EffectiveFlag(settings, &Cheats::Settings::bInfiniteMp));

	settings.bOn = true;
	EXPECT_EQ(Cheats::Effective(settings, &Cheats::Settings::sDamageDealtPct), 10000);
	EXPECT_TRUE(Cheats::EffectiveFlag(settings, &Cheats::Settings::bInfiniteMp));
}

TEST(CheatSettings, ScaleDamageClampsAndKeepsSign)
{
	EXPECT_EQ(Cheats::ScaleDamage(400, 10000), 32767);
	EXPECT_EQ(Cheats::ScaleDamage(1, 50), 1);
	EXPECT_EQ(Cheats::ScaleDamage(3, 50), 1);
	EXPECT_EQ(Cheats::ScaleDamage(10, 0), 0);
	EXPECT_EQ(Cheats::ScaleDamage(0, 10000), 0);
	EXPECT_EQ(Cheats::ScaleDamage(-5, 10000), -5);
	EXPECT_EQ(Cheats::ScaleDamage(100, 100), 100);
}

TEST(CheatSettings, ScaleGainCaps)
{
	EXPECT_EQ(Cheats::ScaleGain(32767, 10000), 3'276'700);
	EXPECT_EQ(Cheats::ScaleGain(2'000'000'000, 200), 2'100'000'000);
	EXPECT_EQ(Cheats::ScaleGain(-10, 500), -10);
}

TEST(CheatSettings, DropChanceCapsAtCertain)
{
	EXPECT_EQ(Cheats::ScaleDropChance(0, 10000), 0);
	EXPECT_EQ(Cheats::ScaleDropChance(50, 10000), 5000);
	EXPECT_EQ(Cheats::ScaleDropChance(5000, 300), 10000);
}

TEST(CheatSettings, RegenHasAFloorAndNeverSlows)
{
	EXPECT_EQ(Cheats::ScaleRegenTime(30000, 100), 30000);
	EXPECT_EQ(Cheats::ScaleRegenTime(30000, 200), 15000);
	EXPECT_EQ(Cheats::ScaleRegenTime(30000, 10000), 5000);
	EXPECT_EQ(Cheats::ScaleRegenTime(4000, 10000), 4000);
	EXPECT_EQ(Cheats::ScaleRegenTime(60000, 50), 60000);
}

TEST(CheatSettings, UnscaleDamage)
{
	EXPECT_EQ(Cheats::UnscaleDamage(1000, 1000), 100);
	EXPECT_EQ(Cheats::UnscaleDamage(5, 0), 5);
	EXPECT_EQ(Cheats::UnscaleDamage(5, 100), 5);
}

TEST(CheatSettings, PlusArithmetic)
{
	EXPECT_EQ(Cheats::PlusOf(0), 0);
	EXPECT_EQ(Cheats::PlusOf(5), 5);
	EXPECT_EQ(Cheats::PlusOf(10), 10);
	EXPECT_EQ(Cheats::PlusOf(15), 5);
	EXPECT_EQ(Cheats::PlusOf(20), 10);

	EXPECT_EQ(Cheats::ExtWithPlus(5, 6), 6);
	EXPECT_EQ(Cheats::ExtWithPlus(5, 0), 0);
	EXPECT_EQ(Cheats::ExtWithPlus(15, 6), 16);
	EXPECT_EQ(Cheats::ExtWithPlus(15, 0), -1);
	EXPECT_EQ(Cheats::ExtWithPlus(0, 10), 10);
	EXPECT_EQ(Cheats::ExtWithPlus(5, 11), -1);
	EXPECT_EQ(Cheats::ExtWithPlus(995, 6), 996);
	EXPECT_EQ(Cheats::ExtWithPlus(1000, 1), -1);
}

TEST(CheatSettings, SequenceOnlyMovesForward)
{
	EXPECT_TRUE(Cheats::IsNewer(5, 4));
	EXPECT_FALSE(Cheats::IsNewer(4, 4));
	EXPECT_FALSE(Cheats::IsNewer(3, 4));
}

TEST(CheatSettings, TeleportZones)
{
	EXPECT_TRUE(Cheats::IsTeleportZone(21));
	EXPECT_FALSE(Cheats::IsTeleportZone(51));
	EXPECT_FALSE(Cheats::IsTeleportZone(91));
	EXPECT_FALSE(Cheats::IsTeleportZone(101));
	EXPECT_FALSE(Cheats::IsTeleportZone(0));
}
