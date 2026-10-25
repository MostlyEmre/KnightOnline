#ifndef SHARED_SPELLDAMAGE_H
#define SHARED_SPELLDAMAGE_H

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

/// \brief A damage spell's damage to a monster, as AIServer works it out (the official 1.298
/// formula, CMagicProcess::GetMagicDamage), with the monster's resistance capped. Standard
/// library only.
namespace SpellDamage
{

/// The formula takes the monster's resistance as a share of 200. OpenKO-db gives many monsters
/// more (Hyde and Antares 500, some bosses 3000); past about 222 the damage came out negative and
/// the sign flip turned the spell into a heal, so OpenKO caps resistance here.
inline constexpr int MAX_RESISTANCE = 200;

/// \param totalHit The spell's damage (negative for a damage spell).
/// \param resistance The monster's resistance to the spell's attribute.
/// \param dexPoint The caster's points behind the spell.
/// \param rightHandDamage The caster's right-hand weapon damage.
/// \param randomBetween Returns a random number between its two arguments, inclusive.
/// \returns The damage, with the spell's sign.
template <typename Rand>
int16_t ToMonster(
	int totalHit, int resistance, int dexPoint, int rightHandDamage, const Rand& randomBetween)
{
	bool bSign = true; // false이면 -, true이면 +

	resistance = std::min(resistance, MAX_RESISTANCE);
	totalHit   = (totalHit * (dexPoint + 20)) / 170;
	if (totalHit < 0)
	{
		totalHit = std::abs(totalHit);
		bSign    = false;
	}

	auto damage = static_cast<int16_t>(totalHit - (0.7f * totalHit * resistance / 200));
	int random  = randomBetween(0, damage);
	damage      = static_cast<int16_t>(
        (0.7f * (totalHit - (0.9f * totalHit * resistance / 200))) + 0.2f * random);
	damage = static_cast<int16_t>(damage + rightHandDamage);

	if (!bSign && damage != 0)
		damage = static_cast<int16_t>(-damage);

	return damage;
}

} // namespace SpellDamage

#endif // SHARED_SPELLDAMAGE_H
