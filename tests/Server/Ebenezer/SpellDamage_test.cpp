#include <gtest/gtest.h>

#include <shared/SpellDamage.h>

/**
 * Tests a damage spell's damage to a monster: the official formula, and the resistance cap that
 * keeps it from turning into a heal.
 */
namespace
{

int Lowest(int min, int /*max*/)
{
	return min;
}

int Highest(int /*min*/, int max)
{
	return max;
}

} // namespace

// Below the cap the numbers are the official ones: a 500 damage spell with 150 points behind it
// is 500 before resistance; 100 resistance leaves 192.5 + up to a fifth of 325, then the
// right hand's damage. Damage keeps the spell's sign (negative).
TEST(SpellDamageTest, OfficialFormulaBelowTheCap)
{
	EXPECT_EQ(SpellDamage::ToMonster(-500, 100, 150, 10, Lowest), -202);
	EXPECT_EQ(SpellDamage::ToMonster(-500, 100, 150, 10, Highest), -267);
	EXPECT_EQ(SpellDamage::ToMonster(-500, 0, 150, 0, Lowest), -350);
}

// Hyde and Antares resist 500 and some bosses 3000; past about 222 the formula went negative and
// the sign flip healed the monster. Resistance now stops at 200.
TEST(SpellDamageTest, HighResistanceStillDamages)
{
	EXPECT_EQ(SpellDamage::ToMonster(-500, 500, 150, 0, Lowest), -35);
	EXPECT_EQ(SpellDamage::ToMonster(-500, 500, 150, 0, Highest), -65);
	EXPECT_EQ(SpellDamage::ToMonster(-500, 3000, 150, 0, Highest),
		SpellDamage::ToMonster(-500, SpellDamage::MAX_RESISTANCE, 150, 0, Highest));
}
