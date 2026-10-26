# Genie (auto-hunt) — design

## Intent

Newer Knight Online versions have Genie: the client plays for you. You stand somewhere, press
Start, and walk away; you come back to more experience, more coins and the loot worth keeping.
The player wants that in this single-player build, to earn XP and money without grinding by hand.

The fork's stance is "this is a single-player implementation; use it if you want". The player's
worry is the what-ifs: someone runs this fork as a multiplayer server, or points this client at a
server that doesn't want bots. So the server gets the final say on whether Genie may run, and a
server that knows nothing about Genie must never be disturbed by it.

## Goals

- **Hunt on its own**, anywhere, without the player at the keyboard:
  - fight nearby monsters with the player's chosen attack skills and/or basic attack;
  - keep the player alive with HP/MP potions, heal skills and buffs;
  - loot what's worth keeping and leave the rest;
  - restock potions and arrows in the field, if the player turns that on, so it can hunt for
    hours;
  - stay near where it was started;
  - take the player to town (the /town command) and stop when it can't go on.
- **Configurable per character**, in a window that looks like the game's own (like the Power-up
  Store and Options windows), with settings remembered between sessions.
- **Server-controlled**: the server announces whether Genie is allowed; the client only offers it
  when it is. The single-player server allows it by default; a server's config can turn it off.

## Non-goals

- Genie time, the Spirit of Genie item, or any cost to run it.
- Party features (healing members, following the leader's target). The player plays alone.
- Pets, transformations, curse curing, quests, PvP.
- Walking anywhere in town. Town paths are a mess, and none of this needs them.
- Detecting or blocking third-party bots.

## Anti-goals (a failure even if it technically works)

- Getting the player disconnected from a server that doesn't know Genie.
- Getting the player killed or stuck over and over: chasing monsters across the map, running into
  guards or other players, wandering off from where it was started.
- Attacking anything but monsters: no players, NPCs or guards.
- Picking up junk until the bag is full of worthless items.
- A window that blocks the game while Genie runs, or a Genie the player can't easily see is on.
- Breaking the Windows build or the SQL Server backend; anything that would need an upstream PR.

## Constraints

- Work stays in the MostlyEmre/KnightOnline fork, branch `macos-port`. The Windows build and both
  database backends keep working.
- The approved protocol change is the Genie handshake below, nothing more. Any other protocol
  change, or any change to game assets, needs the player's OK first.
- The hunting uses only packet types the client already sends (move, attack, magic, bundle open,
  item get, repair, home). It keeps the game's rules (cooldowns, MP costs, attack and skill ranges,
  the /town rule below). The exceptions are reach: looting and repairs work from where the player
  stands, as in newer official Genie, and potions and arrows are bought from a merchant who isn't
  nearby. The player asked for remote looting and for field restocking. (My call where the line
  falls.)
- The 1.298 assets have no Genie art or text. The window is built in code from the message box
  art, on `CUIArtWindow` (src/Client/WarFare/UIArt.h), like `CUIPowerUpStore` and `CUIOptions`.
- Scripted test runs never take focus or show modal dialogs (see port test notes).

## How it fits together (decided)

### Handshake: WIZ_GENIE (0x97)

0x97 is unused in 1.298; it's the opcode later official versions use for Genie.

Open-KO's Ebenezer, in a release build, **disconnects a client that sends an opcode it doesn't
know** (User::Parsing's default case). Presumably the official server does the same. So the client
must never send 0x97 unless the server has said it understands it:

- After the player enters the game, a server that supports Genie sends
  `WIZ_GENIE [GENIE_AVAILABLE][uint8 allowed]`. A server without Genie sends nothing, so the
  client never offers Genie there. A client without Genie ignores the unknown opcode; the 1.298
  client already ignores unknown opcodes.
- Only after `allowed = 1` does the client let the player start Genie. It tells the server
  `WIZ_GENIE [GENIE_START]` and `[GENIE_STOP]`. The server answers a start with
  `[GENIE_START][uint8 ok]`: it refuses a start if Genie isn't allowed or the player is dead.
- Ebenezer's config (the server ini it already reads at startup) gets a Genie switch, on by
  default. Turning it off makes the server announce `allowed = 0` and refuse starts. It takes
  effect when the server restarts. There's no live switch and no server-sent stop (my call): a
  restart disconnects everyone anyway, and the client already stops on its own conditions.
- The server keeps no database state for Genie. Settings live on the client.

The exact sub-opcode values and the reply's error codes are the builder's call. They go in
`src/shared/packets.h` with the other enums, documented the way `e_StoreOpcode` is.

### The window

- **G** opens and closes it; G is unbound in 1.298. Esc and its close button close it too.
- It's modal while open, like Options. Genie keeps hunting while it's open or closed.
- **Start/Stop** button, disabled with a short reason when the server hasn't allowed Genie.
- While Genie runs, a small "Genie" indicator shows on screen. Starting and stopping, with the
  reason (stopped by you, died, bag full, out of potions, out of arrows), go to
  the message window.

### Settings (per character, saved on the Mac next to the client's other settings)

Defaults follow LibreKO's Genie (github.com/ZeusAFK/LibreKO). That's a newer-version
reimplementation, AGPL-licensed, so it's a reference for behaviour only; none of its code is copied.

| Setting | Default |
|---|---|
| Attack skills: up to 8, chosen from the character's learned skills; the first one ready is cast | empty |
| Basic attack on/off | on |
| Buff skills: recast when they wear off | empty |
| Heal skills, used below an HP % | empty, 30% |
| HP potion below % / MP potion below % (on/off each) | on 70% / on 35% |
| Hunting range from the start spot (3–80) | 25 |
| Monster list: only these monsters (added by targeting one and pressing Add, up to 8); empty = any monster | empty |
| Auto-repair equipped gear below a durability % | on, 10% |
| Loot: coins always; item kinds (potions & scrolls / equipment / other); equipment grades (normal, magic, rare, unique); only items that sell for at least N coins | all kinds and grades on, N = 0 |
| Buy potions in the field: on/off; which HP potion and which MP potion; keep at least N of each | off; the potions from the saved merchant's list |
| Buy arrows in the field (bow users): on/off; which arrows; keep at least N | off; the arrows from the saved merchant's list |
| Never spend below N coins when buying | 0 |
| Town (/town) and stop when: bag full / out of HP potions / out of MP potions / out of arrows (bow users) | all on |

### Behaviour

- **Each step**, in this order:
  1. potions and heals;
  2. buffs;
  3. fighting;
  4. looting what has dropped;
  5. going back toward the start spot when there's nothing to fight.
- **Targets**: monsters only. A monster attacking the player comes first, even one that isn't on
  the list. Otherwise it picks the nearest monster on the list (or any monster) within range of the
  start spot.
- **Attacking**: the first attack skill that's learned, off cooldown, affordable and in range;
  otherwise basic attack if it's on, moving into range when needed. A skill the server refuses is
  held back for a few seconds.
- **Looting is remote**: Ebenezer only checks that a drop is in the player's map region
  (User::BundleOpenReq, User::ItemGet), not the distance. Genie loots drops from where it stands,
  stepping closer only when a drop is in another region. The flow is the one the loot window
  uses:
  - WIZ_ITEM_DROP marks the corpse's bundle;
  - WIZ_BUNDLE_OPEN_REQ gets the contents;
  - WIZ_ITEM_GET takes the items that pass the filter. Looting the bundle doesn't open the loot
    window.
- **Repair is remote** too: WIZ_ITEM_REPAIR has no NPC check, and costs coins as at a blacksmith.
- **Buying potions and arrows in the field** (each when it's on). When the chosen potion or
  arrow drops below the amount to keep, Genie buys back up to it with the ordinary buy message
  (WIZ_ITEM_TRADE, buy), from where it stands, and keeps hunting. Ebenezer's buy check
  (User::ItemTrade) needs a real NPC whose selling group matches; it doesn't check distance.
  - **Which merchant:** the client only knows the NPCs it has seen. So Genie buys from the
    merchant whose shop the player opened last that sells the item: potions and arrows may come
    from different merchants, and each is saved on its own. Genie saves each merchant (its NPC ID
    and selling group) in the character's Genie settings, so the player opens each shop once, not
    every session. The window shows which merchant is saved for each, or asks the player to open
    a shop that sells it.
  - **Restarts:** the AI server numbers NPCs in the same order on every start, from the NPC_POS
    table (AIServerApp::LoadNpcPosTable). So the saved merchant stays valid across restarts while
    the world data doesn't change.
  - **When a buy fails:** a refusal, or not being able to buy without going below the coin floor,
    is reported in the message window. "Out of potions" or "out of arrows" then leads to /town as
    when buying is off.
- **/town needs half HP or more**: the client refuses /town below 50% HP or while stunned
  (CMD_TOWN), and LibreKO's Genie keeps that rule too. When a town trigger fires below half HP,
  Genie stops starting fights. It keeps defending itself and healing (potions it still has, heal
  skills, and sitting when nothing is attacking), and sends /town once HP is back over half. (My
  call.)
- **It stops on**:
  - Stop;
  - death;
  - disconnect or zone change;
  - the town triggers above (it sends /town first);
  - the player moving by hand, by keyboard or click (my call: manual control means the player is
    back).

## Left to the builder

- Code structure (a Genie controller ticking from the main game scene, the window class, the
  settings file format), sub-opcode values, and the timing of the decision loop.
- Window layout within the game's style, and how skills are picked: a picker of learned skills with
  icons is expected.
- How potions are found and used. In 1.298 a potion is used by casting its item's effect skill
  through the magic process, as the hotkey bar does.
- What counts as "out of potions/arrows", how buffs' expiry is tracked, and how the monster list
  identifies monsters (by name, as LibreKO does, is fine).
- Tests: the server handshake in Ebenezer.Tests. For the client's decisions (target choice, loot
  filter, town triggers), pure functions where practical; the client has no test harness, so
  scripted app runs cover the rest.
