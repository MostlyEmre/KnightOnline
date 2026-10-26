# Cheats menu — design

## Intent

This is a single-player build. The player wants to bend the game when they feel like it: level
faster, hit harder, get the item they're missing (the Raptor +5 → +6 needs a Blessed Upgrade Scroll
that costs 2.4M coins), jump to a zone, and so on. All of that should come from one in-game window,
without making the character a GM. A GM character is ignored by monsters, and its damage is
replaced by a fixed value (`src/Server/AIServer/Npc.cpp:2418`, `src/Server/AIServer/User.cpp:160`),
which ruins normal play.

As with Genie, the server gets the final say: a server that doesn't allow cheats never offers
them, and a server that knows nothing about cheats is never disturbed by them.

## Goals

A Cheats window with these cheats, all done on the server where the game already computes the
value. None of it is faked in the client.

**Multipliers** (each a value the player picks; ×1 means normal):
- EXP gained
- Damage the player deals
- Damage the player takes (×0 = god mode)
- Coins dropped by monsters
- Item drop chance
- National Points gained
- Monster respawn speed

**Character**:
- Set level
- Add stat points and skill points; reset stats and skills for free
- Refill HP and MP; infinite MP (skills cost no MP)
- Set coins, National Points and Knight Cash

**Items**:
- Give any item: search by name, pick a count and a +level
- Set an equipped item's +level directly
- Repair everything; no durability loss
- Arrows and potions aren't used up
- No weight limit
- NPC shops charge nothing

**World**:
- Teleport to a zone or town, or straight to a named NPC
- Kill every monster around the player
- Set the weather and the time of day

**Switches**:
- A master switch in the window, **Cheats on**, off by default. While it's off, nothing in the
  menu has any effect: the multipliers act as ×1, the toggles act as off, and the one-shot actions
  are disabled. The settings are kept, so turning it back on restores them.
- The settings are saved by the server and survive a restart.

## Non-goals

- GM features: banning, chat moderation, announcements, events, sieges, wars.
- Anything for other players: no targeting other characters, and no party-wide effects.
- Cheats in PvP: there are no other players here.
- Client-only effects (speed hacks, wall hacks, seeing through fog). The servers check movement,
  and faking it would desync.
- Making the existing GM `+` chat commands work. They stay as they are.
- Spawning monsters. Every monster, bosses included, spawns when AIServer starts (each starts as a
  first spawn, with no delay), and the app restarts the servers each time it opens. The respawn
  speed multiplier covers the rest. (The player's call.)

## Anti-goals (a failure even if it technically works)

- Cheats that a server not allowing them would honour, or a client that sends cheat messages to a
  server that never announced them (Ebenezer disconnects clients that send unknown opcodes).
- A cheat that desyncs the client from the server: HP that's wrong on screen, items that vanish on
  relog, a level that resets, monsters that are dead on one side and alive on the other.
- A corrupted character or database: items with IDs that don't exist, levels outside the game's
  range, coins or points that overflow.
- Cheats that are on without the player knowing.
- Breaking the Windows build or the SQL Server backend; anything that would need an upstream PR.

## Constraints

- Work stays in the MostlyEmre/KnightOnline fork, branch `macos-port`. The Windows build and both
  database backends keep working.
- No changes to game assets. The window is built in code on `CUIArtWindow`
  (src/Client/WarFare/UIArt.h), like Options, the Power-up Store and Genie.
- Protocol: the additions under "Messages" below, approved by the player. Nothing else.
- Values that are written to the database must stay within the game's ranges:
  - levels between 1 and the game's top level;
  - coins at most 2,100,000,000 (the game's cap);
  - +levels only to item IDs that exist in the server's ITEM table.
- Multipliers apply to everything on the server. With one player, that means the player.
- Scripted test runs never take focus or show modal dialogs.

## How it fits together (decided)

### Who owns what

The game splits its work between two servers, so the cheats split the same way:

- **Ebenezer** (the main game server) owns:
  - the settings and the master switch, and saves them;
  - EXP and NP gains (`CAISocket::RecvUserExp`, `CUser::ExpChange`, `CUser::LoyaltyChange`);
  - items, coins, level and points;
  - durability (`CUser::ItemWoreOut`);
  - shops (`CUser::ItemTrade`), weight, arrow and potion use, MP costs (`CMagicProcess`);
  - teleports (`CUser::ZoneChange`), weather and time;
  - damage from monster skills, area spells and timed damage. AIServer hands these back to
    Ebenezer, which computes them in `CMagicProcess` (AISocket.cpp → `MagicPacket`), decides those
    deaths, and sends the resulting HP to AIServer;
  - the coin multiplier, applied when coins are picked up (see "Coins" below).
- **AIServer** (monsters) owns:
  - damage dealt to monsters (`CUser::GetDamage`, `CMagicProcess::GetMagicDamage`);
  - damage from monster basic attacks;
  - drop chances (`CNpc::GiveNpcHaveItem`);
  - respawn timing (`m_sRegenTime`);
  - killing monsters around the player.

  Damage is scaled where it's computed, so "damage taken" is applied in both servers. Each server
  decides the deaths for the damage it computes. Scaling AIServer's damage later, in Ebenezer, would
  let the two disagree about the player's HP.

### Messages

**Client ↔ Ebenezer: a new opcode WIZ_CHEAT**, with a handshake that works like Genie's (WIZ_GENIE
0x97):
- After the player enters the game, a server that supports cheats sends
  `WIZ_CHEAT [CHEAT_AVAILABLE][uint8 allowed]`, followed by the current settings. A server without
  cheats sends nothing, so the client never offers the window there.
- Only after `allowed = 1` does the client send cheat requests. Each request gets a reply: whether
  it worked, and why not if it didn't. Whenever the settings change, the server sends them all back.
- The client needs those settings too, because it checks some rules itself before asking the
  server:
  - it won't cast a skill without the MP for it;
  - it won't buy something it can't afford by its own price list;
  - it won't pick up or buy past its weight limit.

  When a cheat relaxes one of these rules, the client's check relaxes with it.

**Ebenezer → AIServer: a new internal message** (an AG_ opcode) carrying the AI-side settings:
- the damage multipliers, the drop multiplier, and the respawn speed;
- the master switch.

Ebenezer sends it when AIServer connects and whenever those settings change. Killing the monsters
around the player gets its own request.

Both sets of opcodes go in `src/shared/packets.h`, documented the way `e_GenieOpcode` is. Their
values and the sub-opcodes are the builder's call.

### Permission

- Ebenezer's ini gets `[CHEATS] ALLOWED`. It's **off** by default in the code, so someone hosting
  this fork for other people doesn't hand everyone a cheat menu. The packaged app turns it on for
  its own server.
- With it off, the server announces `allowed = 0` and refuses every cheat request.

### The window

- A key that 1.298 leaves unbound opens and closes it (the builder checks the client's key
  handling). Esc and the close button close it too. It's modal while open, like Options and Genie.
- Pages: **Multipliers**, **Character**, **Items**, **World**, plus the **Cheats on** switch at
  the top of every page.
- Multipliers are picked from fixed steps, not typed. Suggested steps: ×1, ×2, ×3, ×5, ×10, ×25,
  ×50, ×100. Damage taken also gets ×0 and ×0.5. (My call.)
- Item search runs in the client over its own item table: type part of a name, see matching items
  with their icons, then pick a count and a +level. A +level is offered only when that item ID
  exists on the server: the client's tables compose about 19,000 IDs the server's ITEM table
  doesn't have, so after a pick the client asks the server which IDs of that item exist.
- NPC and zone searches are answered by the server, which has the names and positions.
- Results and refusals go to the message window ("Gave Blessed Upgrade Scroll ×1.", "Can't: the
  bag is full.").
- While cheats are on, a small "Cheats" label shows on screen, as Genie's indicator does. That way
  it's never on without the player seeing it. (My call.)

### Behaviour details (decided)

- **EXP and NP multipliers** apply to what the player gains from monsters and quests. Losses (EXP
  lost on death) aren't multiplied.
- **Item drop chance** multiplies each drop's chance, capped at certain. It doesn't add new items
  to a monster's drop list.
- **Damage dealt** applies to basic attacks and skills against monsters.
- **Damage taken** applies to every kind of damage from monsters and from NPCs that attack, guards
  included, skills included.
- **Coins**: a coin drop is capped at 32,000. AIServer clamps it (`GiveNpcHaveItem`), and the drop
  and pickup messages carry the amount as a 16-bit number. So the coin multiplier is applied in
  Ebenezer when the coins are picked up: the player gets the full multiplied amount through the
  usual coin update. The loot window still shows the unmultiplied amount. (My call: widening the
  messages would change existing protocol.)
- **Respawn speed** divides the respawn time of monsters killed after the change.
- **Set level** works up or down:
  - going up grants the stat and skill points the game gives for those levels;
  - going down also resets stats and skills for free, so the points fit the new level;
  - EXP goes to the start of the new level. (My call.)
- **Give item**:
  - stackable items come in the chosen count;
  - other items come one per free bag slot, up to the count;
  - it refuses when the bag is full.
- **Set an equipped item's +level**: swaps the item for the same item at that +level, keeping its
  durability as a share of the maximum. It refuses when that ID doesn't exist.
- **Kill every monster around the player**:
  - monsters only, never guards, merchants or other NPCs;
  - within 50 m of the player: the range in which AIServer credits a kill's EXP and drops
    (`NPC_EXP_RANGE`); a monster killed farther away would give nothing;
  - each kill counts as the player's: EXP, drops, NP and Knight Cash as usual.
- **Weather and time** override the server's own cycle until the player picks "Normal".
- **Teleports skip the game's entry rules**. Normally `CUser::ZoneChange` refuses:
  - the enemy nation's home zone outside a war;
  - frontier and war zones below level 20;
  - the frontier zone during a war.

  The cheat lets the player go anyway. The zones offered are a hand-picked list of ordinary places:
  towns, both homelands, Moradon, Eslant, Ronark Land, and the like. Zones where the player could
  get stuck are left out: the quest and event zones (51–55), Monster Storage (91) and the war zones
  (101–111). The builder checks each listed zone has a spawn point. (My call.)
- **Teleport to an NPC** puts the player next to it, in its zone, if that zone is on the list.
- **NPC shops charge nothing**: buying from NPC shops costs 0 coins. Selling pays as usual. The
  Power-up Store is unaffected.

## Left to the builder

- Code structure: a settings object in Ebenezer, the window class and its pages, the save format
  for the settings (next to Ebenezer's ini), and opcode values.
- How AIServer applies each multiplier.
- Exact step lists for counts, levels and weather, within the decisions above.
- Tests:
  - Ebenezer.Tests for the handshake, the permission switch and each Ebenezer-side cheat
    (EXP and NP multipliers, give item, set level, +level, shops, durability, weight);
  - AIServer has no test project, so its multiplier math goes in pure functions testable from an
    existing test project;
  - scripted app runs, plus instructions for the player's own playtest, for the rest.
