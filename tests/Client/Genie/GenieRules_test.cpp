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

// Staying put (an archer on the spot the player picked): only what can be hit from where the
// player stands, attacker or not, and the hunting range doesn't apply.
TEST(ChooseTarget, StayPutOnlyInReach)
{
	const std::array<Monster, 3> monsters = { {
		{ .iID            = 1,
			.fX           = 5,
			.fZ           = 0,
			.bAttackingMe = true,
			.bInReach     = false },                            // a ranged attacker out of reach
		{ .iID = 2, .fX = 10, .fZ = 0, .bInReach = false }, // near, out of reach (behind a wall)
		{ .iID = 3, .fX = 30, .fZ = 0, .bInReach = true }, // beyond the hunting range, in bow reach
	} };
	EXPECT_EQ(ChooseTarget(monsters, 0, 0, 25, 0, 0, true), 3);

	// Moving, reach doesn't matter: the attacker first.
	EXPECT_EQ(ChooseTarget(monsters, 0, 0, 25, 0, 0, false), 1);

	const std::array<Monster, 1> outOfReach = {
		{ { .iID = 1, .fX = 2, .fZ = 0, .bAttackingMe = true, .bInReach = false } }
	};
	EXPECT_EQ(ChooseTarget(outOfReach, 0, 0, 25, 0, 0, true), -1);
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

// Out of a potion it can't buy back (coins under the floor, no room, or the merchant doesn't sell
// it), Genie goes to town as if buying were off.
TEST(GenieRules, CantRestockWhatCantBeBought)
{
	const RestockOffer offer {
		.bOn = true, .bMerchant = true, .bSold = true, .iUnitPrice = 160, .iRoom = 9999
	};
	EXPECT_TRUE(CanRestockFromEmpty(offer, {}, 50, 10'000, 5'000));

	EXPECT_FALSE(
		CanRestockFromEmpty(offer, {}, 50, 5'100, 5'000)); // above the floor, but not by one potion
	EXPECT_FALSE(CanRestockFromEmpty(
		{ .bOn = true, .bMerchant = true, .bSold = true, .iUnitPrice = 160, .iRoom = 0 }, {}, 50,
		10'000, 0));
	EXPECT_FALSE(CanRestockFromEmpty(
		{ .bOn = true, .bMerchant = true, .bSold = false, .iUnitPrice = 160, .iRoom = 9999 }, {},
		50, 10'000, 0));
	EXPECT_FALSE(CanRestockFromEmpty(
		{ .bOn = true, .bMerchant = false, .bSold = true, .iUnitPrice = 160, .iRoom = 9999 }, {},
		50, 10'000, 0));
	EXPECT_FALSE(CanRestockFromEmpty(
		{ .bOn = false, .bMerchant = true, .bSold = true, .iUnitPrice = 160, .iRoom = 9999 }, {},
		50, 10'000, 0));
	EXPECT_FALSE(CanRestockFromEmpty(offer, { .bDisabled = true }, 50, 10'000, 0));
	EXPECT_FALSE(CanRestockFromEmpty(offer, {}, 0, 10'000, 0)); // keeping none
}

TEST(GenieRules, RestockNowBuysBackToKeep)
{
	const RestockOffer offer {
		.bOn = true, .bMerchant = true, .bSold = true, .iUnitPrice = 160, .iRoom = 9999
	};
	EXPECT_EQ(RestockNow(offer, {}, 9, 20, 10'000, 0), 11);
	EXPECT_EQ(RestockNow(offer, {}, 9, 20, 1'000, 0), 6);
	EXPECT_EQ(RestockNow(offer, {}, 20, 20, 10'000, 0), 0);
	EXPECT_EQ(
		RestockNow(
			{ .bOn = true, .bMerchant = true, .bSold = false, .iUnitPrice = 160, .iRoom = 9999 },
			{}, 9, 20, 10'000, 0),
		0);
}

// A merchant sells what's in its selling group, with the shop's extension (CUITransactionDlg).
TEST(GenieRules, BuysOnlyWhatTheMerchantSells)
{
	EXPECT_TRUE(IsSoldBy(253, 389010000, 253000));
	EXPECT_FALSE(IsSoldBy(254, 389010000, 253000)); // another selling group
	EXPECT_FALSE(IsSoldBy(253, 389010001, 253000)); // the same item, another extension
	EXPECT_TRUE(IsSoldBy(140, 391010003, 140003));
}

// A drop that didn't open from here (another region) is walked to within the hunting range and
// for a limited time; while something attacks, the fight comes first and the walk waits.
TEST(GenieRules, WalksToDropOnlyWhenSafeAndNear)
{
	EXPECT_EQ(WalkToDrop(false, 20.0f, 30.0f, 0.0f), DropWalk::Walk);
	EXPECT_EQ(WalkToDrop(false, 20.0f, 30.0f, LOOT_WALK_TIME - 0.1f), DropWalk::Walk);
	EXPECT_EQ(WalkToDrop(true, 20.0f, 30.0f, 0.0f), DropWalk::Wait);
	EXPECT_EQ(WalkToDrop(false, 31.0f, 30.0f, 0.0f), DropWalk::GiveUp);
	EXPECT_EQ(WalkToDrop(true, 31.0f, 30.0f, 0.0f), DropWalk::GiveUp);
	EXPECT_EQ(WalkToDrop(false, 20.0f, 30.0f, LOOT_WALK_TIME), DropWalk::GiveUp);

	// Staying put, Genie never walks: a drop in another region is left.
	EXPECT_EQ(WalkToDrop(false, 20.0f, 30.0f, 0.0f, true), DropWalk::GiveUp);
}

// Skills only (no basic attack), nothing castable this moment: walk up to the target to get a
// ready skill within reach; while all are cooling down, until any reaches. A melee skill reaches
// once the target is within attack reach.
TEST(GenieRules, ApproachesForSkills)
{
	const SkillChoice meleeReadyFar {
		.iID = 1, .bReady = true, .bUsable = true, .bInRange = false
	};
	const SkillChoice rangedCoolingNear {
		.iID = 2, .bReady = false, .bUsable = true, .bInRange = true
	};
	const SkillChoice rangedCoolingFar {
		.iID = 3, .bReady = false, .bUsable = true, .bInRange = false
	};
	const SkillChoice unusableFar { .iID = 4, .bReady = true, .bUsable = false, .bInRange = false };
	const SkillChoice heldFar {
		.iID = 5, .bReady = true, .bUsable = true, .bInRange = false, .bHeld = true
	};

	EXPECT_TRUE(ShouldApproach(std::array { meleeReadyFar }));
	EXPECT_TRUE(ShouldApproach(std::array { rangedCoolingNear, meleeReadyFar }));
	EXPECT_TRUE(ShouldApproach(std::array { rangedCoolingFar }));
	EXPECT_FALSE(ShouldApproach(std::array { rangedCoolingNear }));
	EXPECT_FALSE(ShouldApproach(std::array { rangedCoolingNear, heldFar }));
	EXPECT_FALSE(ShouldApproach(std::array { unusableFar }));
	EXPECT_FALSE(ShouldApproach(std::span<const SkillChoice> {}));
}

// While the player has a window open that handles items (loot, shop, warehouse, trade, repair) or
// an item move is waiting for the server, Genie leaves items alone: its results would land in the
// player's window, or overwrite the move the player is making.
TEST(GenieRules, LeavesItemsAloneWhileThePlayerHandlesThem)
{
	EXPECT_FALSE(PlayerHandlesItems({}));
	EXPECT_TRUE(PlayerHandlesItems({ .bLoot = true }));
	EXPECT_TRUE(PlayerHandlesItems({ .bShop = true }));
	EXPECT_TRUE(PlayerHandlesItems({ .bWarehouse = true }));
	EXPECT_TRUE(PlayerHandlesItems({ .bTrade = true }));
	EXPECT_TRUE(PlayerHandlesItems({ .bRepair = true }));
	EXPECT_TRUE(PlayerHandlesItems({ .bWaiting = true }));
}

// With basic attack off, Genie sits for MP only when no attack skill can be paid for, not while
// they're only cooling down.
TEST(GenieRules, SkillsCoolingDownDontMakeGenieSit)
{
	EXPECT_TRUE(
		AnyAffordable(std::array { SkillChoice { .iID = 1, .bReady = false, .bUsable = true } }));
	EXPECT_FALSE(
		AnyAffordable(std::array { SkillChoice { .iID = 1, .bReady = true, .bUsable = false } }));
	EXPECT_FALSE(AnyAffordable(std::span<const SkillChoice> {}));
}

// Two unreachable targets taking turns as the nearest: each one's time without progress adds up,
// so both get given up.
TEST(GenieRules, StalledTargetsTakingTurnsAreSkipped)
{
	TargetProgress progress;
	for (int i = 0; i < 40; i++)
		progress.Reset(i % 2 == 0 ? 1 : 2, static_cast<float>(i) * 0.5f);

	progress.Reset(1, 20.0f); // target 1's turns so far: 20 x 0.5 s
	EXPECT_TRUE(progress.ShouldSkip(20.0f));

	// Progress on it starts its count over.
	progress.OnProgress(20.0f);
	EXPECT_FALSE(progress.ShouldSkip(29.9f));
}

// A repair or buy waits 5 s for its result. A result after that is Genie's late one and is
// dropped, unless the player has the window open that expects one.
TEST(GenieRules, LateUpkeepResultsAreDropped)
{
	PendingRequest request;
	EXPECT_EQ(request.Claim(false), ResultOwner::Player); // nothing sent

	request.Send(10.0f);
	request.Tick(14.0f);
	EXPECT_TRUE(request.IsWaiting());
	EXPECT_EQ(request.Claim(true), ResultOwner::Genie); // in time, even with a window open
	EXPECT_FALSE(request.IsWaiting());

	request.Send(20.0f);
	request.Tick(25.0f);
	EXPECT_FALSE(request.IsWaiting());
	EXPECT_EQ(request.Claim(false), ResultOwner::Dropped); // late
	EXPECT_EQ(request.Claim(false), ResultOwner::Player);  // only the one

	request.Send(30.0f);
	request.Tick(35.0f);
	EXPECT_EQ(request.Claim(true), ResultOwner::Player); // the player's window expects it

	request.Send(40.0f);
	request.Cancel();                                    // leaving the game
	EXPECT_EQ(request.Claim(false), ResultOwner::Player);
}

// Monsters by the NPC type the server sends: not traps, guards, merchants or objects (camp spawns
// in zone 30 are type 64 with no nation).
TEST(GenieRules, MonstersByNpcType)
{
	for (int iType : { 0, 2, 3, 4, 6, 9 })
		EXPECT_TRUE(IsMonsterType(static_cast<uint8_t>(iType))) << iType;

	for (int iType : { 1, 5, 11, 21, 24, 64, 255 })
		EXPECT_FALSE(IsMonsterType(static_cast<uint8_t>(iType))) << iType;
}
