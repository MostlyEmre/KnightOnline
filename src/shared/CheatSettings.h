#ifndef SHARED_CHEATSETTINGS_H
#define SHARED_CHEATSETTINGS_H

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>

/// \brief The cheats menu's settings, shared by the client, Ebenezer and AIServer: the allowed
/// values, their byte layout in WIZ_CHEAT and AG_CHEAT_SETTINGS, the master switch, and the
/// scaling and +level arithmetic the servers apply. Standard library only.
namespace Cheats
{

/// Multipliers are percentages: 100 is normal.
inline constexpr std::array<uint16_t, 8> MULTIPLIER_STEPS { 100, 200, 300, 500, 1000, 2500, 5000,
	10000 };

/// Damage taken may also be removed (god mode) or halved.
inline constexpr std::array<uint16_t, 10> DAMAGE_TAKEN_STEPS { 0, 50, 100, 200, 300, 500, 1000,
	2500, 5000, 10000 };

inline constexpr std::array<uint32_t, 7> COIN_STEPS { 0, 100'000, 1'000'000, 10'000'000,
	100'000'000, 1'000'000'000, 2'100'000'000 };

/// National Points and Knight Cash.
inline constexpr std::array<uint32_t, 5> POINT_STEPS { 0, 1'000, 10'000, 100'000, 1'000'000 };

inline constexpr std::array<uint16_t, 8> COUNTABLE_STEPS { 1, 5, 10, 50, 100, 500, 1000, 9999 };
inline constexpr std::array<uint16_t, 4> SINGLE_STEPS { 1, 2, 5, 10 };

inline constexpr uint8_t POINTS_PER_PRESS = 10;
inline constexpr int MIN_LEVEL            = 1;
inline constexpr int MAX_LEVEL            = 80;
inline constexpr int MAX_PLUS             = 10;

/// The game's coin, NP and EXP cap (MAX_CURRENCY in globals.h).
inline constexpr int64_t MAX_GAIN         = 2'100'000'000;

/// A faster respawn never goes below this, so the respawn can't overtake the death on its way to
/// Ebenezer.
inline constexpr int RESPAWN_FLOOR_MS     = 5000;

inline constexpr int8_t HOUR_NORMAL       = -1;

/// The largest single hit both servers can carry (an int16 on the wire).
inline constexpr int MAX_DAMAGE           = 32767;

enum class Weather : uint8_t
{
	Normal,
	Clear,
	LightRain,
	HeavyRain,
	LightSnow,
	HeavySnow
};

inline constexpr uint8_t WEATHER_COUNT = 6;

/// A WIZ_WEATHER value: type (1 fine, 2 rain, 3 snow) and amount.
struct WeatherOverride
{
	uint8_t byType;
	int16_t sAmount;

	bool operator==(const WeatherOverride&) const = default;
};

/// \returns the weather to send instead of the server's own, or nullopt for Normal.
inline std::optional<WeatherOverride> WeatherFor(Weather weather)
{
	switch (weather)
	{
		case Weather::Clear:
			return WeatherOverride { 1, 0 };
		case Weather::LightRain:
			return WeatherOverride { 2, 30 };
		case Weather::HeavyRain:
			return WeatherOverride { 2, 100 };
		case Weather::LightSnow:
			return WeatherOverride { 3, 30 };
		case Weather::HeavySnow:
			return WeatherOverride { 3, 100 };
		default:
			return std::nullopt;
	}
}

struct Settings
{
	/// The master switch: while off, nothing below has any effect.
	bool bOn                               = false;

	uint16_t sExpPct                       = 100;
	uint16_t sDamageDealtPct               = 100;
	uint16_t sDamageTakenPct               = 100;
	uint16_t sCoinPct                      = 100;
	uint16_t sDropPct                      = 100;
	uint16_t sLoyaltyPct                   = 100;
	uint16_t sRespawnPct                   = 100;

	bool bInfiniteMp                       = false;
	bool bNoDurabilityLoss                 = false;
	bool bKeepConsumables                  = false;
	bool bNoWeightLimit                    = false;
	bool bFreeShops                        = false;

	Weather eWeather                       = Weather::Normal;

	/// 0..23, or HOUR_NORMAL
	int8_t cHour                           = HOUR_NORMAL;

	bool operator==(const Settings&) const = default;
};

/// [u8 on][u16 exp][u16 dealt][u16 taken][u16 coins][u16 drops][u16 NP][u16 respawn] (little-endian)
/// [u8 flags: bit 0 infinite MP, 1 no durability loss, 2 keep consumables, 3 no weight limit,
/// 4 free shops][u8 weather][i8 hour]
inline constexpr size_t SETTINGS_SIZE = 18;

template <class T, size_t N>
constexpr bool IsStep(const std::array<T, N>& steps, T value)
{
	return std::find(steps.begin(), steps.end(), value) != steps.end();
}

inline bool IsValid(const Settings& settings)
{
	for (uint16_t pct : { settings.sExpPct, settings.sDamageDealtPct, settings.sCoinPct,
			 settings.sDropPct, settings.sLoyaltyPct, settings.sRespawnPct })
	{
		if (!IsStep(MULTIPLIER_STEPS, pct))
			return false;
	}

	if (!IsStep(DAMAGE_TAKEN_STEPS, settings.sDamageTakenPct))
		return false;

	if (static_cast<uint8_t>(settings.eWeather) >= WEATHER_COUNT)
		return false;

	return settings.cHour == HOUR_NORMAL || (settings.cHour >= 0 && settings.cHour <= 23);
}

inline void Write(const Settings& settings, std::span<uint8_t, SETTINGS_SIZE> out)
{
	size_t i      = 0;
	out[i++]      = settings.bOn ? 1 : 0;

	auto writeU16 = [&](uint16_t value)
	{
		out[i++] = static_cast<uint8_t>(value & 0xFF);
		out[i++] = static_cast<uint8_t>(value >> 8);
	};

	writeU16(settings.sExpPct);
	writeU16(settings.sDamageDealtPct);
	writeU16(settings.sDamageTakenPct);
	writeU16(settings.sCoinPct);
	writeU16(settings.sDropPct);
	writeU16(settings.sLoyaltyPct);
	writeU16(settings.sRespawnPct);

	uint8_t flags  = 0;
	flags         |= settings.bInfiniteMp ? 0x01 : 0;
	flags         |= settings.bNoDurabilityLoss ? 0x02 : 0;
	flags         |= settings.bKeepConsumables ? 0x04 : 0;
	flags         |= settings.bNoWeightLimit ? 0x08 : 0;
	flags         |= settings.bFreeShops ? 0x10 : 0;
	out[i++]       = flags;

	out[i++]       = static_cast<uint8_t>(settings.eWeather);
	out[i++]       = static_cast<uint8_t>(settings.cHour);
}

/// \returns the settings, or nullopt when the input is too short or any value isn't allowed.
inline std::optional<Settings> Read(std::span<const uint8_t> in)
{
	if (in.size() < SETTINGS_SIZE)
		return std::nullopt;

	size_t i = 0;
	Settings settings;
	if (in[i] > 1)
		return std::nullopt;
	settings.bOn = in[i++] == 1;

	auto readU16 = [&]()
	{
		uint16_t value  = static_cast<uint16_t>(in[i] | (in[i + 1] << 8));
		i              += 2;
		return value;
	};

	settings.sExpPct           = readU16();
	settings.sDamageDealtPct   = readU16();
	settings.sDamageTakenPct   = readU16();
	settings.sCoinPct          = readU16();
	settings.sDropPct          = readU16();
	settings.sLoyaltyPct       = readU16();
	settings.sRespawnPct       = readU16();

	const uint8_t flags        = in[i++];
	settings.bInfiniteMp       = (flags & 0x01) != 0;
	settings.bNoDurabilityLoss = (flags & 0x02) != 0;
	settings.bKeepConsumables  = (flags & 0x04) != 0;
	settings.bNoWeightLimit    = (flags & 0x08) != 0;
	settings.bFreeShops        = (flags & 0x10) != 0;

	settings.eWeather          = static_cast<Weather>(in[i++]);
	settings.cHour             = static_cast<int8_t>(in[i++]);

	if (!IsValid(settings))
		return std::nullopt;

	return settings;
}

/// \returns the percentage in effect: normal (100) while cheats are off.
inline uint16_t Effective(const Settings& settings, uint16_t Settings::* pct)
{
	return settings.bOn ? settings.*pct : uint16_t { 100 };
}

/// \returns whether the toggle is in effect: never while cheats are off.
inline bool EffectiveFlag(const Settings& settings, bool Settings::* flag)
{
	return settings.bOn && settings.*flag;
}

/// EXP, NP and coin gains. Losses (base <= 0) aren't multiplied.
inline int64_t ScaleGain(int64_t base, uint16_t pct)
{
	if (base <= 0)
		return base;

	return std::min(base * pct / 100, MAX_GAIN);
}

/// A hit's damage. Values <= 0 (misses, heals, errors) pass through. A hit that does damage keeps
/// doing at least 1 unless the multiplier is 0, and never exceeds what the wire can carry.
inline int ScaleDamage(int base, uint16_t pct)
{
	if (base <= 0)
		return base;

	if (pct == 0)
		return 0;

	const int64_t scaled = static_cast<int64_t>(base) * pct / 100;
	return static_cast<int>(std::clamp<int64_t>(scaled, 1, MAX_DAMAGE));
}

/// The damage a hit would have done without the multiplier, for weapon wear.
inline int UnscaleDamage(int dealt, uint16_t pct)
{
	if (pct == 0 || pct == 100)
		return dealt;

	return static_cast<int>(static_cast<int64_t>(dealt) * 100 / pct);
}

/// A drop's chance out of 10,000. A zero chance stays zero, so no new items appear.
inline int ScaleDropChance(int per10000, uint16_t pct)
{
	if (per10000 <= 0)
		return per10000;

	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(per10000) * pct / 100, 10000));
}

/// A respawn delay in ms: faster with a higher speed, never slower than normal, and never below
/// RESPAWN_FLOOR_MS unless it already was.
inline int ScaleRegenTime(int ms, uint16_t speedPct)
{
	if (speedPct <= 100)
		return ms;

	const int scaled = static_cast<int>(static_cast<int64_t>(ms) * 100 / speedPct);
	return std::max(scaled, std::min(ms, RESPAWN_FLOOR_MS));
}

/// An item's +level from the last 3 digits of its ID: 0 is +0, then each line of 10 runs +1..+10.
inline int PlusOf(int ext)
{
	if (ext <= 0)
		return 0;

	return (ext - 1) % 10 + 1;
}

/// \returns the ext on the same line with the given +level, or -1. Only the first line (ext 0..10)
/// has a +0.
inline int ExtWithPlus(int ext, int plus)
{
	if (ext < 0 || ext > 999 || plus < 0 || plus > MAX_PLUS)
		return -1;

	const int line = ext == 0 ? 0 : (ext - 1) / 10;
	if (plus == 0)
		return line == 0 ? 0 : -1;

	const int result = line * 10 + plus;
	return result <= 999 ? result : -1;
}

/// AG_CHEAT_SETTINGS: [u32 seq][settings]
inline constexpr size_t AG_SETTINGS_SIZE = 4 + SETTINGS_SIZE;

/// AIServer applies a snapshot only when it's newer than the last one: Ebenezer's sockets to it
/// don't keep messages in order.
inline bool IsNewer(uint32_t seq, uint32_t lastSeq)
{
	return seq > lastSeq;
}

struct Zone
{
	int16_t sId;
	const char* szName;
};

/// The ordinary places a player can teleport to. Quest, event and war zones, where a player could
/// get stuck, aren't here.
inline constexpr std::array<Zone, 9> ZONES { {
	{ 21, "Moradon" },
	{ 1, "Luferson Castle" },
	{ 2, "El Morad Castle" },
	{ 11, "Karus Eslant" },
	{ 12, "El Morad Eslant" },
	{ 201, "Colony Zone" },
	{ 30, "Delos" },
	{ 32, "Desperation Abyss" },
	{ 33, "Hell Abyss" },
} };

inline bool IsTeleportZone(int zone)
{
	return std::any_of(
		ZONES.begin(), ZONES.end(), [zone](const Zone& entry) { return entry.sId == zone; });
}

/// The NPC kinds a player can teleport to: guards, merchants and other townsfolk, not monsters,
/// gates, objects or animals.
inline bool IsTeleportNpcType(int npcType)
{
	return npcType >= 11 && !(npcType >= 50 && npcType <= 68) && npcType != 99;
}

/// Monsters, as Genie counts them: below 10, except 1 and traps (5).
inline bool IsMonsterType(int npcType)
{
	return npcType < 10 && npcType != 1 && npcType != 5;
}

} // namespace Cheats

#endif // SHARED_CHEATSETTINGS_H
