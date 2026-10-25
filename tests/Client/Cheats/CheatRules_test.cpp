#include <gtest/gtest.h>

#include <Client/WarFare/CheatRules.h>
#include <shared/packets.h>

#include <set>
#include <string>
#include <vector>

/**
 * Tests the cheats window's rules: item search, the names of an item's +levels, multiplier labels
 * and the message for every result.
 */

TEST(CheatRules, SearchIsCaseInsensitiveAndSorted)
{
	const std::vector<Cheats::ItemName> items {
		{ 156210000, "Raptor" },
		{ 2, "raptor bow" },
		{ 3, "" },
		{ 110050000, "Dagger" },
		{ 110010000, "Dagger" },
	};

	EXPECT_EQ(Cheats::SearchItems(items, "RAP"), (std::vector<uint32_t> { 156210000, 2 }));
	EXPECT_EQ(
		Cheats::SearchItems(items, "dagger"), (std::vector<uint32_t> { 110010000, 110050000 }));
	EXPECT_TRUE(Cheats::SearchItems(items, "  ").empty());
	EXPECT_TRUE(Cheats::SearchItems(items, "").empty());
	EXPECT_EQ(Cheats::SearchItems(items, "a", 1).size(), 1u);
}

TEST(CheatRules, VariantLabels)
{
	EXPECT_EQ(Cheats::VariantLabel("Raptor", 5, "Upgrade", 5), "Raptor (+5)");
	EXPECT_EQ(Cheats::VariantLabel("Raptor", 0, "", 0), "Raptor");
	EXPECT_EQ(Cheats::VariantLabel("Raptor", 15, "Strength", 2), "Strength Raptor (+5)");
	EXPECT_EQ(Cheats::VariantLabel("Raptor", 120, "Javana Bow", 4), "Javana Bow");
	EXPECT_EQ(Cheats::VariantLabel("Raptor", 10, "Upgrade", 5), "Raptor (+10)");
}

TEST(CheatRules, MultiplierLabels)
{
	EXPECT_EQ(Cheats::MultiplierLabel(0), "x0 (no damage)");
	EXPECT_EQ(Cheats::MultiplierLabel(50), "x0.5");
	EXPECT_EQ(Cheats::MultiplierLabel(100), "x1 (normal)");
	EXPECT_EQ(Cheats::MultiplierLabel(2500), "x25");
}

// Every result has its own message, for every request.
TEST(CheatRules, EveryResultHasAMessage)
{
	std::set<std::string> messages;
	for (uint8_t result = CHEAT_OK; result <= CHEAT_NO_AI; result++)
	{
		const std::string message = Cheats::ResultMessage(CHEAT_GIVE_ITEM, result);
		EXPECT_FALSE(message.empty()) << int(result);
		messages.insert(message);
	}

	EXPECT_EQ(messages.size(), static_cast<size_t>(CHEAT_NO_AI + 1));
}
