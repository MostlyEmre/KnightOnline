#include <gtest/gtest.h>

#include <Client/WarFare/GenieRules.h>

#include <array>
#include <string>

using namespace Genie;

TEST(KindOfItemClass, Mapping)
{
	for (int iClass : { 97, 98, 255, 120 })
		EXPECT_EQ(KindOfItemClass(iClass), ItemKind::Consumable) << iClass;

	for (int iClass : { 11, 45, 80, 91, 94, 100, 110, 130, 210, 225, 240 })
		EXPECT_EQ(KindOfItemClass(iClass), ItemKind::Equipment) << iClass;

	for (int iClass : { 0, 10, 81, 90, 95, 96, 99, 101, 209, 241, 251 })
		EXPECT_EQ(KindOfItemClass(iClass), ItemKind::Other) << iClass;
}

TEST(GradeOfItemAttrib, Mapping)
{
	EXPECT_EQ(GradeOfItemAttrib(0), ItemGrade::Normal);
	EXPECT_EQ(GradeOfItemAttrib(1), ItemGrade::Magic);
	EXPECT_EQ(GradeOfItemAttrib(2), ItemGrade::Rare);
	EXPECT_EQ(GradeOfItemAttrib(3), ItemGrade::Rare);
	EXPECT_EQ(GradeOfItemAttrib(4), ItemGrade::Unique);
	EXPECT_EQ(GradeOfItemAttrib(5), ItemGrade::Normal);
}

TEST(WantsLoot, GoldAlways)
{
	LootFilter filter;
	filter.bConsumables = filter.bEquipment = filter.bOther = false;
	filter.iMinSellPrice                                    = 1'000'000;
	EXPECT_TRUE(WantsLoot(filter, { .bGold = true }));
}

TEST(WantsLoot, KindGradeAndPrice)
{
	LootFilter filter;
	filter.bConsumables  = false;
	filter.bMagic        = false;
	filter.iMinSellPrice = 100;

	// The kind has to be ticked.
	EXPECT_FALSE(WantsLoot(filter, { .eKind = ItemKind::Consumable, .iSellPrice = 500 }));
	EXPECT_TRUE(WantsLoot(filter, { .eKind = ItemKind::Other, .iSellPrice = 500 }));

	// The grade only counts for equipment.
	EXPECT_FALSE(WantsLoot(
		filter, { .eKind = ItemKind::Equipment, .eGrade = ItemGrade::Magic, .iSellPrice = 500 }));
	EXPECT_TRUE(WantsLoot(
		filter, { .eKind = ItemKind::Equipment, .eGrade = ItemGrade::Rare, .iSellPrice = 500 }));
	EXPECT_TRUE(WantsLoot(
		filter, { .eKind = ItemKind::Other, .eGrade = ItemGrade::Magic, .iSellPrice = 500 }));

	// It has to sell for at least the minimum.
	EXPECT_FALSE(WantsLoot(filter, { .eKind = ItemKind::Other, .iSellPrice = 99 }));
	EXPECT_TRUE(WantsLoot(filter, { .eKind = ItemKind::Other, .iSellPrice = 100 }));
}

TEST(ChooseTarget, AttackerFirst)
{
	// The attacker is farther away, off the list and out of range: it still comes first.
	const std::array<Monster, 2> monsters = { {
		{ .iID = 1, .fX = 2, .fZ = 0 },
		{ .iID = 2, .fX = 60, .fZ = 0, .bAttackingMe = true, .bOnList = false },
	} };
	EXPECT_EQ(ChooseTarget(monsters, 0, 0, 25, 0, 0), 2);
}

TEST(ChooseTarget, NearestInRangeOnList)
{
	const std::array<Monster, 4> monsters = { {
		{ .iID = 1, .fX = 26, .fZ = 0 },                   // out of range of the origin
		{ .iID = 2, .fX = 10, .fZ = 0, .bOnList = false }, // not on the list
		{ .iID = 3, .fX = 20, .fZ = 0 },
		{ .iID = 4, .fX = 0, .fZ = 15 },
	} };

	// Nearest to the player (standing at 18, 0), among those in range of the origin (0, 0).
	EXPECT_EQ(ChooseTarget(monsters, 0, 0, 25, 18, 0), 3);
	EXPECT_EQ(ChooseTarget(monsters, 0, 0, 25, 0, 10), 4);

	// A monster at 26 is never chosen with a range of 25.
	const std::array<Monster, 1> farOnly = { { { .iID = 1, .fX = 26, .fZ = 0 } } };
	EXPECT_EQ(ChooseTarget(farOnly, 0, 0, 25, 26, 0), -1);
	EXPECT_EQ(ChooseTarget({}, 0, 0, 25, 0, 0), -1);
}

TEST(FirstCastable, FirstReadyUsableInRangeNotHeld)
{
	const std::array<SkillChoice, 4> skills = { {
		{ .iID = 1, .bReady = false, .bUsable = true, .bInRange = true },
		{ .iID = 2, .bReady = true, .bUsable = true, .bInRange = true, .bHeld = true },
		{ .iID = 3, .bReady = true, .bUsable = true, .bInRange = true },
		{ .iID = 4, .bReady = true, .bUsable = true, .bInRange = true },
	} };
	EXPECT_EQ(FirstCastable(skills), 3);
	EXPECT_EQ(FirstCastable({}), -1);
}

TEST(BelowPercent, Basics)
{
	EXPECT_TRUE(BelowPercent(69, 100, 70));
	EXPECT_FALSE(BelowPercent(70, 100, 70));
	EXPECT_FALSE(BelowPercent(0, 0, 70));
}

TEST(CanTown, HalfHpRule)
{
	EXPECT_TRUE(CanTown(50, 100, false));
	EXPECT_FALSE(CanTown(49, 100, false));
	EXPECT_FALSE(CanTown(100, 100, true));
	EXPECT_FALSE(CanTown(0, 0, false));
}

TEST(TownReason, Order)
{
	const Supplies supplies { .iFreeSlots = 0,
		.bUsesHpPotion                    = true,
		.iHpPotions                       = 0,
		.bUsesMpPotion                    = true,
		.iMpPotions                       = 0,
		.bUsesArrows                      = true,
		.iArrows                          = 0 };
	TownRules rules;
	EXPECT_EQ(TownReason(supplies, rules), StopReason::BagFull);

	rules.bBagFull = false;
	EXPECT_EQ(TownReason(supplies, rules), StopReason::OutOfHpPotions);

	rules.bNoHpPotions = false;
	EXPECT_EQ(TownReason(supplies, rules), StopReason::OutOfMpPotions);

	rules.bNoMpPotions = false;
	EXPECT_EQ(TownReason(supplies, rules), StopReason::OutOfArrows);

	rules.bNoArrows = false;
	EXPECT_EQ(TownReason(supplies, rules), StopReason::None);
}

TEST(TownReason, OnlyWhenInUse)
{
	// No potions or arrows, but none of them in use: nothing to go to town for.
	const Supplies supplies { .iFreeSlots = 5 };
	EXPECT_EQ(TownReason(supplies, {}), StopReason::None);
}

TEST(RestockCount, LimitedByCoinFloorAndStack)
{
	// 40 wanted, but 600 coins above the floor buy 20 at 30 each.
	EXPECT_EQ(RestockCount(10, 50, 30, 1000, 400, 9999), 20);

	// Limited by the room on the stack.
	EXPECT_EQ(RestockCount(10, 50, 30, 100000, 0, 15), 15);

	// Nothing wanted, nothing to spend, or no price.
	EXPECT_EQ(RestockCount(50, 50, 30, 1000, 0, 9999), 0);
	EXPECT_EQ(RestockCount(10, 50, 30, 400, 400, 9999), 0);
	EXPECT_EQ(RestockCount(10, 50, 0, 1000, 0, 9999), 0);
}

// Review Focus 1: a target there's no progress on is given up, and left alone for a while.
TEST(GenieRules, SkipsStalledTarget)
{
	TargetProgress progress;
	progress.Reset(7, 100.0f);
	EXPECT_FALSE(progress.ShouldSkip(109.0f));

	progress.OnProgress(105.0f);
	EXPECT_FALSE(progress.ShouldSkip(114.0f));
	EXPECT_TRUE(progress.ShouldSkip(115.0f));

	progress.Skip(7, 115.0f);
	EXPECT_TRUE(progress.IsSkipped(7, 144.0f));
	EXPECT_FALSE(progress.IsSkipped(7, 145.0f));
	EXPECT_FALSE(progress.IsSkipped(8, 120.0f));
}

// Review Focus 2: no answer to GENIE_START.
TEST(GenieRules, StartTimesOut)
{
	EXPECT_FALSE(StartTimedOut(10.0f, 14.9f));
	EXPECT_TRUE(StartTimedOut(10.0f, 15.0f));
}

// Review Focus 3: a loot result that says the bag is full.
TEST(GenieRules, LootResultFillsBag)
{
	EXPECT_EQ(LootResultReason(6), StopReason::BagFull);
	EXPECT_EQ(LootResultReason(7), StopReason::BagFull);
	EXPECT_EQ(LootResultReason(1), StopReason::None);
	EXPECT_EQ(LootResultReason(0), StopReason::None);
}

// Review Focus 4: out of MP with nothing to cast, Genie sits until MP is back to half.
TEST(GenieRules, SitsToRecoverMana)
{
	EXPECT_TRUE(ShouldSitToRecover(false, false, 10, 100, false));
	EXPECT_FALSE(ShouldSitToRecover(true, false, 10, 100, false));  // being attacked
	EXPECT_FALSE(ShouldSitToRecover(false, true, 10, 100, false));  // something to cast
	EXPECT_FALSE(ShouldSitToRecover(false, false, 50, 100, false)); // half MP already

	// Once sitting, it stays down until half, even when a skill becomes affordable.
	EXPECT_TRUE(ShouldSitToRecover(false, true, 30, 100, true));
	EXPECT_FALSE(ShouldSitToRecover(false, true, 50, 100, true));
	EXPECT_FALSE(ShouldSitToRecover(true, false, 30, 100, true));
}

// Review Focus 5: a refused buy stops restocking that item.
TEST(GenieRules, RefusedBuyDisablesRestock)
{
	EXPECT_FALSE(WantsRestock({ .bDisabled = true }, 0, 50));
	EXPECT_TRUE(WantsRestock({ .bDisabled = false }, 0, 50));
	EXPECT_FALSE(WantsRestock({ .bDisabled = false }, 50, 50));
}

TEST(StopMessage, SaysWhy)
{
	EXPECT_EQ(std::string(StopMessage(StopReason::BagFull)), "Genie stopped: your bag is full.");
	EXPECT_EQ(
		std::string(StopMessage(StopReason::MovedByHand)), "Genie stopped: you took control.");
	EXPECT_EQ(std::string(StopMessage(StopReason::NoReply)),
		"Genie couldn't start: the server didn't answer.");
	EXPECT_EQ(std::string(StopMessage(StopReason::None)), "");
}
