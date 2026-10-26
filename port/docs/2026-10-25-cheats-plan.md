# Cheats menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Cheats window (J) with multipliers, character, item and world cheats, all applied by
the servers. It works only when the server allows it, through a WIZ_CHEAT handshake.

**Architecture:**
- **Shared model:** `src/shared/CheatSettings.h`, header-only and standard-library-only. It holds:
  - the settings struct, the allowed steps and their byte layout;
  - the scaling math and the item +level arithmetic;
  - the teleport zone list.

  The client and both servers include it.
- **Ebenezer:** owns the settings and their file (`cheats.ini`), the handshake and every
  Ebenezer-side cheat. The cheat code goes in a new `UserCheat.cpp`; the hook points in existing
  code call small methods that tests can call directly.
- **AIServer:** receives a settings snapshot (AG_CHEAT_SETTINGS) and the kill-around request
  (AG_CHEAT_KILL_AROUND). It applies damage, drop and respawn scaling at the points mapped below.
- **Client:**
  - a `CCheats` controller (handshake, the server-confirmed settings, requests, replies, the
    relaxed client checks);
  - pure rules in `CheatRules.h`, tested by a new `Cheats.Tests`;
  - the window, `CUICheats`, a `CUIArtWindow` like `CUIGenie`, with a new `CUIArtEdit` text field.

**Tech Stack:** C++20, Open-KO client (WarFare/N3Base), Ebenezer, AIServer, GoogleTest, CMake plus
MSBuild (vcxproj/slnx).

**Spec:** `port/docs/2026-10-25-cheats-design.md`. Read it first; this plan argues from it.

**Hook maps (read the one for your task):** the mapping notes in
`/private/tmp/claude-501/-Users-emre-Documents-side-projects/b57c6727-c616-469a-8f50-d7455ac9efef/scratchpad/cheats/`
(`ebenezer-notes.md`, `aiserver-notes.md`, `client-notes.md`) list every file:line referred to
below, with the gotchas found there. They are session scratch; the line numbers are as of commit
80e105b3.

## Global Constraints

- **Where the work goes:** only the fork, MostlyEmre/KnightOnline, on branch `macos-port`. No
  upstream PRs.
- **Builds:** the Windows build (MSBuild and the MinGW cross-build) and both DB backends keep
  working. Cheats add no database tables; settings live in `cheats.ini` next to `gameserver.ini`.
- **New files** go in CMake and in the vcxproj plus `.filters`, and a new test project also goes in
  `Tests.slnx`/`All.slnx`.
- **C++ files:** start with a UTF-8 BOM and are formatted with clang-format 19. Changed lines pass
  clang-tidy: mingw-nopch for the client, the scratchpad `srvdb5` database for the servers.
- **Protocol:** approved additions only.
  - `WIZ_CHEAT = 0xF0` with `e_CheatOpcode` and `e_CheatResult` (Task 1);
  - `AG_CHEAT_SETTINGS = 114` and `AG_CHEAT_KILL_AROUND = 115`.

  One fix to an existing message is allowed: Ebenezer's NP update, which the client can't parse
  today, is sent in the format the client already expects (Task 3). No other protocol changes, and
  no asset changes.
- **When the client may send WIZ_CHEAT:** only after `CHEAT_AVAILABLE` with `allowed = 1`.
  Ebenezer disconnects clients that send unknown opcodes.
- **Permission:** Ebenezer reads `[CHEATS] ALLOWED` from `gameserver.ini`. The default is 0, or 1
  when started with `--allow-cheats`, which the app's `ko-servers` passes. Because CIni writes
  missing defaults back, the app's first run writes `ALLOWED=1`.
- **Master switch:** while `bOn` is false, every multiplier acts as 100% and every toggle as off,
  and one-shot requests answer `CHEAT_OFF`.
- **Step values** (exact; any other value is refused):
  - multipliers: 100, 200, 300, 500, 1000, 2500, 5000, 10000 percent;
  - damage taken: also 0 and 50;
  - coins: 0, 100,000, 1M, 10M, 100M, 1B, 2.1B;
  - NP and Knight Cash: 0, 1,000, 10,000, 100,000, 1,000,000;
  - give counts: 1, 5, 10, 50, 100, 500, 1000, 9999 for countable items, and 1, 2, 5, 10 for the
    rest;
  - stat and skill points: +10 per press, capped at 255;
  - level: 1–80;
  - +level: 0–10.
- **Respawn floor:** 5,000 ms, and a respawn is never slower than normal.
- **Kill-around:** monsters only (Genie's rule: type < 10, not 1, not 5), not the player's own
  nation, alive, within 50 m (`NPC_EXP_RANGE`).
- **Knight Cash:** earnings stay as they are. A kill's Knight Cash uses the unmultiplied EXP, and
  cheated levels give no Knight Cash.
- **Scripted app runs:** never take focus or show modal dialogs. Use scratchpad `tools/run_app.sh`
  with a test HOME holding a copy of the player's database, never the real Application Support
  data. For playtests, give the player instructions rather than long scripted UI runs.

## Review Focus

1. **Settings reach AIServer out of order, or it reconnects.**
   - Snapshots carry a sequence number seeded from the clock at Ebenezer start, so it keeps rising
     across Ebenezer restarts. AIServer ignores older snapshots and defaults to ×1 until one
     arrives.
   - The snapshot is re-sent at the end of `SendAllUserInfo`.
   - Test: `CheatSettings.SequenceOnlyMovesForward` (Task 1); the re-send is checked in Task 2.
2. **Damage overflow and rounding.** ×100 on a 400 hit must give 32767, not a negative number;
   ×0.5 on a 1 hit must give 1 (not a miss); a base of 0 or less passes through unchanged (heals,
   errors). Test: `CheatSettings.ScaleDamageClampsAndKeepsSign` (Task 1).
3. **The text field keeps focus after the window closes.** That leaves every game key dead.
   - Closing must release the focus, and typing the toggle letter in the field must not close the
     window.
   - Test: app run, Task 8: type in the item search, press Esc, then I opens the inventory.
4. **Turning cheats off while carrying more than the normal limit.** No item is lost, the max weight
   returns to normal, and the client is told. Test:
   `CheatHooks.WeightLimitRaisedAndReturnsWhenOff` (Task 3).
5. **A server restart with cheats on.** The settings come back from `cheats.ini`, and the client
   gets them at login. Tests: `CheatSettingsFile.RoundTrip`, and
   `CheatHandshake.AnnouncesLoadedSettings` (Task 2).

---

### Task 1: The shared cheat model and the opcodes

**Files:**
- Create: `src/shared/CheatSettings.h`, header-only, in `namespace Cheats`. Standard library only.
- Modify: `src/shared/packets.h`: add `WIZ_CHEAT = 0xF0`, `e_CheatOpcode`, `e_CheatResult` and the
  two AG opcodes, documented like `e_GenieOpcode`.
- Modify: src/shared's CMake source list, `shared.vcxproj` and `.filters`.
- Test: `tests/Server/Ebenezer/CheatSettings_test.cpp`, registered in the tests `CMakeLists.txt`,
  `Ebenezer.Tests.vcxproj` and `.filters`.

**Interfaces (Produces, used by every later task):**
```cpp
// packets.h
WIZ_CHEAT = 0xF0, // private to this fork; never sent unless the server announced it
enum e_CheatOpcode : uint8_t {
    CHEAT_AVAILABLE = 1,      // S: [u8 allowed]; when allowed, a CHEAT_SETTINGS packet follows
    CHEAT_SETTINGS = 2,       // S: [settings]; C: [settings] -> S: CHEAT_SETTINGS to every in-game user, or CHEAT_RESULT
    CHEAT_RESULT = 3,         // S: [u8 request opcode][u8 e_CheatResult]
    CHEAT_SET_LEVEL = 10,     // C: [u8 level]
    CHEAT_ADD_POINTS = 11,    // C: [u8 kind: 0 stat, 1 skill]
    CHEAT_RESET_POINTS = 12,  // C: [u8 kind: 0 stat, 1 skill]
    CHEAT_REFILL = 13,        // C: []
    CHEAT_SET_CURRENCY = 14,  // C: [u8 kind: 0 coins, 1 NP, 2 Knight Cash][u32 value]
    CHEAT_ITEM_VARIANTS = 20, // C: [u32 baseId] -> S: [u32 baseId][125 bytes: bit e = ID baseId+e exists]
    CHEAT_GIVE_ITEM = 21,     // C: [u32 itemId][u16 count]
    CHEAT_SET_PLUS = 22,      // C: [u8 slot][u8 plus] -> S: [u8 slot][u32 newId][i16 durability]
    CHEAT_REPAIR_ALL = 23,    // C: [] -> S: []
    CHEAT_TELEPORT_ZONE = 31, // C: [i16 zone]
    CHEAT_FIND_NPC = 32,      // C: [u8 len][query] -> S: [u8 n] n x {[i16 npcId][i16 zone][u8 len][name]}, n <= 9
    CHEAT_TELEPORT_NPC = 33,  // C: [i16 npcId]
    CHEAT_KILL_AROUND = 34,   // C: []
};
enum e_CheatResult : uint8_t { CHEAT_OK, CHEAT_NOT_ALLOWED, CHEAT_OFF, CHEAT_BAD_VALUE, CHEAT_DEAD,
    CHEAT_BAG_FULL, CHEAT_NO_SUCH_ITEM, CHEAT_NO_PLUS, CHEAT_EMPTY_SLOT, CHEAT_BUSY,
    CHEAT_NOT_FOUND, CHEAT_NO_AI };
AG_CHEAT_SETTINGS = 114,    // [u32 seq][settings]
AG_CHEAT_KILL_AROUND = 115, // [i16 userId]

// CheatSettings.h
namespace Cheats {
inline constexpr std::array<uint16_t, 8> MULTIPLIER_STEPS { 100, 200, 300, 500, 1000, 2500, 5000, 10000 };
inline constexpr std::array<uint16_t, 10> DAMAGE_TAKEN_STEPS { 0, 50, 100, 200, 300, 500, 1000, 2500, 5000, 10000 };
inline constexpr std::array<uint32_t, 7> COIN_STEPS { 0, 100'000, 1'000'000, 10'000'000, 100'000'000, 1'000'000'000, 2'100'000'000 };
inline constexpr std::array<uint32_t, 5> POINT_STEPS { 0, 1'000, 10'000, 100'000, 1'000'000 }; // NP and Knight Cash
inline constexpr std::array<uint16_t, 8> COUNTABLE_STEPS { 1, 5, 10, 50, 100, 500, 1000, 9999 };
inline constexpr std::array<uint16_t, 4> SINGLE_STEPS { 1, 2, 5, 10 };
inline constexpr uint8_t POINTS_PER_PRESS = 10;
inline constexpr int64_t MAX_GAIN = 2'100'000'000;  // == MAX_CURRENCY; Ebenezer static_asserts it
inline constexpr int RESPAWN_FLOOR_MS = 5000;
inline constexpr int8_t HOUR_NORMAL = -1;            // otherwise 0..23
enum class Weather : uint8_t { Normal, Clear, LightRain, HeavyRain, LightSnow, HeavySnow };
struct WeatherOverride { uint8_t byType; int16_t sAmount; };
std::optional<WeatherOverride> WeatherFor(Weather);  // Normal -> nullopt; Clear {1,0}; rain {2,30|100}; snow {3,30|100}
struct Settings {
    bool bOn = false;
    uint16_t sExpPct = 100, sDamageDealtPct = 100, sDamageTakenPct = 100, sCoinPct = 100,
             sDropPct = 100, sLoyaltyPct = 100, sRespawnPct = 100;
    bool bInfiniteMp = false, bNoDurabilityLoss = false, bKeepConsumables = false,
         bNoWeightLimit = false, bFreeShops = false;
    Weather eWeather = Weather::Normal;
    int8_t cHour = HOUR_NORMAL;
    bool operator==(const Settings&) const = default;
};
inline constexpr size_t SETTINGS_SIZE = 18;  // [u8 on][7 x u16 LE][u8 flags bit0..4 in field order][u8 weather][i8 hour]
bool IsValid(const Settings&);
void Write(const Settings&, std::span<uint8_t, SETTINGS_SIZE>);
std::optional<Settings> Read(std::span<const uint8_t>);   // nullopt: too short or !IsValid
uint16_t Effective(const Settings&, uint16_t Settings::*pct);   // !bOn -> 100
bool EffectiveFlag(const Settings&, bool Settings::*flag);       // !bOn -> false
int64_t ScaleGain(int64_t base, uint16_t pct);   // base <= 0 unchanged; else base*pct/100, at most MAX_GAIN
int ScaleDamage(int base, uint16_t pct);          // base <= 0 unchanged; pct 0 -> 0; else clamp(base*pct/100, 1, 32767)
int UnscaleDamage(int dealt, uint16_t pct);       // weapon wear: dealt*100/pct; pct 0 or 100 -> dealt
int ScaleDropChance(int per10000, uint16_t pct);  // 0 stays 0; else min(per*pct/100, 10000)
int ScaleRegenTime(int ms, uint16_t speedPct);    // pct <= 100 -> ms; else max(ms*100/pct, min(ms, RESPAWN_FLOOR_MS))
int PlusOf(int ext);                              // ext = id % 1000: 0 -> 0; else (ext-1)%10+1
int ExtWithPlus(int ext, int plus);               // same line: plus 0 -> 0 only if ext <= 10, else -1;
                                                  // plus 1..10 -> ((ext==0 ? 0 : (ext-1)/10))*10 + plus; out of range -> -1
inline constexpr size_t AG_SETTINGS_SIZE = 4 + SETTINGS_SIZE;
bool IsNewer(uint32_t seq, uint32_t lastSeq);     // seq > lastSeq
struct Zone { int16_t sId; const char* szName; };
inline constexpr std::array<Zone, 9> ZONES {{ {21, "Moradon"}, {1, "Luferson Castle"},
    {2, "El Morad Castle"}, {11, "Karus Eslant"}, {12, "El Morad Eslant"}, {201, "Colony Zone"},
    {30, "Delos"}, {32, "Desperation Abyss"}, {33, "Hell Abyss"} }};
bool IsTeleportZone(int zone);
template <class T, size_t N> bool IsStep(const std::array<T, N>&, T value);
}
```

- [ ] **Step 1: Write the failing tests** in `CheatSettings_test.cpp`:
  - `CheatSettings.RoundTripsEveryField`: a settings with every field non-default → `Write` →
    `Read` gives equal settings, and the bytes start `[1][..]` with the u16 values little-endian.
  - `CheatSettings.ReadRefusesBadValues`: 17 bytes → nullopt; sExpPct 150 → nullopt; sExpPct 0 →
    nullopt; sDamageTakenPct 0 and 50 → accepted; hour 24 → nullopt; weather 6 → nullopt.
  - `CheatSettings.OffMeansNormal`: with `bOn = false`, `Effective` is 100 for a 10000 setting and
    `EffectiveFlag` is false for a true flag; with `bOn = true` both pass through.
  - `CheatSettings.ScaleDamageClampsAndKeepsSign`:
    - (400, 10000) → 32767; (1, 50) → 1; (3, 50) → 1; (10, 0) → 0;
    - (0, 10000) → 0; (−5, 10000) → −5; (100, 100) → 100.
  - `CheatSettings.ScaleGainCaps`: (32767, 10000) → 3,276,700; (2'000'000'000, 200) → 2'100'000'000;
    (−10, 500) → −10.
  - `CheatSettings.DropChanceCapsAtCertain`: (0, 10000) → 0; (50, 10000) → 5000; (5000, 300) →
    10000.
  - `CheatSettings.RegenHasAFloorAndNeverSlows`: (30000, 100) → 30000; (30000, 200) → 15000;
    (30000, 10000) → 5000; (4000, 10000) → 4000; (60000, 50) → 60000.
  - `CheatSettings.UnscaleDamage`: (1000, 1000) → 100; (5, 0) → 5; (5, 100) → 5.
  - `CheatSettings.PlusArithmetic`:
    - `PlusOf`: 0 → 0, 5 → 5, 10 → 10, 15 → 5, 20 → 10.
    - `ExtWithPlus`: (5, 6) → 6; (5, 0) → 0; (15, 6) → 16; (15, 0) → −1; (0, 10) → 10;
      (5, 11) → −1; (995, 6) → 996; (1000, 1) → −1.
  - `CheatSettings.SequenceOnlyMovesForward`: `IsNewer(5, 4)` true; `IsNewer(4, 4)` and
    `IsNewer(3, 4)` false.
  - `CheatSettings.TeleportZones`: `IsTeleportZone(21)` true; 51, 91, 101 and 0 false.
- [ ] **Step 2: Run them and see them fail.**
  - Build: `cmake --build build/mac-sqlite --target Ebenezer.Tests -j8`
  - Expected: a compile failure, since `shared/CheatSettings.h` doesn't exist.
- [ ] **Step 3: Implement** `CheatSettings.h` and the packets.h additions. Pick 0xF0 only after
  checking nothing in packets.h uses it.
- [ ] **Step 4: Run.**
  - Command: `build/mac-sqlite/bin/Debug/Ebenezer.Tests --gtest_filter='CheatSettings*'`
  - Expected: all pass. Then the whole suite passes too (`cd build/mac-sqlite && ctest -j8`).
- [ ] **Step 5: Commit:** `Cheats: the shared settings model and the WIZ_CHEAT / AG opcodes`.

### Task 2: Ebenezer: permission, settings, handshake and the AIServer snapshot

**Files:**
- Modify: `src/Server/Ebenezer/EbenezerApp.h` / `.cpp`:
  - the permission flag and the `--allow-cheats` option;
  - the settings, their mutex and `cheats.ini`;
  - the AG snapshot, appended to `SendAllUserInfo` (EbenezerApp.cpp:2073).
- Create: `src/Server/Ebenezer/UserCheat.cpp` (CUser cheat methods), in CMake and
  `Ebenezer.Core.vcxproj` + `.filters`.
- Modify: `src/Server/Ebenezer/User.h` / `User.cpp`:
  - the `case WIZ_CHEAT: Cheat(pData + index, len - index);` in `Parsing`;
  - `SendCheatAvailable()` after `SendGenieAvailable()` in `GameStart` (:14706).
- Modify: `port/app/ko-servers/main.cpp`: add `--allow-cheats` to Ebenezer's arguments.
- Test: `tests/Server/Ebenezer/CheatHandshake_test.cpp`, registered as in Task 1.

**Interfaces:**
- Consumes: Task 1 (`Cheats::Settings`, `Read`/`Write`, `IsNewer`, the opcodes).
- Produces, in `EbenezerApp`:
  ```cpp
  bool m_bCheatsAllowed = false;
  bool _allowCheatsByDefault = false;                    // --allow-cheats
  std::filesystem::path _cheatSettingsPath = "cheats.ini";
  Cheats::Settings CheatSettings() const;                // copy under _cheatMutex
  uint16_t CheatPct(uint16_t Cheats::Settings::*) const; // Effective()
  bool CheatFlag(bool Cheats::Settings::*) const;        // EffectiveFlag()
  bool SetCheatSettings(const Cheats::Settings&);        // false if !IsValid
  void LoadCheatSettings();                              // invalid or missing file -> defaults
  int BuildCheatSettingsAI(char* buf) const;             // AG_CHEAT_SETTINGS packet, returns length
  uint32_t _cheatSeq;                                    // starts at (uint32_t) time(nullptr)
  ```
  `SetCheatSettings` does all of this in order:
  1. stores the settings;
  2. saves them (with its own `CIni`, keys `[CHEATS] ON, EXP, DAMAGE_DEALT, DAMAGE_TAKEN, COINS,
     DROPS, NP, RESPAWN, INFINITE_MP, NO_DURABILITY_LOSS, KEEP_CONSUMABLES, NO_WEIGHT_LIMIT,
     FREE_SHOPS, WEATHER, HOUR`);
  3. bumps `_cheatSeq` and sends `BuildCheatSettingsAI` with `Send_AIServer`;
  4. sends `[WIZ_CHEAT][CHEAT_SETTINGS][settings]` to every user in `CONNECTION_STATE_GAMESTART`;
  5. calls `OnCheatSettingsChanged(old, new)`, which Tasks 3 and 5 fill in (empty here).
- Produces, in `CUser` (UserCheat.cpp):
  - `void Cheat(char* pBuf, int len);`
  - `void SendCheatAvailable();`
  - `void SendCheatResult(uint8_t opcode, uint8_t result);`
  - `Cheat` ignores everything unless in `CONNECTION_STATE_GAMESTART`.
  - When `!m_bCheatsAllowed`, every request is answered with `CHEAT_RESULT … CHEAT_NOT_ALLOWED`.
  - An unknown sub-opcode or a short payload gets `CHEAT_BAD_VALUE`.
  - `CHEAT_SETTINGS` goes to `SetCheatSettings`.
  - One-shot sub-opcodes are dispatched to the Task 4 and 5 methods (stubs answering
    `CHEAT_BAD_VALUE` until then).
- `LoadConfig` reads `m_bCheatsAllowed = iniFile.GetBool("CHEATS", "ALLOWED",
  _allowCheatsByDefault)` beside `GENIE`, then calls `LoadCheatSettings()`.

- [ ] **Step 1: Write the failing tests** in `CheatHandshake_test.cpp`, using the Genie_test
  fixture. Point `_cheatSettingsPath` at a temp file removed in TearDown.
  - `CheatHandshake.AnnouncesAvailability`:
    - Allowed: `SendCheatAvailable()` sends `[WIZ_CHEAT][CHEAT_AVAILABLE][1]`, then
      `[WIZ_CHEAT][CHEAT_SETTINGS]` + 18 bytes.
    - Not allowed: only `[..][0]`.
  - `CheatHandshake.AnnouncesLoadedSettings`: write a `cheats.ini` with `ON=1, EXP=1000`, call
    `LoadCheatSettings()`; the settings packet carries both.
  - `CheatHandshake.SetsAndEchoesSettings`: a valid `CHEAT_SETTINGS` request → `CheatSettings()`
    equals it, and the user receives the echo.
  - `CheatHandshake.RefusesWhenNotAllowed`: any request → `[CHEAT_RESULT][request][CHEAT_NOT_ALLOWED]`,
    and the settings are unchanged.
  - `CheatHandshake.RefusesBadSettings`: sExpPct 150 → `CHEAT_BAD_VALUE`, unchanged; a 5-byte
    payload → `CHEAT_BAD_VALUE`.
  - `CheatHandshake.IgnoredOutsideTheGame`: not in GAMESTART → nothing sent.
  - `CheatSettingsFile.RoundTrip`: `SetCheatSettings(x)`, then a fresh app with the same path and
    `LoadCheatSettings()` gives x. A file with `EXP=150` loads defaults.
  - `CheatHandshake.AiSnapshot`:
    - `BuildCheatSettingsAI` gives `[AG_CHEAT_SETTINGS][u32 seq][18 bytes]`.
    - The seq rises after `SetCheatSettings`.
    - `SendAllUserInfo` calls the builder: check through a counter or a seam you add. A
      `Send_AIServer` call can't be captured.
- [ ] **Step 2: Run them and see them fail** (compile failure: `Cheat`, `SendCheatAvailable`, …
  don't exist). Build as in Task 1; filter `'Cheat*'`.
- [ ] **Step 3: Implement.** Add `static_assert(Cheats::MAX_GAIN == MAX_CURRENCY)` in UserCheat.cpp.
- [ ] **Step 4: Run.** Expected: `Cheat*` passes, and so does the whole suite.
- [ ] **Step 5: Commit:** `Ebenezer: WIZ_CHEAT handshake, cheat settings saved in cheats.ini and sent to AIServer; the app allows cheats`.

### Task 3: Ebenezer multipliers and toggles

**Files:**
- Modify: `UserCheat.cpp`, `User.h`, `User.cpp`, `AISocket.cpp`, `MagicProcess.cpp`,
  `EbenezerApp.cpp` (`OnCheatSettingsChanged`).
- Test: `tests/Server/Ebenezer/CheatHooks_test.cpp`. Add TestApp helpers for a level-up table entry
  and a class coefficient entry if the tests need them.

**Interfaces:**
- Consumes: Task 2 (`CheatPct`, `CheatFlag`, `OnCheatSettingsChanged`).
- Produces, in `CUser`:
  - `void ExpGain(int base);`
    - Scales by EXP %, calls `ExpChange`, then keeps levelling while `m_iExp >= m_iMaxExp` and
      the level is below `MAX_LEVEL`.
    - Check `ExpChange(0)`'s behaviour for this; if it doesn't level, write a loop that does,
      and ledger it.
  - `void LoyaltyGain(int base);`
    - Scales by NP %, `CurrencyChange(m_iLoyalty, …)`, then sends
      `[WIZ_LOYALTY_CHANGE][1][i32 loyalty][i32 monthly]`.
    - Extract `SendLoyaltyChange()` from `ChangeLoyalty` (User.cpp:5934-5939) and use it in both.
  - `int CoinPickupAmount(int count) const;` (ScaleGain by coin %).
  - `void SendStatsUpdate();`, extracted from the `WIZ_ITEM_MOVE [1]` block at User.cpp:4795; the
    old block calls it.
- **Wiring:**
  - **EXP and NP from kills:** `RecvUserExp` (AISocket.cpp:1132-1141) calls
    `pUser->LoyaltyGain(sLoyalty)` and `pUser->ExpGain(sExp)`. Knight Cash keeps using the raw
    `sExp`. The broken 5-byte NP message at :1141 goes; `LoyaltyGain` sends the good one.
  - **Quest EXP and NP:** `EXEC_EXP_CHANGE` (:12295) uses `ExpGain` when > 0;
    `EXEC_CHANGE_LOYALTY` (:12313) scales when > 0.
  - **Coins:** `ItemGet`'s gold branch (:5641) uses `CoinPickupAmount(count)` for the solo gain
    (:5647) and before the party split (:5682). The packets keep the original `count` field.
  - **Monster spell damage** (`MagicProcess.cpp`, when `bFlag` and the damage < 0): scale with
    `-ScaleDamage(-d, CheatPct(&Settings::sDamageTakenPct))` at :1384 (instant), :1484 (first DoT
    hit) and :1549 (DoT per tick).
  - **Weapon wear:** `RecvNpcAttack` type 1 (AISocket.cpp:603) passes
    `UnscaleDamage(damage, dealt %)` to `ItemWoreOut`.
  - **Infinite MP:** skip the check and the deduction at MagicProcess.cpp:298/304, :858/:979 and
    :1822. HP-cost skills stay as they are.
  - **Keep consumables:** at MagicProcess.cpp:308 (arrows) and :924 (potions and scrolls), require
    `CheckExistItem(id, n)` and skip `ItemCountChange`.
  - **No durability loss:** an early return in `ItemWoreOut` (:7711).
  - **Free shops:** `ItemTrade`'s `buyPrice = 0` (:5257).
  - **No weight limit:** `m_iMaxWeight = SHRT_MAX` in `SetUserAbility` (:4231) and
    `SetDetailData` (:2913).
  - **`OnCheatSettingsChanged`:** if the effective weight flag changed, it calls `SetUserAbility()`
    and `SendStatsUpdate()` for every in-game user.

- [ ] **Step 1: Write the failing tests** in `CheatHooks_test.cpp` (cheats on unless stated):
  - `CheatHooks.ExpMultiplierLevelsUpSeveralTimes`: level 1, EXP ×100, a level table where 5
    levels' worth fits in 100×base → several levels gained, EXP within the new level.
  - `CheatHooks.ExpOffIsNormal`: master off → `ExpGain(50)` adds exactly 50.
  - `CheatHooks.LoyaltyGainSendsTheClientFormat`: NP ×10, `LoyaltyGain(3)` → +30, and the packet
    is `[WIZ_LOYALTY_CHANGE][1][i32][i32]`.
  - `CheatHooks.CoinPickupScales`: ×25 → `CoinPickupAmount(32000)` = 800000; off → 32000.
  - `CheatHooks.FreeShopsChargeNothing`: an NPC with a matching selling group, item price 1000,
    player gold 0, free shops → the buy succeeds with gold still 0; off → refused.
  - `CheatHooks.NoDurabilityLoss`: equipped weapon at 5000 durability; `ItemWoreOut(attack, 900)` →
    still 5000; off → lower.
  - `CheatHooks.WeightLimitRaisedAndReturnsWhenOff`:
    - On: `SetUserAbility()` gives `m_iMaxWeight == SHRT_MAX`.
    - Then off through `SetCheatSettings`: it is back to `(Str+ItemStr)*50`, the bag is unchanged,
      and a `WIZ_ITEM_MOVE` stats packet was sent.
  - `CheatHooks.InfiniteMpAndKeptArrows`:
    - Through `CMagicProcess::IsAvailable` with a seeded magic row; add a TestApp helper if it
      takes under ~40 lines.
    - Infinite MP → a cast with `m_sMp = 0` passes and MP stays 0.
    - Keep consumables → an arrow skill leaves the arrow count unchanged but still fails with no
      arrows.
    - If seeding magic is impractical, test the guard helpers instead and ledger a ruling.
- [ ] **Step 2: Run them and see them fail.** Filter `'CheatHooks*'`; expected: compile failures,
  then assertion failures.
- [ ] **Step 3: Implement** the wiring above.
- [ ] **Step 4: Run.** Expected: `CheatHooks*` passes, the whole suite passes, and the existing
  ItemUpgrade/Genie/ShoppingMall tests are unchanged.
- [ ] **Step 5: Commit:** `Ebenezer: cheat multipliers (EXP, NP, coins, monster spell damage) and toggles (MP, consumables, durability, shops, weight)`.

### Task 4: Ebenezer character and item cheats

**Files:**
- Modify: `UserCheat.cpp`, `User.h`, `User.cpp` (the extractions below).
- Test: `tests/Server/Ebenezer/CheatCharacter_test.cpp` and `CheatItems_test.cpp`.

**Interfaces:**
- Consumes: Tasks 2 and 3.
- **Extracted from existing code** (the old paths call them, so behaviour stays the same):
  - `void ApplyStatPointReset();`, the core of `StatPointResetRequest` after its checks. It sends
    the `CLASS_RESET_STAT_REQ` success packet with the current gold.
  - `void ApplySkillPointReset();`, from `SkillPointResetRequest`, with skill points clamped to
    ≥ 0 below level 10.
  - `void SendLevelChangeInfo();`, the `WIZ_LEVEL_CHANGE` send from `LevelChange`.
- **New methods.** Each returns an `e_CheatResult`; `Cheat()` sends it as `CHEAT_RESULT`. Each
  refuses with `CHEAT_OFF` when the master switch is off, and `CHEAT_DEAD` when dead, where that
  applies.
  - `uint8_t CheatSetLevel(int level);`
    - Refuses outside 1–80 (`CHEAT_BAD_VALUE`).
    - **Up:**
      - free stat points += max(0, (300 + 3(L−1)) − (stats + points));
      - free skill points += max(0, 2·max(0, L−9) − sum(skill[0..8]));
      - both clamped to 255.
    - **Down:** forced stat and skill resets at the new level.
    - Then `LevelChange(L, false)` (no Knight Cash) and `m_iExp = 0`.
    - It sends `AG_USER_SET_HP` through `HpChange` type 0. A drop in level must still send it.
  - `uint8_t CheatAddPoints(uint8_t kind);` adds +10 stat or skill points (max 255), then calls
    `SendLevelChangeInfo()`.
  - `uint8_t CheatResetPoints(uint8_t kind);` calls `ApplyStatPointReset` or `ApplySkillPointReset`:
    free, even with gear on.
  - `uint8_t CheatRefill();` calls `HpChange(m_iMaxHp - m_sHp)` and `MSpChange(m_iMaxMp - m_sMp)`.
  - `uint8_t CheatSetCurrency(uint8_t kind, uint32_t value);`
    - The value must be a `COIN_STEPS` or `POINT_STEPS` entry.
    - Coins: `GoldGain` or `GoldLose` for the difference.
    - NP: set, then `SendLoyaltyChange()`.
    - Knight Cash: `KnightCashAddToAgent(sock, account, -MAX_CURRENCY, false)`, then
      `(…, value, true)` when the value is above 0.
  - `void SendCheatItemVariants(int baseId);`
    - `baseId % 1000 == 0`, otherwise `CHEAT_BAD_VALUE`.
    - Bit e is set when `m_ItemTableMap.GetData(baseId + e)` exists.
  - `uint8_t CheatGiveItem(int itemId, int count);`
    - The ID must exist; refuse 900000000 and IDs ≥ `ITEM_NO_TRADE`.
    - Countable: one `GiveItem(id, count)`, with count in `COUNTABLE_STEPS`.
    - Otherwise: count in `SINGLE_STEPS`, one `GiveItem(id, 1)` per item until the bag is full,
      each with a serial from `GenerateItemSerial()`.
    - `CHEAT_BAG_FULL` if nothing was given.
  - `uint8_t CheatSetPlus(uint8_t slot, int plus);`
    - Slot is an equipped slot 0–13, otherwise `CHEAT_BAD_VALUE`; an empty slot gives
      `CHEAT_EMPTY_SLOT`.
    - New ID = `id - ext + ExtWithPlus(ext, plus)`. It's valid only if it is ≥ 0, exists, has the
      same Kind and Slot, and both items are on a +level line (ext 0, or ITEM `ItemType == 5`).
      Otherwise `CHEAT_NO_PLUS`.
    - Durability keeps its share of the maximum (int64, clamped to the new int16 maximum).
    - Then `SetSlotItemValue()`, `SetUserAbility()`, `SendStatsUpdate()`, `WIZ_DURATION`,
      `UserLookChange`, `Send2AI_UserUpdateInfo()`, and the `CHEAT_SET_PLUS` reply.
  - `uint8_t CheatRepairAll();`
    - Every item's durability goes to its table maximum.
    - Equipped slots: `WIZ_DURATION`. Bag items: `WIZ_ITEM_COUNT_CHANGE` district 1, existing.
    - If something was broken: `SetSlotItemValue()`, `SetUserAbility()`, `SendStatsUpdate()`,
      `Send2AI_UserUpdateInfo()`.
    - Then the `CHEAT_REPAIR_ALL` reply.

- [ ] **Step 1: Write the failing tests.**
  - `CheatCharacter.SetLevelUpGrantsPoints`: level 10 with points spent → level 30;
    points += 60; skill points += 40; EXP 0; a `WIZ_LEVEL_CHANGE` was sent; Knight Cash unchanged.
  - `CheatCharacter.SetLevelDownResets`: level 60 → 20: stats at the race base,
    points == 19·3 + 10, skill[0] == 22, skill[1..8] == 0.
  - `CheatCharacter.SetLevelRefusesOutOfRange`: 0 and 81 → `CHEAT_BAD_VALUE`; off → `CHEAT_OFF`.
  - `CheatCharacter.AddPointsCapsAt255`: points 250 → 255, then another press keeps 255.
  - `CheatCharacter.ResetStatsWithGearOn`: gear equipped → reset succeeds (the normal request
    still refuses).
  - `CheatCharacter.Refill`: HP 1 of 500 → 500; MP 0 of 300 → 300; dead → `CHEAT_DEAD`.
  - `CheatCharacter.SetCurrency`:
    - Coins 5,000 → 1,000,000 sends `WIZ_GOLD_CHANGE` with type gain and delta 995,000; 777 →
      `CHEAT_BAD_VALUE`.
    - NP to 10,000 sends `[WIZ_LOYALTY_CHANGE][1][10000][..]`.
  - `CheatItems.VariantsBitmap`: items 156210000, 156210005 and 156210230 → bits 0, 5 and 230
    set, others clear.
  - `CheatItems.GiveCountable`: a countable scroll ×50 → one stack of 50; ×777 → `CHEAT_BAD_VALUE`.
  - `CheatItems.GiveSingles`: a bow ×5 with 3 free slots → 3 given; a full bag →
    `CHEAT_BAG_FULL`; a missing ID → `CHEAT_NO_SUCH_ITEM`.
  - `CheatItems.SetPlus`:
    - Equipped 156210005 at full durability → plus 6 gives 156210006 at full durability, and the
      reply is `[22][slot][156210006][dur]`.
    - Plus 6 on ext 15 gives ext 16.
    - A missing target → `CHEAT_NO_PLUS`; an empty slot → `CHEAT_EMPTY_SLOT`.
  - `CheatItems.RepairAll`: a broken equipped item and a worn bag item both reach the maximum;
    `WIZ_DURATION` and `WIZ_ITEM_COUNT_CHANGE` were sent.
- [ ] **Step 2: Run them and see them fail.** Filter `'CheatCharacter*:CheatItems*'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expected: all pass, and the whole suite passes, including the existing
  class-change and item tests.
- [ ] **Step 5: Commit:** `Ebenezer: character and item cheats: level, points, resets, refill, currencies, give, +level, repair`.

### Task 5: Ebenezer world cheats and the kill-around request

**Files:**
- Modify: `UserCheat.cpp`, `User.h`, `User.cpp` (split `ZoneChange`), `EbenezerApp.h` / `.cpp`
  (weather and time).
- Test: `tests/Server/Ebenezer/CheatWorld_test.cpp`.

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces:
  - `void ZoneChangeUnchecked(int zone, float x, float z);`: everything in `ZoneChange` from
    `m_bZoneChangeFlag = true` (:2710) on. `ZoneChange` calls it after its rules.
  - `uint8_t CheatTeleportZone(int zone);`
    - The zone must be `IsTeleportZone`, otherwise `CHEAT_BAD_VALUE`.
    - Refuses while `m_bWarp`, dead or trading (`CHEAT_BUSY` or `CHEAT_DEAD`).
    - Goes to the zone's start position for the player's nation (`(0,0)` →
      `GetStartPosition`), or `Warp` in the same zone.
  - `void SendCheatNpcSearch(std::string_view query);`
    - Case-insensitive substring over `m_NpcMap` names.
    - NPCs with type ≥ 11, not 50–68 (gates and objects) and not 99, in a teleport zone, alive.
    - Up to 9, sorted by name then ID.
  - `uint8_t CheatTeleportNpc(int npcId);`: the same NPC rules, then `ZoneChangeUnchecked` to its
    position + 2 m on x (the server's `IsValidPosition` check applies on a same-zone warp), else
    `CHEAT_NOT_FOUND`.
  - `uint8_t CheatKillAround();`
    - Alive, otherwise `CHEAT_DEAD`.
    - Sends `[AG_CHEAT_KILL_AROUND][i16 socketId]`, returning `CHEAT_NO_AI` when no AI socket is
      connected.
    - The packet is built by `int BuildKillAroundAI(char* buf) const` so it can be tested.
  - **Weather and time** (`EbenezerApp`):
    - `std::optional<Cheats::WeatherOverride> CheatWeather() const` and
      `int CheatHour() const` (−1 = normal), both effective, so off means normal.
    - `UpdateWeather` sends the override instead of rolling, leaving `m_nWeather`/`m_nAmount`
      alone.
    - `UpdateGameTime` re-sends `WIZ_TIME` with the override hour to every in-game user each tick
      while an hour override is on. `AG_TIME_WEATHER` to AIServer stays on the real clock.
    - `SendTimeStatus` uses the override.
    - `OnCheatSettingsChanged` broadcasts `WIZ_WEATHER` and `WIZ_TIME` (override or real)
      whenever the effective weather or hour changes.

- [ ] **Step 1: Write the failing tests.**
  - `CheatWorld.TeleportZoneSkipsRules`: a Karus player, level 10, teleports to zone 2 (El Morad,
    refused by `ZoneChange`) → succeeds; zone 51 → `CHEAT_BAD_VALUE`; while `m_bWarp` →
    `CHEAT_BUSY`.
  - `CheatWorld.NpcSearch`: NPCs "Charon" (type 21, zone 21), "Charon Guard" (type 11) and
    "Charon Rat" (type 0) → the query "charon" returns the first two, sorted, with their zones.
  - `CheatWorld.TeleportNpc`: to Charon → the player lands within 3 m of it; an unknown ID →
    `CHEAT_NOT_FOUND`.
  - `CheatWorld.KillAroundPacket`: `BuildKillAroundAI` = `[115][i16 id]`; no AI → `CHEAT_NO_AI`.
  - `CheatWorld.WeatherOverride`: Heavy Snow on → `CheatWeather()` = {3, 100}; master off →
    nullopt; the settings change broadcast `WIZ_WEATHER [3][100]`.
  - `CheatWorld.HourOverride`: hour 6 → `CheatHour()` 6 and `SendTimeStatus` carries 6; back to
    Normal → the real hour is broadcast.
- [ ] **Step 2: Run them and see them fail.** Filter `'CheatWorld*'`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Expected: all pass, and the whole suite passes, including the existing zone
  tests if any.
- [ ] **Step 5: Commit:** `Ebenezer: world cheats: teleport to a zone or an NPC, weather and time, kill around`.

### Task 6: AIServer: settings, damage, drops, respawn and kill-around

**Files:**
- Modify: `src/Server/AIServer/AIServerApp.h` / `.cpp`, `GameSocket.h` / `.cpp`, `User.cpp`,
  `MagicProcess.cpp`, `Npc.h` / `.cpp`, `NpcThread.cpp`.

**Interfaces:**
- Consumes: Task 1, and the AG packets from Tasks 2 and 5.
- Produces, in `AIServerApp`:
  - `std::atomic<uint16_t>` effective percentages for damage dealt, damage taken, drops and
    respawn (all 100 at start);
  - `std::atomic<uint32_t> _cheatSeq{0}`;
  - `int ScaleDealt(int) const`, `int ScaleTaken(int) const`, `int CheatDropPct() const` and
    `int CheatRespawnPct() const`.
- **Parsing:**
  - `CGameSocket::Parsing` passes `length - index` to `RecvCheatSettings(char*, int)`.
  - That handler refuses short packets and applies the snapshot only when `IsNewer(seq,
    _cheatSeq)`.
  - It logs `cheat settings seq={} on={} dealt={} taken={} drops={} respawn={}` at info.
- **Damage dealt:** `ScaleDealt` just before each user call site of `CNpc::SetDamage`, so the
  value sent to Ebenezer equals what was subtracted:
  - `User.cpp:168`, applied right after `GetDamage` (:158), before the GM override;
  - `MagicProcess.cpp:202, 247, 341, 379, 519, 905, 998`;
  - the timed tick at `Npc.cpp:6838`.
- **Damage taken:** `ScaleTaken` after the test-mode override in `CNpc::Attack` (:3397) and
  `TracingAttack` (:3650), user branch.
- **Drops:** after `if (iPer == 0) continue;` (Npc.cpp:6011):
  `iPer = Cheats::ScaleDropChance(iPer, CheatDropPct())`.
- **Respawn:** `m_Delay = Cheats::ScaleRegenTime(m_sRegenTime, CheatRespawnPct())` (Npc.cpp:1972).
- **Kill around:**
  - `RecvCheatKillAround` finds the user (alive, HP > 0).
  - It copies the NPC IDs of the 3×3 regions under `g_region_mutex`, clamping with
    `GetXRegionMax`/`GetZRegionMax`, then releases it.
  - It keeps monsters only (type < 10, not 1, not 5): not the player's nation, alive, not a
    special object, within 50 m by `IsUserInSight`'s formula.
  - For each, it stores `pNpc->m_iCheatKillBy` (`std::atomic<int>`, −1 = none).
  - At the top of that NPC's own tick (before NpcThread.cpp:52), a claimed request runs:
    `SetDamage(0, hp, …)` (unscaled), `SendExpToUserList`, `SendDead`, then
    `SendAttackSuccess(… MAGIC_ATTACK_TARGET_DEAD, min(hp, 32767), …, MAGIC_ATTACK)` through the
    user.
- AIServer has no test project. The math is covered by Task 1. This task is verified by an app run
  plus the review.

- [ ] **Step 1: Build first.** Run `cmake --build build/mac-sqlite --target AIServer -j8` and get
  it compiling with the hooks in.
- [ ] **Step 2: Run the app.** Use a copy of the player's database in a test HOME with the
  archieApple character, and log in with a script.
  1. Send a settings packet through the window (after Task 8) or a temporary input-script verb:
     ON, damage dealt ×100, drops ×100, respawn ×10.
  2. Check AIServer.log for the settings line.
  3. One basic attack kills a Moradon monster.
  4. A drop appears.
  5. Kill-around kills the monsters within 50 m and EXP rises.

  Note in the ledger which of these you could only check after Task 8.
- [ ] **Step 3: Check the whole suite and lint.**
  - `cd build/mac-sqlite && ctest -j8` passes.
  - clang-tidy is clean on the changed lines (srvdb5).
- [ ] **Step 4: Commit:** `AIServer: cheat settings snapshot; damage, drop and respawn scaling; kill around`.

### Task 7: Client: CCheats controller, rules and relaxed checks

**Files:**
- Create: `src/Client/WarFare/Cheats.h` / `.cpp` (controller) and `src/Client/WarFare/CheatRules.h`
  (pure). List them in the WarFare CMake source list and `WarFare.Core.vcxproj` + `.filters`.
- Create: `tests/Client/Cheats/` (`CMakeLists.txt`, `CheatRules_test.cpp`, `Cheats.Tests.vcxproj`,
  `.filters`), copied from `tests/Client/Genie` with a new GUID. Register it in the root
  `CMakeLists.txt` beside Genie, and in `Tests.slnx`/`All.slnx`.
- Modify:
  - `GameProcMain.h` / `.cpp`: the `case WIZ_CHEAT`; `m_pCheats`, created, deleted and
    `OnLeaveGame` in `Release`; the indicator render after Genie's.
  - `GameProcedure.cpp`: `MsgRecv_ServerChange` resets availability.
  - `MagicSkillMng.cpp`: `HasRequiredMana`.
  - `UITransactionDlg.cpp`: the three coin checks, done in int64.

**Interfaces:**
```cpp
// CheatRules.h (namespace Cheats; standard library + shared/CheatSettings.h only)
struct ItemName { uint32_t dwID; std::string szName; };
std::vector<uint32_t> SearchItems(std::span<const ItemName>, std::string_view query, size_t maxResults = 200);
    // ASCII case-insensitive substring; empty names skipped; blank query -> none; sorted by name, then ID
std::string VariantLabel(std::string_view baseName, int ext, std::string_view header, int magicOrRare);
    // magicOrRare 4 (unique) -> header; otherwise (header non-empty and != "Upgrade" ? header + " " : "")
    // + baseName + (PlusOf(ext) > 0 ? " (+N)" : "")
std::string MultiplierLabel(uint16_t pct);         // 0 -> "x0 (no damage)", 50 -> "x0.5", 100 -> "x1 (normal)", 2500 -> "x25"
std::string ResultMessage(uint8_t opcode, uint8_t result);  // message-window text for every e_CheatResult

// Cheats.h
class CCheats {
public:
    explicit CCheats(CGameProcMain* pMain);
    void Receive(Packet& pkt);                     // every server sub-opcode
    bool IsAvailable() const;
    const Cheats::Settings& Confirmed() const;     // last CHEAT_SETTINGS from the server
    bool IsOn(bool Cheats::Settings::*flag) const; // EffectiveFlag(Confirmed(), flag)
    void OnLeaveGame();                            // availability and results cleared
    void SendSettings(const Cheats::Settings&);    // plus one Send... per request sub-opcode, each
    // refusing unless available: SendSetLevel, SendAddPoints, SendResetPoints, SendRefill,
    // SendSetCurrency, SendItemVariants, SendGiveItem, SendSetPlus, SendRepairAll,
    // SendTeleportZone, SendFindNpc, SendTeleportNpc, SendKillAround
    const std::bitset<1000>* Variants(uint32_t baseId) const;  // last reply for that item, or null
    struct NpcHit { int16_t sID, sZone; std::string szName; };
    const std::vector<NpcHit>& NpcHits() const;
    void RenderIndicator();                        // "Cheats" bar while Confirmed().bOn
};
```
- **Replies:**
  - `CHEAT_RESULT` → `MsgOutput(ResultMessage(...))`: green for OK, red otherwise.
  - `CHEAT_SETTINGS`:
    - stores the settings;
    - calls `UpdateDisableCheck()` on the hotkey bar and the skill tree;
    - reports "Cheats on" or "Cheats off" when that changed.
  - `CHEAT_SET_PLUS`: swaps the equipped item in place, as the anvil does (UIItemUpgrade.cpp:957-982),
    then `m_pUIInventory->ItemAdd(...)` for the model and durability.
  - `CHEAT_REPAIR_ALL`: sets every item's durability to its table maximum and clears
    `UISTYLE_DURABILITY_EXHAUST`, as Genie's repair does.
- **Relaxations, server-confirmed only:**
  - `HasRequiredMana` returns true when `bInfiniteMp` is on.
  - The `UITransactionDlg` coin checks pass when `bFreeShops` is on.
  - The weight checks need nothing: the server sends the raised maximum.
  - Genie is unchanged.
- **Indicator:** a 110×29 `BAR_BLUE` labelled "Cheats", top middle at y = 96 (Genie's is at 64).

- [ ] **Step 1: Write the failing tests** in `CheatRules_test.cpp`:
  - `CheatRules.SearchIsCaseInsensitiveAndSorted`: {Raptor 156210000, "raptor bow" 2, "" 3,
    Dagger 110010000, Dagger 110050000} and query "RAP" → [156210000, 2]; query "dagger" → both
    daggers in ID order; query "  " → empty; maxResults 1 → 1 result.
  - `CheatRules.VariantLabels`:
    - ("Raptor", 5, "Upgrade", 5) → "Raptor (+5)";
    - ("Raptor", 0, "", 0) → "Raptor";
    - ("Raptor", 15, "Strength", 2) → "Strength Raptor (+5)";
    - ("Raptor", 120, "Javana Bow", 4) → "Javana Bow".
  - `CheatRules.MultiplierLabels`: 0, 50, 100, 2500 as above.
  - `CheatRules.EveryResultHasAMessage`: every `e_CheatResult` value gives a non-empty text,
    distinct for each value.
- [ ] **Step 2: Run them and see them fail.**
  - Build: `cmake --build build/mac-sqlite --target Cheats.Tests`
  - Expected: a compile failure.
- [ ] **Step 3: Implement** CheatRules.h, CCheats and the wiring.
- [ ] **Step 4: Run.**
  - `build/mac-sqlite/bin/Debug/Cheats.Tests` and `Genie.Tests` pass.
  - The client builds: `cmake --build build/mac-client -j8` and
    `cmake --build build/mingw-client -j8`.
  - `ctest` passes.
- [ ] **Step 5: Commit:** `WarFare: Cheats controller: WIZ_CHEAT handshake, replies, relaxed MP and shop checks, indicator`.

### Task 8: Client: the Cheats window

**Files:**
- Create: `src/Client/WarFare/UICheats.h` / `.cpp`, in the WarFare CMake list and vcxproj +
  `.filters`.
- Modify:
  - `UIArt.h` / `.cpp`: `CUIArtEdit`; `CUIArtWindow::TakeInput` skips the toggle key while one of
    its own edits has focus.
  - `GameProcedure.h` / `.cpp`: register `s_pUICheats` in the modal-window places listed in
    client-notes (TakeInput chain, `IsModalWindowOpen`, `RenderModalWindows`, `StaticMemberRelease`,
    the static declaration).
  - `GameProcMain.cpp`: J opens it, beside G; `CommandToggleUICheats()` refuses with "This server
    doesn't allow cheats." when not available; `Release` closes it.
  - `WarFareMain.cpp`: wheel routing to the open modal window, with null checks (fixes the
    existing crash when the store was never opened).
  - `Application.h`, `InputScript.cpp/.h`, `WarFareMain.cpp`: a `cheats` input-script verb (like
    `genie`).

**Interfaces:**
- Consumes: Task 7 (`CCheats`, `CheatRules.h`), Task 1 (steps, zones).
- Produces:
  - `class CUIArtEdit : public CN3UIEdit` with `bool Create(CN3UIBase* parent, const RECT&,
    int iMaxLen)`. It creates its `CN3UIString` child and sets `m_pBuffOutRef`. Return sends
    `UIMSG_EDIT_RETURN` to the parent.
  - `class CUICheats : public CUIArtWindow` with `SetToggleKey(DIK_J)`.
    - `Close()` kills its edits' focus before `CUIArtWindow::Close()`.
    - `Tick` shows the confirmed settings.
    - Edits apply on change through `SendSettings` with a copy of the confirmed settings plus that
      one change.
- **Layout** (rows follow `CUIGenie::AddRow`'s pattern; Genie itself isn't refactored):
  - **Every page:** the "Cheats on" row (Off/On) and the tabs **Multipliers**, **Character**,
    **Items**, **World**.
  - **Multipliers:** EXP, Damage dealt, Damage taken, Coins, Item drops, National Points,
    Respawn speed. Each is a value row over its steps, labelled with `MultiplierLabel`.
  - **Character:**
    - Level: a value row 1–80, with −10/+10 buttons beside it, and Apply.
    - Buttons: Add 10 stat points, Add 10 skill points, Reset stats, Reset skills, Refill HP/MP.
    - Infinite MP: Off/On.
    - Coins, NP and Knight Cash: a value row over its steps, each with Set.
  - **Items:**
    - A search `CUIArtEdit` with a Search button (Return searches too).
    - A results list of 8 rows (icon + name, page arrows and the wheel).
    - Picking a result requests its variants and shows them in a paged list (icon +
      `VariantLabel`; only IDs the server listed and the client's tables can show).
    - A count row (`COUNTABLE_STEPS` or `SINGLE_STEPS` by `byContable`) and Give.
    - Set +level: a slot row (Right hand, Left hand, Head, Chest, Legs, Gloves, Feet), a +level
      row 0–10, and Apply.
    - Toggles: No durability loss, Arrows & potions not used up, No weight limit, Shops are free.
    - A Repair everything button.
  - **World:**
    - A zone row over `Cheats::ZONES` with Go.
    - An NPC search `CUIArtEdit` with Search, a results list (name + zone name, up to 9) and Go.
    - A Kill monsters around me button.
    - A Weather row (Normal, Clear, Light rain, Heavy rain, Light snow, Heavy snow) and a Time row
      (Normal, 00:00–23:00).
- Item search uses `s_pTbl_Items_Basic.GetMap()` (never the `GetIndexedData` loop). Icons come
  from `MakeResrcFileNameForUPC`, as the Power-up Store gets them.

- [ ] **Step 1: Build it.** Build the client on Mac and MinGW. There's no client unit test for UI
  code; the logic it uses was tested in Task 7.
- [ ] **Step 2: Do a scripted app run** in a test HOME (`tools/run_app.sh`, the `cheats` verb,
  snapshots of each page). Then:
  - turn on, set EXP ×10, kill a monster, and check the EXP gain is about ten times normal;
  - search "blessed upgrade", give 1, and check it's in the bag;
  - teleport to Moradon;
  - **Review Focus 3:** type in the item search, press Esc, then I opens the inventory; also type
    "j" in the search and check the window stays open;
  - **wheel:** scroll in the Genie window without ever having opened the store, with no crash.
- [ ] **Step 3: Lint and format.** clang-format; clang-tidy on the changed lines (mingw-nopch).
- [ ] **Step 4: Commit:** `WarFare: the Cheats window (J): multipliers, character, items and world pages; a text field for code-built windows`.

### Task 9: End to end, and the player's playtest

**Files:**
- Memory: update the KnightOnline memory file (cheats done, J key, `cheats.ini`).

- [ ] **Step 1: Build and package.**
  - `cmake --build build/mac-app` (Release).
  - `python3 port/app/make_app.py --build build/mac-app --assets assets --seed build/seed/KN_online.sqlite --out build/app`.
- [ ] **Step 2: Do a scripted run of the packaged app** in a test HOME. Check that:
  - `gameserver.ini` gets `[CHEATS] ALLOWED=1`;
  - `cheats.ini` is written after a change and survives an app restart (Review Focus 5);
  - AIServer.log shows the settings line after a restart.
- [ ] **Step 3: Run every check.**
  - `ctest -j8`, plus `Cheats.Tests`, `Genie.Tests` and `Ebenezer.Tests`, all green.
  - The MinGW client build passes.
  - clang-format and clang-tidy are clean.
- [ ] **Step 4: Push, and wait for CI on the in-fork PR #1** (`--repo MostlyEmre/KnightOnline`):
  all jobs green.
- [ ] **Step 5: Hand over.** Give the player playtest instructions: J opens the window, then one
  short check per page, including the Raptor +5 → +6 path (Set +level, or give a Blessed Upgrade
  Scroll).
