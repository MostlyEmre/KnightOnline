# Genie (auto-hunt) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Genie that hunts, survives, loots, repairs, restocks potions and arrows, and goes to town by itself. The server must allow it, through a WIZ_GENIE (0x97) handshake.

**Architecture:**
- **Server:** Ebenezer announces Genie on game start and accepts start/stop. Nothing is stored in the database.
- **Client logic:** all hunting lives in a `CGenie` controller, ticked by `CGameProcMain`. Its decisions are pure functions in a header-only `GenieRules.h`, tested by a new `Genie.Tests` project. Settings are a header-only `GenieSettings.h` over `CIni`.
- **Client window:** `CUIGenie`, a `CUIArtWindow` like `CUIOptions`.
- **Actions:** only packets the client already sends. Where the client's own result handling depends on a dialog Genie bypasses (buy, repair, loot), Genie applies the result itself.

**Tech Stack:** C++20, existing Open-KO client (WarFare/N3Base) and Ebenezer, GoogleTest, CMake plus MSBuild (vcxproj/slnx).

**Spec:** `port/docs/2026-10-24-genie-design.md`. Read it first; this plan argues from it.

## Global Constraints

- Work only in the fork, MostlyEmre/KnightOnline, on branch `macos-port`. No upstream PRs.
- Keep the Windows build (MSBuild and the MinGW cross-build) and both DB backends working. Genie adds no database state.
- New files go in CMake and in the vcxproj plus `.filters` (and in `Tests.slnx`/`All.slnx` for a new project). C++ files start with a UTF-8 BOM and are formatted with clang-format 19. Changed lines must pass clang-tidy (see `port/` lint notes).
- Allowed protocol change: WIZ_GENIE = 0x97 with the sub-opcodes in Task 1. No other protocol or asset changes.
- The client must never send WIZ_GENIE unless the server has sent `GENIE_AVAILABLE` with `allowed = 1`. Ebenezer disconnects clients that send unknown opcodes (`User::Parsing` default case, release builds).
- Spec defaults are exact:
  - attack skills: 8 slots, basic attack on;
  - heal skills at 30% HP;
  - HP potion on at 70%, MP potion on at 35%;
  - range 25 (allowed 3–80);
  - monster list up to 8, empty means any monster;
  - repair on at 10% durability;
  - loot: all kinds and grades, minimum sell price 0;
  - buying potions and arrows off, coin floor 0;
  - all town triggers on.
- Plan defaults the spec left open:
  - keep 50 HP potions, 50 MP potions and 500 arrows;
  - decision tick every 0.3 s;
  - a refused skill is held back 3 s.
- Only monsters are targeted: an NPC whose `m_InfoBase.eNation == NATION_NOTSELECTED`, with `m_pShapeExtraRef == nullptr`, that is alive.
- /town only when `iHP * 2 >= iHPMax` and the player isn't stunned (the client's own CMD_TOWN rule).
- Scripted app runs never take focus and never show modal dialogs. Use the scratchpad `tools/run_app.sh` with a test HOME holding a copy of the player's database. Never touch the real Application Support data.

## Review Focus

1. **Unreachable target.** A monster Genie can't reach or hurt (stuck behind terrain, or off the attack path) must not hold Genie forever. A target with no progress for 10 s is skipped for 30 s. Test: `GenieRules.SkipsStalledTarget`, Task 2.
2. **No answer to GENIE_START.** If no reply arrives within 5 s, Genie isn't running, Start works again, and the message window says so. Test: `GenieRules.StartTimesOut`, Task 2, used by Task 4.
3. **Bag fills mid-loot.** A loot result of 6 or 7 (too heavy, or bag full) must trigger the bag-full town rule at once, not after the next kill. Test: `GenieRules.LootResultFillsBag`, Task 2, wired in Task 7.
4. **Out of MP with basic attack off.** When nothing is castable and nothing is attacking, Genie sits until MP is back to 50%, instead of standing still. Test: `GenieRules.SitsToRecoverMana`, Task 2.
5. **Saved merchant no longer valid.** A refused buy (result 0) reports in the message window, stops buying for that item this session, and falls through to the town rule when empty. Test: `GenieRules.RefusedBuyDisablesRestock`, Task 2, wired in Task 8.

---

### Task 1: Server handshake (WIZ_GENIE)

**Files:**
- Modify: `src/shared/packets.h`: add `WIZ_GENIE = 0x97` to the WIZ enum; add `enum e_GenieOpcode`.
- Modify: `src/Server/Ebenezer/EbenezerApp.h` / `.cpp`: add `bool m_bGenieEnabled = true;`, read beside `ZONE_INFO`/`MY_INFO` in the config loader.
- Modify: `src/Server/Ebenezer/User.h` / `.cpp`: add the handler, the announcement, the state and its reset.
- Test: `tests/Server/Ebenezer/Genie_test.cpp`, registered in `CMakeLists.txt`, `Ebenezer.Tests.vcxproj` and `.filters`.

**Interfaces:**
- Produces, in `packets.h`, documented like `e_StoreOpcode`:
  ```cpp
  enum e_GenieOpcode : uint8_t {
      GENIE_AVAILABLE = 1, // Server: [uint8 allowed]
      GENIE_START     = 2, // Client: [] -> Server: [uint8 ok]
      GENIE_STOP      = 3  // Client: []
  };
  ```
- Produces, in `CUser`:
  - `bool m_bGenieActive = false;`
  - `void Genie(char* pBuf);`
  - `void SendGenieAvailable();`
- Produces, in `EbenezerApp`: `bool m_bGenieEnabled`, from `iniFile.GetBool("GENIE", "ENABLED", true)`.

- [ ] **Step 1: Write the failing tests** in `Genie_test.cpp`. Use the `ShoppingMall_test.cpp` fixture pattern: TestApp, a user in `CONNECTION_STATE_GAMESTART`, HP 100.
  - `GenieTest.AnnouncesAvailability`:
    - `SendGenieAvailable()` sends exactly `[WIZ_GENIE][GENIE_AVAILABLE][1]`, 3 bytes.
    - With `_app->m_bGenieEnabled = false` it sends `[..][0]`.
  - `GenieTest.StartAndStop`:
    - `[GENIE_START]` replies `[WIZ_GENIE][GENIE_START][1]` and sets `m_bGenieActive`.
    - `[GENIE_STOP]` sends nothing and clears it.
  - `GenieTest.StartRefused`:
    - When disabled, the reply is `[..][0]` and the user isn't active.
    - With `m_bResHpType = USER_DEAD`, same.
    - When not in game state, nothing is sent.
- [ ] **Step 2: Run them and see them fail.**
  - Build: `cmake --build build/mac-sqlite --target Ebenezer.Tests -j8`
  - Run: `build/mac-sqlite/bin/Debug/Ebenezer.Tests --gtest_filter='Genie*'`
  - Expected: a compile failure, since `Genie`, `SendGenieAvailable` and `WIZ_GENIE` don't exist yet.
- [ ] **Step 3: Implement.**
  - Add `case WIZ_GENIE: Genie(pData + index);` in `CUser::Parsing`.
  - Call `SendGenieAvailable()` at the end of `CUser::GameStart`'s `opcode == 2` branch.
  - Reset `m_bGenieActive` in `CUser::Initialize()`.
  - `Genie()` ignores everything unless in `CONNECTION_STATE_GAMESTART`.
- [ ] **Step 4: Run.** Expected: `Genie*` passes, and so does the whole suite (`cd build/mac-sqlite && ctest -j8`).
- [ ] **Step 5: Commit.** `Ebenezer: WIZ_GENIE, the Genie handshake: announced on game start, start and stop`.

### Task 2: Genie's decisions as pure functions, and the Genie.Tests project

**Files:**
- Create: `src/Client/WarFare/GenieRules.h`. Header-only, standard library only (no client headers), everything in `namespace Genie`.
- Create: `tests/Client/Genie/CMakeLists.txt`, `GenieRules_test.cpp`, `Genie.Tests.vcxproj` and `.filters`.
  - Copy the MathUtils.Tests layout, with a new project GUID and an include path to `src`.
  - Depend on `fetch-googletest` only.
- Modify:
  - root `CMakeLists.txt`: `add_subdirectory(tests/Client/Genie)` beside `tests/MathUtils`;
  - `Tests.slnx` and `All.slnx`: a `/tests/Client/` folder holding the project;
  - `WarFare.Core.vcxproj` and `.filters`, `src/Client/WarFare/CMakeLists.txt`: list `GenieRules.h`.

**Interfaces (Produces, used by Tasks 3–9):**
```cpp
namespace Genie {
enum class StopReason : uint8_t { None, ByPlayer, Died, BagFull, OutOfHpPotions, OutOfMpPotions,
    OutOfArrows, ServerRefused, NoReply, ZoneChanged, MovedByHand, LeftGame };
const char* StopMessage(StopReason);                 // message-window text, e.g. "Genie stopped: your bag is full."
enum class ItemKind : uint8_t { Consumable, Equipment, Other };
enum class ItemGrade : uint8_t { Normal, Magic, Rare, Unique };
ItemKind KindOfItemClass(int iItemClass);            // e_ItemClass values
ItemGrade GradeOfItemAttrib(int iMagicOrRare);       // e_ItemAttrib values
struct LootFilter { bool bConsumables = true, bEquipment = true, bOther = true;
    bool bNormal = true, bMagic = true, bRare = true, bUnique = true; int iMinSellPrice = 0; };
struct LootCandidate { bool bGold = false; ItemKind eKind = ItemKind::Other;
    ItemGrade eGrade = ItemGrade::Normal; int iSellPrice = 0; };
bool WantsLoot(const LootFilter&, const LootCandidate&);
struct Monster { int iID = -1; float fX = 0, fZ = 0; bool bAttackingMe = false; bool bOnList = true; };
int ChooseTarget(std::span<const Monster>, float fOriginX, float fOriginZ, float fRange, float fSelfX, float fSelfZ);
struct SkillChoice { int iID = 0; bool bReady = false, bUsable = false, bInRange = false, bHeld = false; };
int FirstCastable(std::span<const SkillChoice>);    // -1 when none
bool BelowPercent(int iValue, int iMax, int iPercent);
bool CanTown(int iHp, int iHpMax, bool bStunned);
struct Supplies { int iFreeSlots = 28; bool bUsesHpPotion = false; int iHpPotions = 0;
    bool bUsesMpPotion = false; int iMpPotions = 0; bool bUsesArrows = false; int iArrows = 0; };
struct TownRules { bool bBagFull = true, bNoHpPotions = true, bNoMpPotions = true, bNoArrows = true; };
StopReason TownReason(const Supplies&, const TownRules&);
StopReason LootResultReason(uint8_t byResult);       // 6, 7 -> BagFull; else None
int RestockCount(int iHave, int iKeep, int iUnitPrice, int64_t iGold, int64_t iCoinFloor, int iStackRoom);
struct RestockState { bool bDisabled = false; };       // set after a refused buy, Review Focus 5
bool WantsRestock(const RestockState&, int iHave, int iKeep);   // !bDisabled && iHave < iKeep
class TargetProgress { public:                       // Review Focus 1
    void Reset(int iTargetID, float fNow); void OnProgress(float fNow);
    bool ShouldSkip(float fNow) const;                // no progress for 10 s
};
bool StartTimedOut(float fSentAt, float fNow);        // 5 s, Review Focus 2
bool ShouldSitToRecover(bool bAttacked, bool bAnyCastable, int iMsp, int iMspMax, bool bSitting);  // Review Focus 4
}
```
Rules the tests pin:
- `KindOfItemClass`:
  - 97, 98, 255 and 120 → Consumable;
  - 11–80, 91–94, 100, 110, 130 and 210–240 → Equipment;
  - anything else → Other.
- `GradeOfItemAttrib`: 0 → Normal, 1 → Magic, 2 → Rare, 3 (craft) → Rare, 4 → Unique, 5 (upgrade) → Normal.
- `WantsLoot`: gold is always wanted. Otherwise the kind must be ticked; the grade is checked only for Equipment; and `iSellPrice >= iMinSellPrice`.
- `ChooseTarget`:
  - The nearest-to-self monster with `bAttackingMe` wins, regardless of list or range.
  - Otherwise, the nearest-to-self monster with `bOnList` whose distance from the origin is at most `fRange`.
  - Otherwise -1.
- `TownReason`: checks in the order BagFull (no free slots), HP, MP, arrows. A rule applies only when it's ticked and the supply is in use.
- `RestockCount`:
  - `max(0, keep - have)`, limited by `iStackRoom` and by `(gold - floor) / price`;
  - 0 when the price is ≤ 0 or `gold <= floor`.
- `BelowPercent(v, max, p)`: `max > 0 && v * 100 < max * p`.
- `CanTown`: `max > 0 && hp * 2 >= max && !stunned`.

- [ ] **Step 1: Write the failing tests** in `GenieRules_test.cpp`:
  - `WantsLoot.GoldAlways` and `WantsLoot.KindGradeAndPrice`;
  - `ChooseTarget.AttackerFirst` and `ChooseTarget.NearestInRangeOnList` (a monster at range 26 from the origin is never chosen with range 25);
  - `TownReason.Order` and `TownReason.OnlyWhenInUse`;
  - `RestockCount.LimitedByCoinFloorAndStack`, for example `RestockCount(10, 50, 30, 1000, 400, 9999) == 20`;
  - `CanTown.HalfHpRule`, true at 50/100, false at 49/100;
  - `GenieRules.SkipsStalledTarget`, `GenieRules.StartTimesOut`, `GenieRules.LootResultFillsBag`, `GenieRules.SitsToRecoverMana`;
  - `GenieRules.RefusedBuyDisablesRestock`: `WantsRestock({true}, 0, 50)` is false; `WantsRestock({false}, 0, 50)` is true.
  - Item class and attrib mappings, one test each.
- [ ] **Step 2: Run them and see them fail.**
  - Build: `cmake build/mac-sqlite && cmake --build build/mac-sqlite --target Genie.Tests -j8`
  - Run: `build/mac-sqlite/bin/Debug/Genie.Tests`
  - Expected: a compile failure, since `GenieRules.h` doesn't exist yet.
- [ ] **Step 3: Implement `GenieRules.h`** to the rules above.
- [ ] **Step 4: Run.** Expected: everything passes; `ctest` lists GenieTests.
- [ ] **Step 5: Commit.** `WarFare: Genie's decisions (targets, loot filter, town rules, restocking) as tested pure functions`.

### Task 3: Genie settings, saved per character

**Files:**
- Create: `src/Client/WarFare/GenieSettings.h`. Header-only; uses `GenieRules.h` and `shared/Ini.h`.
- Test: `tests/Client/Genie/GenieSettings_test.cpp`. `Genie.Tests` links `shared`.

**Interfaces (Produces):**
```cpp
namespace Genie {
struct Merchant { int iNpcID = -1; int iTradeID = 0; bool IsSet() const { return iNpcID >= 0 && iTradeID > 0; } };
struct Settings {
    std::array<int, 8> attackSkills {}; bool bBasicAttack = true;
    std::array<int, 8> buffSkills {};   std::array<int, 8> healSkills {}; int iHealPercent = 30;
    bool bHpPotion = true; int iHpPercent = 70; int iHpPotionID = 0;
    bool bMpPotion = true; int iMpPercent = 35; int iMpPotionID = 0;
    int iRange = 25; std::vector<std::string> monsters;                    // up to 8
    bool bRepair = true; int iRepairPercent = 10;
    LootFilter loot;
    bool bBuyPotions = false; int iKeepHpPotions = 50, iKeepMpPotions = 50;
    bool bBuyArrows = false;  int iArrowID = 0; int iKeepArrows = 500;
    int64_t iCoinFloor = 0;
    Merchant potionMerchant, arrowMerchant;
    TownRules town;
};
void Load(CIni& ini, Settings& out);        // missing keys -> defaults; out-of-range values clamped (range 3–80, % 1–99)
void Save(CIni& ini, const Settings& in);
std::filesystem::path SettingsPath(const std::string& szBaseDir, const std::string& szCharName); // <base>/Genie_<name>.ini
}
```
- [ ] **Step 1: Write the failing tests:**
  - `GenieSettings.DefaultsWhenMissing`: an empty `CIni` gives every spec default.
  - `GenieSettings.RoundTrip`: set every field to a non-default value, `Save`, write to a temp file, reload, and compare.
  - `GenieSettings.ClampsOutOfRange`: range 200 becomes 80, HP percent 0 becomes 1, a monster list of 9 becomes 8.
- [ ] **Step 2: Run them and see them fail** (compile failure).
- [ ] **Step 3: Implement.** One `[Genie]` section, scalar keys, and `MonsterN`/`AttackSkillN`-style indexed keys.
- [ ] **Step 4: Run.** Expected: everything passes.
- [ ] **Step 5: Commit.**

### Task 4: Client plumbing: the controller skeleton, the window's Start/Stop, G, the indicator

**Files:**
- Create: `src/Client/WarFare/Genie.h` / `.cpp` (`CGenie`).
- Create: `src/Client/WarFare/UIGenie.h` / `.cpp` (`CUIGenie`): this task gives it only the Start/Stop button, the status line and the close controls. Task 9 adds the pages.
- Modify:
  - `UIArt.h` / `.cpp`: `CUIArtWindow` gets `void SetToggleKey(int iDIK)`. When set, that key closes the window in `TakeInput`.
  - `GameProcedure.h` / `.cpp`:
    - `static CUIGenie* s_pUIGenie`, ticked in the modal chain after the store;
    - rendered under the store;
    - counted in `IsModalWindowOpen()`;
    - released in `StaticMemberRelease`.
  - `GameDef.h`: `KM_TOGGLE_GENIE = DIK_G`.
  - `GameProcMain.h` / `.cpp`:
    - `CGenie* m_pGenie`, created in `Init` and released in `Release`;
    - `case WIZ_GENIE: MsgRecv_Genie(pkt)`;
    - in `ProcessLocalInput` beside `KM_TOGGLE_INVENTORY`: `IsKeyPress(KM_TOGGLE_GENIE)` → `CommandToggleUIGenie()`;
    - `m_pGenie->Tick(CN3Base::TimeGet())` in `Tick` after `m_pMagicSkillMng->Tick()`;
    - the indicator rendered after `CGameProcedure::Render()` in `Render`.
  - `src/N3Base/Platform/Application.h`: `std::function<void()> OnShowGenie;`.
  - `src/N3Base/Platform/macOS/InputScript.cpp`: a `genie` verb, like `options`. Scripted keys don't reach `ProcessLocalInput` without focus.
  - `WarFareMain.cpp`: wire `OnShowGenie`.
  - CMake, vcxproj and `.filters` for the new files.

**Interfaces:**
- Consumes: Task 1's opcodes, Task 2's `StopReason`/`StopMessage`/`StartTimedOut`, Task 3's `Settings`/`Load`/`Save`/`SettingsPath`.
- Produces, in `CGenie`. Later tasks add behaviour behind these; the names are fixed here:
  ```cpp
  class CGenie {
  public:
      explicit CGenie(CGameProcMain* pMain);
      void OnAvailable(bool bAllowed);  bool IsAvailable() const;
      void OnStartResult(bool bOk);
      void RequestStart();              void Stop(Genie::StopReason eReason);
      bool IsRunning() const;           bool IsWaitingForStart() const;
      void Tick(float fNow);
      void OnLeaveGame();               // CGameProcMain::Release: Stop(LeftGame), availability reset
      Genie::Settings& Settings();      void LoadSettings(); void SaveSettings(); // per s_pPlayer->m_InfoBase.szID, base dir as Option.ini (CN3Base::PathGet() via PathResolver)
      void RenderIndicator();           // a small "Genie" label while running
  };
  ```
- Produces, in `CGameProcMain`: `void MsgRecv_Genie(Packet&)`, `bool CommandToggleUIGenie()`.

- [ ] **Step 1: Build check.** The client builds with the skeleton: `cmake --build build/mac-client -j8`, then `cmake build/mingw-client && cmake --build build/mingw-client -j8`. Expected: exit 0, no warnings.
- [ ] **Step 2: Implement the protocol rules:**
  - `RequestStart` does nothing unless available and not waiting. It saves the settings, sends `[WIZ_GENIE][GENIE_START]` and starts the timeout.
  - `OnStartResult(false)`, or `StartTimedOut`, means `Stop(ServerRefused)` or `Stop(NoReply)` with its message.
  - `Stop` sends `[WIZ_GENIE][GENIE_STOP]` only if running, stops attacking and moving, and prints `StopMessage`.
  - The window's Start button is disabled, with the reason ("This server doesn't allow Genie."), until `OnAvailable(true)`.
- [ ] **Step 3: Scripted app run.**
  - Test dir `genie1`, with a copy of the player's DB. Script: login, enter, `genie`, snapshot, click Start (with `move` first), wait 60, snapshot, key ESCAPE, snapshot, quit.
  - Expected:
    - the window shows with Start enabled;
    - the indicator shows after Start, and the message window says Genie started;
    - Ebenezer.log has no "Unhandled opcode".
  - With `[GENIE] ENABLED=0` in the test home's gameserver.ini, Start is disabled.
- [ ] **Step 4: Commit.**

### Task 5: Hunting: targets, attacks, movement, stop conditions

**Files:** `Genie.cpp` / `.h`; `GameProcMain.cpp` hooks.

**Interfaces:**
- Consumes: `ChooseTarget`, `FirstCastable`, `TargetProgress`, `ShouldSitToRecover`.
- Produces: `void OnAttackedBy(int iAttackerID)`, `void OnManualControl()`, `void OnDied()`, `void OnZoneChanged()`.
- Client calls to use:
  - Monsters come from `s_pOPMgr->m_NPCs`, filtered by the monster rule in Global Constraints. "On the list" compares `IDString()` with `Settings().monsters`.
  - Origin is `s_pPlayer->Position()` at start; distance is in x/z.
  - Basic attack: `TargetSelect(id, true)`, then `CommandEnableAttackContinous(true, pTarget)`. Melee walks by itself in third person only, so always move explicitly.
  - Moving: `SetMoveTargetID(id)` or `SetMoveTargetPos(v)`, then `CommandMove(MD_FORWARD, true)`; stop with `CommandMove(MD_STOP, true)`. Stop moving once in range of the chosen skill (`iValidDist`) or `AttackableDistance`.
  - Skills: `s_pTbl_Skill.Find(id)`. Ready means `m_pMagicSkillMng->GetCooldown(p) < 0`. Usable means `CheckValidSkillMagic(p)`. Learned means `m_pUISkillTreeDlg->HasIDSkill(id)`. In range means the distance ≤ `iValidDist`.
  - Casting is `MsgSend_MagicProcess(targetId, p)`. A `false` return holds the skill for 3 s.
  - Area-target skills (`iTarget` 10/11/12) aim at the target's position. Set `m_vMouseSkillPos` and advance `m_dwRegionMagicState` as a mouse click does; see `CMagicSkillMng::Tick` around the region branch.
  - Attackers come from `MsgRecv_Attack` when `bIAmTarget` and the attacker is an NPC, and from `CMagicSkillMng::MsgRecv_Casting` when an NPC targets the player. Remember each for 10 s.
- Hooks:
  - `OnManualControl()` from W, S, E and R in `ProcessLocalInput`, and from ground click, drag-move, Ctrl+click and double-click in `OnMouseLBtnPress` / `OnMouseLbtnDown` / `OnMouseLDBtnPress`. A/D rotation doesn't count.
  - `OnDied()` from `MsgRecv_Dead` for the player.
  - `OnZoneChanged()` from `InitZone` when the zone changes.
- Order each 0.3 s tick: survival (Task 6) → fighting → loot (Task 7) → return to origin. With no target and no loot, walk back when more than 3 m from the origin.

- [ ] **Step 1: Implement**, using the rules from Task 2 for every decision.
- [ ] **Step 2: Scripted app run** in the field (the player's character stands among worms near Moradon).
  - Start Genie and wait 1200 frames; snapshot every 300; quit.
  - Expected:
    - kills (EXP and "Earned N Knight Cash" lines in the message window);
    - the character returns near its start spot;
    - no hostile NPC guards are attacked.
  - Then a second run: after starting, `move` then `click` on open ground (in-world mouse clicks work from scripts; keys don't, since the window isn't focused). Expected: "Genie stopped: you took control."
- [ ] **Step 3: Commit.**

### Task 6: Survival: potions, heals, buffs, sitting

**Files:** `Genie.cpp` / `.h`.

**Interfaces:**
- Consumes: `BelowPercent`, `ShouldSitToRecover`.
- Client calls to use:
  - A potion's skill is `s_pTbl_Skill.Find(pItemBasic->dwEffectID1)` of the chosen potion item (`Settings().iHpPotionID` / `iMpPotionID`). Cast it on the player: `MsgSend_MagicProcess(s_pPlayer->IDNumber(), p)`, when its cooldown is ready.
  - Count with `m_pUIInventory->GetIndexItemCount(base id)`. It sums the stacks; `GetCountInInvByID` returns only the first stack.
  - HP is `m_InfoBase.iHP/iHPMax`; MP is `m_InfoExt.iMSP/iMSPMax`.
  - Heal slots are cast on the player below `iHealPercent`.
  - Buff slots are cast on the player when the skill's type-4 `iBuffType` (from `m_pMagicSkillMng->m_pTbl_Type_4.Find(id)`) isn't in `m_ListBuffTypeID`.
  - Sit with `CommandSitDown(false, true)` when `ShouldSitToRecover`; stand before moving or attacking.
- [ ] **Step 1: Implement.**
- [ ] **Step 2: Scripted app run.**
  - Pre-write `Genie_<name>.ini` in the test home: an HP potion the character carries, HP 90%, and a buff the character knows (look these up in the character's inventory and skills in the test DB).
  - Expected: potion counts drop as HP falls, and the buff icon reappears after it expires.
- [ ] **Step 3: Commit.**

### Task 7: Looting from a distance

**Files:**
- `src/Client/WarFare/UIDroppedItemDlg.h` / `.cpp`: quiet mode.
- `Genie.cpp` / `.h`.
- `GameProcMain.cpp`: `MsgRecv_ItemDroppedGetResult` → `m_pGenie->OnLootResult`.

**Interfaces:**
- Produces, in `CUIDroppedItemDlg`:
  - `void BeginQuietLoot(int iBundleID, std::function<bool(const __IconItemSkill&)> fnWants)`. It sets `m_iItemBundleID`. The next `MsgRecv_ItemBundleOpen` fills the table without showing the dialog, then requests only the wanted items through the Loot All chain (`LootNext` / `ContinueLootAll`), and leaves the dropped state when done.
  - `bool IsQuietLooting() const`.
- Produces, in `CGenie`: `void OnLootResult(uint8_t byResult)`, which applies `LootResultReason`.
- Genie behaviour:
  - Corpses come from `s_pOPMgr->m_Corpses` with `m_iDroppedItemID > 0`, each tried once.
  - Genie sends `[WIZ_BUNDLE_OPEN_REQ][DWORD bundle]` itself. `MsgSend_RequestItemBundleOpen` refuses distance.
  - The server ignores a bundle in another region. With no open reply within 1 s, walk to the corpse and retry once.
  - The filter is a `LootCandidate` from `pItemBasic->byClass`, `pItemExt->byMagicOrRare` and `GetSellPrice()`.
  - Gold is always taken.
- [ ] **Step 1: Implement.** Keep the visible loot window and Loot All working as before: quiet mode is off by default.
- [ ] **Step 2: Scripted app run.**
  - Start Genie with a loot filter that skips Consumables; hunt 1200 frames.
  - Expected:
    - coins rise;
    - no loot window appears;
    - skipped consumables stay out of the inventory;
    - the visible loot window still works by hand afterwards (right-click a corpse in a second run).
- [ ] **Step 3: Commit.**

### Task 8: Upkeep: repair, town, buying

**Files:** `Genie.cpp` / `.h`; `GameProcMain.cpp` (`MsgRecv_ItemRepair`, `MsgRecv_ItemTradeResult`, `MsgRecv_ItemTradeStart` hooks).

**Interfaces:**
- Consumes: `TownReason`, `CanTown`, `RestockCount`, `WantsRestock`.
- Produces, in `CGenie`:
  - `void OnShopOpened(int iNpcID, int iTradeID)`;
  - `bool OnTradeResult(uint8_t byResult, uint8_t byType, int iMoney)`, true when it was Genie's buy;
  - `bool OnRepairResult(uint8_t byResult, int iGold)`, true when it was Genie's repair.
- **Repair:**
  - For an equipped slot (`m_pUIInventory->m_pMySlot[i]`) below `iRepairPercent` of max durability (`pItemBasic->siMaxDurability + pItemExt->siMaxDurability`), send `[WIZ_ITEM_REPAIR][0x01][slot][DWORD item id]`. One at a time.
  - On Genie's result: success sets `iDurability` to max and `iGold`, and refreshes the inventory's gold display.
- **Town:**
  - When `TownReason` isn't `None` and `CanTown`: `Stop(reason)`, then send `WIZ_HOME` as a 2-byte word, exactly as CMD_TOWN does.
  - When it isn't `CanTown`: recover. No new targets; defend against attackers; potions and heals; sit when nothing is attacking. Re-check every tick.
- **Merchants:**
  - `OnShopOpened` comes from `MsgRecv_ItemTradeStart`, with the NPC ID in `s_pPlayer->m_iIDTarget`.
  - It scans the item table as `CUITransactionDlg::EnterTransactionState` does (`bySellGroup == iTradeID / 1000`, ext `== iTradeID % 1000`).
  - It saves `potionMerchant` if any listed item has `byClass == ITEM_CLASS_POTION`, and `arrowMerchant` if any has `ITEM_CLASS_ARROW`, then calls `SaveSettings()`.
- **Buying:**
  - When `WantsRestock` and the merchant is set: pick a bag slot, either the existing stack with room or an empty slot. Price is `GetBuyPrice()` of the item.
  - Send exactly the `CUITransactionDlg::SendToServerBuyMsg` layout: `[WIZ_ITEM_TRADE][0x01][DWORD tradeId][short npcId][DWORD itemId][byte pos][short count]`.
  - On Genie's result:
    - success sets `iGold = iMoney` and adds the items to `m_pMyInvWnd[pos]`: a count increase, or a new `__IconItemSkill` built as `CUIDroppedItemDlg::GetItemByIDToInventory` builds one.
    - failure (`byResult == 0`) prints "Genie couldn't buy <item>: open a shop that sells it to save a new merchant." and sets that item's `RestockState.bDisabled`.
  - `MsgRecv_ItemTradeResult` routes to Genie first when a Genie buy is pending and `m_pUITransactionDlg` isn't open.
  - Arrows: the item is the bow basic-attack skill's `dwExhaustItem` (skill 102003, launcher 102009). "Uses arrows" means the weapon matches `CPlayerMySelf::Tick`'s bow/launcher branch.
- [ ] **Step 1: Implement.**
- [ ] **Step 2: Scripted app runs:**
  - Pre-damage gear isn't possible from a script, so set `iRepairPercent = 99` in the test settings. Expected: equipped items are repaired, coins drop, and durability tooltips show max.
  - Buying:
    - In the test DB copy, stand the character next to Moradon's potion merchant: take its position from `NPC_POS` and set `USERDATA` px/pz.
    - Script: `move` and right-click the merchant to open its shop, close it, then `genie` and Start, with buying on and `iKeepHpPotions` above the current count.
    - No monsters are near in town, so Genie restocks straight away.
    - Expected: the potion count rises, coins drop, the saved merchant shows in the settings file, and the server log has no errors.
  - Use a test DB where the character has 1 HP potion, HP potion on at 99% and buying off. Expected: after the last potion, "Genie stopped: out of HP potions." and the character is in town.
- [ ] **Step 3: Commit.**

### Task 9: The full Genie window

**Files:** `UIGenie.h` / `.cpp`.

**Interfaces:**
- Consumes: `CGenie::Settings()`, `SaveSettings()`, `RequestStart()`, `Stop()`, `IsRunning()`, `IsAvailable()`, and the saved merchants.
- Layout and controls:
  - Built like `CUIOptions`: tabs **Attack**, **Recovery**, **Loot**, **Supplies**; rows with ◀ value ▶; Start/Stop and Close at the bottom.
  - Every change goes to `Settings()` and is saved on close and on Start.
  - **Attack:**
    - 8 skill slots: icon buttons on `UIArt::SLOT` art. Clicking one opens a picker of the character's learned enemy-target skills, with icons, plus "Clear".
    - Basic attack On/Off; Range 3–80 in steps of 1/5.
    - The monster list: "Add target" adds the current target's name if it's a monster; each entry can be removed.
  - **Recovery:**
    - HP potion On/Off, percent and the potion. The potion choices are the potions in the bag plus the saved potion merchant's potions, by name.
    - The same for MP.
    - 8 heal slots and the heal percent; 8 buff slots (self-buffs only).
  - **Loot:** the three kinds, the four grades, and the minimum sell price in coin steps.
  - **Supplies:**
    - Repair On/Off and percent;
    - Buy potions On/Off with keep counts; Buy arrows On/Off with the arrow and keep count;
    - the coin floor;
    - the four town rules;
    - the saved merchants' names, or "Open a shop that sells potions/arrows to set one".
- [ ] **Step 1: Implement.**
- [ ] **Step 2: Scripted app run.**
  - Open with `genie` and snapshot each tab (click the tabs with `move` first); change a few values; close; reopen; snapshot.
  - Expected: values persist, `Genie_<name>.ini` holds them, and nothing overflows the window at 1470×924.
- [ ] **Step 3: Commit.**

### Task 10: Wrap-up

- [ ] **Step 1: Format and lint.**
  - Run clang-format on every changed and new C++ file.
  - Run changed-lines clang-tidy: mingw-nopch for the client (Windows target flags as in the port notes); a stripped mac-sqlite compile DB for the servers and tests.
  - Expected: 0 diagnostics.
- [ ] **Step 2: Build everything.**
  - Builds: `build/mac-client`, `build/mac-app` (Release), `build/mac-sqlite` with `ctest -j8` (all pass, including GenieTests), and `build/mingw-client`.
  - Expected: exit 0.
- [ ] **Step 3: Package.** `python3 port/app/make_app.py --build build/mac-app --assets assets --seed build/seed/KN_online.sqlite --out build/app`. Then one end-to-end scripted run: Start, hunt 2000 frames, Stop. Check coins, EXP and loot in the test DB.
- [ ] **Step 4: Records.**
  - Update the project memory (`knightonline-macos-port.md`).
  - Commit the design and plan docs with the code.
  - Push.
  - Wait for CI on all six jobs.
