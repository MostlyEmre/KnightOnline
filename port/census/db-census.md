# Open-KO DB census: what a SQLite backend has to replace

This is a read-only census. Neither repo was modified.

> Generated in Phase 0 by a census pass over the server source and OpenKO-db, spot-checked by hand
> (15 `StoredProc<>` uses, the `isClaimed*` schema drift, the 12 seed files with raw NULs). Two
> facts it could not settle statically were answered by reference rig B (SQL Server 2022 loaded by
> kodb-util, see `port/docs/phase0/rig-b-db-inspect.txt`): the database collation is
> `SQL_Latin1_General_CP1_CI_AS`, and the import builds the `isClaimed*` columns (kodb-util uses
> jsonSchema), which is why Ebenezer starts there.

**Inputs**
- Server: `KnightOnline` at commit `7d6cf810`. All `file:line` references below are relative to `src/` (server code) or `deps/` (db-models, nanodbc) unless a full path is given.
- `deps/db-models` is at `edbcb8a`. That commit carries tags `v0.0.6` and `v0.1.2`. `v0.1.2` is the version the CMake build fetches (`cmake/db-models/db-models-config.cmake:5-8`), so this checkout is what actually builds.
- `deps/nanodbc` is at `82f3770`, tag `v2.1.4.0a-OpenKO`, which is the CMake-pinned tag (`cmake/FindNanodbc.cmake:5-8`). Build flags: `NANODBC_DISABLE_NULL_ACCESS_ERROR=ON`, `NANODBC_USE_UINT8_FOR_TINYINT=ON` and `NANODBC_DISABLE_MSSQL_TVP=ON` (`cmake/FindNanodbc.cmake:15-17`, `props/nanodbc.props:6`).
- Database: OpenKO-db at `bec6194` (tag v0.1.2), the same commit rig B imports. It has 76 `5_CreateTable_*` files, 60 `6_InsertData_*`, 1 `7_CreateView_*` and 70 `8_CreateStoredProc_*`, plus jsonSchema with 76 tables and 70 procedures.

**Method.** I grepped `src/` and `deps/db-models` for `StoredProc`, `storedProc::`, `SqlBuilder`, `ModelRecordSet`, `recordset_loader::`, `CreatePoolConnection`, `CreateStatement`, `nanodbc::`, SQL keywords inside string literals, `{call` and `EXEC`. I then read every hit. I also wrote small throwaway Python parsers (not committed; the Phase 1 converter and parity tests re-derive these facts and assert them):
- `parse_models.py` parses the generated model and binder `.cpp` files, the `5_` DDL, and counts seed rows.
- `procscan.py` is a regex feature scanner for T-SQL.
- `seedscan.py` / `nulscan.py` profile the seed data.

I read all 15 procs the servers call line by line. For the 55 procs nobody calls, the feature list is regex-derived and only indicative.

---

## 0. How the DB layer works (needed to read the rest)

| Mechanism | Where | What it does |
|---|---|---|
| `db::StoredProc<P>` | `db-library/StoredProc.h:14-31` | Wraps a generated `storedProc::P`. The constructor pulls a pool connection for `P::DbType()`, which is `GAME` for all 70 procs, and calls `ReconnectIfDisconnected()`. |
| Generated proc classes | `db-models/StoredProc/StoredProc.cpp` (70 classes) | Each has a fixed ODBC escape string (`{CALL X(?,...)}`, or `{? = CALL X(?)}` for 2 procs). `execute()` calls `prepare` → `reset_parameters` → `_stmt.bind(i, …)` / `bind_binary` → `SQLExecute`. |
| Output/flush | `db-models/detail/StoredProc/StoredProcedure.cpp:31-102` | The destructor (`flush_on_destruct`) drains every result set with `next()` plus `next_result()`. It ignores SQLSTATE `24000`. Under SQL Server, OUTPUT parameters and RETURN values only arrive once results are drained, so they are valid only **after the proc object is destroyed**. |
| Destruction order | `db-library/StoredProc.h:30` vs `StoredProcedure.cpp:31` | `StoredProc<P>::_poolConn` is a member of the derived class, so it is destroyed **before** the base `~P()` runs `flush()`. The connection therefore goes back to the pool (`PoolConnection.cpp:13-16`) before the outputs are flushed. This is an existing race, latent today. |
| `SqlBuilder<M>` | `db-library/SqlBuilder.h` | Generates `SELECT`/`COUNT`/`INSERT`/`DELETE` text from model metadata. Exact output is in §2.1. |
| `ModelRecordSet<M,B>` | `db-library/ModelRecordSet.h:83-173` | Prepares the SqlBuilder text. When `fetchRowCount` is set it first runs `SelectCountString()`. It then executes and indexes bindings **by result column name**. |
| `Model::IndexColumnNameBindings` | `db-library/Model.h:101-126` | `result.column_name(i)` is looked up with an exact, case-sensitive key in the binder map. A miss logs a warning and binds `nullptr`, so the field silently stays at its default. |
| `recordset_loader::{Base,STLMap,Vector}` | `db-library/RecordSetLoader*.h` | `Load_ForbidEmpty` fails on an empty table (`RecordSetLoader.h:83-93`). It catches only `DatasourceConfigNotFoundException` and `nanodbc::database_error` (`:110-119`). `STLMap` keeps the first row per `MapKey()` and silently deletes duplicates (`RecordSetLoader_STLMap.h:51-60`). |
| NULL reads | `nanodbc/nanodbc.cpp:3830-3842` | With `NANODBC_DISABLE_NULL_ACCESS_ERROR`, `get_ref<T>` on NULL returns `T{}` (0 or "") with no exception. |
| Integer reads | `nanodbc/nanodbc.cpp:4114-4130, 5069-5086` | `SQL_TINYINT` binds as `SQL_C_UTINYINT`. smallint/int/bigint bind as `SQL_C_SBIGINT` and are then C-cast to the requested C++ type, which wraps silently on overflow. |
| Param typing | `nanodbc/nanodbc.cpp:2225-2257 (prepare_bind → describe_parameters at :2239), 2480-2500` | Every bind calls `SQLDescribeParam` and uses the driver-reported SQL type and size. |

**DbTypes.** All generated models and all 70 procs report `DbType::GAME`; jsonSchema also lists all 146 objects as `"database": "GAME"`. Only two places use `DbType::ACCOUNT`: `Aujard/DBAgent.cpp:55-56` (connection probe) and `:835-836` (the `CONCURRENT` update). The configs default both DSNs to `KN_online` (`Aujard/AujardApp.cpp:124-136`, `VersionManager/VersionManagerApp.cpp:110-119`), so in practice they are one database.

---

## 1. Stored procedures

### 1.1 Call paths found

- **`db::StoredProc<storedProc::…>`**: the only live mechanism. There are 15 call sites, 14 in Aujard and 1 in VersionManager:
  - `Server/Aujard/DBAgent.cpp:110, 448, 489, 508, 534, 569, 640, 705, 731, 750, 771, 988, 1132, 1361`
  - `Server/VersionManager/DBProcess.cpp:243`
- **Raw `{call …}` strings**: only inside commented-out legacy code:
  - `CHECK_COUPON_EVENT` (`Aujard/DBAgent.cpp:1398`)
  - `UPDATE_COUPON_EVENT` (`:1452`)
  - `DELETE_CHAR` (`:1497`)

  The whole block `DBAgent.cpp:1388-1531` is commented out, its own comments say "Proc does not exist", and none of the three exist in OpenKO-db.
- **`EXEC` in strings**: none. Grepping `EXEC` only matches `#include "EXEC.h"` in Ebenezer.
- AIServer, Ebenezer, ItemManager and shared-server call **no** procedures.

**Result: 15 distinct procs called. 55 of 70 never called.** All 70 have generated wrappers; I diffed the `CALL` names in `StoredProc.cpp` against the `8_*` files and they match.

### 1.2 Summary of the 15 called procs

"Out read when" refers to the timing rule in §0. Every query string lives in `db-models/StoredProc/StoredProc.cpp`.

| # | Proc | Server: C++ caller | Upstream trigger | ODBC text (StoredProc.cpp line) | RETURN read? | Result set consumed |
|---|---|---|---|---|---|---|
| 1 | ACCOUNT_LOGIN | Aujard `DBAgent.cpp:482-499` | `AujardApp.cpp:492` | `{CALL ACCOUNT_LOGIN(?,?,?)}` (:18) | no | none (drained by the destructor) |
| 2 | ACCOUNT_LOGOUT | Aujard `DBAgent.cpp:1124-1164` | `AujardApp.cpp:370, 1064` | `{CALL ACCOUNT_LOGOUT(?,?,?,?)}` (:57) | no | none; uses `affected_rows()` |
| 3 | CLAIM_USER_RANK_STIPEND | Aujard `DBAgent.cpp:1354-1386` | `AujardApp.cpp:1191` | `{CALL CLAIM_USER_RANK_STIPEND(?,?,?,?,?)}` (:252) | no | none |
| 4 | CREATE_KNIGHTS | Aujard `DBAgent.cpp:698-722` | `AujardApp.cpp:799` | `{CALL CREATE_KNIGHTS(?,?,?,?,?,?)}` (:326) | no | none |
| 5 | CREATE_NEW_CHAR | Aujard `DBAgent.cpp:526-545` | `AujardApp.cpp:549` | `{CALL CREATE_NEW_CHAR(?×13)}` (:412) | no | none |
| 6 | DELETE_KNIGHTS | Aujard `DBAgent.cpp:743-760` | `AujardApp.cpp:957` | `{CALL DELETE_KNIGHTS(?,?)}` (:504) | no | none |
| 7 | LOAD_ACCOUNT_CHARID | Aujard `DBAgent.cpp:631-696` | `AujardApp.cpp:605` | `{? = CALL LOAD_ACCOUNT_CHARID(?)}` (:1267) | bound as `PARAM_RETURN` into `rowCount` but **never read** | 1 row: columns 0–2 |
| 8 | LOAD_CHAR_INFO | Aujard `DBAgent.cpp:547-629` | `AujardApp.cpp:606-608` (×3) | `{CALL LOAD_CHAR_INFO(?,?)}` (:1305) | no | 1 row: 7 columns |
| 9 | LOAD_KNIGHTS_MEMBERS | Aujard `DBAgent.cpp:762-825` | `AujardApp.cpp:984` | `{CALL LOAD_KNIGHTS_MEMBERS(?)}` (:1343) | no | all rows: 4 columns |
| 10 | LOAD_USER_DATA | Aujard `DBAgent.cpp:79-387` | `AujardApp.cpp:281` | `{CALL LOAD_USER_DATA(?,?,?)}` (:1484) | no | 1 row: 37 columns, **positional** |
| 11 | LOAD_PREMIUM_SERVICE_USER | VersionManager `DBProcess.cpp:237-254` | `VersionManager/User.cpp:210` | `{CALL LOAD_PREMIUM_SERVICE_USER(?,?,?)}` (:1375) | no | none |
| 12 | NATION_SELECT | Aujard `DBAgent.cpp:501-524` | `AujardApp.cpp:512` | `{CALL NATION_SELECT(?,?,?)}` (:1555) | no | none |
| 13 | UPDATE_KNIGHTS | Aujard `DBAgent.cpp:724-741` | `AujardApp.cpp:835, 865, 899` | `{CALL UPDATE_KNIGHTS(?,?,?,?,?)}` (:2081) | no | none |
| 14 | UPDATE_USER_DATA | Aujard `DBAgent.cpp:389-480` | `AujardApp.cpp:404, 424, 472` | `{CALL UPDATE_USER_DATA(?×38)}` (:2523) | no | none; uses `affected_rows()` |
| 15 | UPDATE_WAREHOUSE | Aujard `DBAgent.cpp:952-1014` | `AujardApp.cpp:408, 431, 463` | `{CALL UPDATE_WAREHOUSE(?,?,?,?,?)}` (:2604) | no | none; uses `affected_rows()` |

**None of the 15 uses `SET NOCOUNT ON`.** `procscan.py` finds `SET NOCOUNT` only in five uncalled procs: CHECK_KNIGHTS, EDITER_KNIGHTS, EXEC_KNIGHTS_USER, RANK_KNIGHTS and USER_KNIGHTS_RATING_UPDATE. None uses TRY/CATCH, cursors, WHILE, TOP, temp tables, table variables, dynamic SQL, IDENTITY, MERGE, UPDATE…FROM, string functions, CONVERT/CAST, or nested EXEC.

### 1.3 Per-proc detail

Notation for parameter direction: in, out (`PARAM_OUT`, so the client sends no value and the T-SQL variable starts NULL), ret (`PARAM_RETURN`). C++ types come from the generated `execute()` signature. The nanodbc C type follows from the C++ type: `const char*` → `SQL_C_CHAR`, `uint8_t` → `SQL_C_UTINYINT`, `int16_t` → `SQL_C_SSHORT`, `int32_t` → `SQL_C_SLONG`, `int64_t` → `SQL_C_SBIGINT`, `std::vector<uint8_t>` via `bind_binary` → `SQL_C_BINARY`. SQL types are from the T-SQL header.

#### 1. ACCOUNT_LOGIN (`8_CreateStoredProc_ACCOUNT_LOGIN.sql`)
- **Params** (StoredProc.cpp:27-37):
  1. `@AccountID` in, `const char*`, varchar(21)
  2. `@Password` in, `const char*`, varchar(13)
  3. `@nRet` out, `int16_t*`, smallint OUTPUT
- **C++ use.** `retCode` is read after the proc goes out of scope; `return retCode - 1` (`DBAgent.cpp:498`).
  - Quirk: on a DB exception the function does `return false`, i.e. 0 (`DBAgent.cpp:495`), which is the same value as "logged in, no nation selected".
- **Tables.** Reads TB_USER (`strPasswd`, `strAuthority`) and ACCOUNT_CHAR (`bNation`, `bCharNum`). Writes nothing.
- **T-SQL features.**
  - Assignment `SELECT`s followed by `@@ROWCOUNT` (lines 7-8, 14-15).
  - `strAuthority != 255` (tinyint).
  - **Variable comparison `@pwd != @Password`** (line 8), done under the *database default* collation (§4.1), so passwords compare case-insensitively and ignore trailing spaces.
  - Bare `RETURN`.
  - Result codes: 0 = bad credentials or blocked; 1 = no ACCOUNT_CHAR row or `bCharNum = 0`; `Nation + 1` otherwise.

#### 2. ACCOUNT_LOGOUT (`8_CreateStoredProc_ACCOUNT_LOGOUT.sql`)
- **Params** (StoredProc.cpp:66-78):
  1. `@AccountID` in, `const char*`, varchar(21)
  2. `@LogoutCode` in, `int32_t`, int. Always 0 from C++ (default argument at `DBAgent.h:43`); unused by the proc.
  3. `@nRet` out, `int16_t*`, smallint
  4. `@nRet2` out, `int16_t*`, smallint
- **C++ use.** `result->affected_rows() == 0` means failure (`DBAgent.cpp:1144-1148`). `ret1` only drives a debug log (`:1156-1161`).
- **Tables.** DELETE from CURRENTUSER.
- **T-SQL features.** A single `DELETE … WHERE strAccountID = @AccountID`, then `SET @nRet = 1`, `SET @nRet2 = 1`. With NOCOUNT off, the DELETE's row count is what `SQLRowCount` reports.

#### 3. CLAIM_USER_RANK_STIPEND (`8_CreateStoredProc_CLAIM_USER_RANK_STIPEND.sql`)
- **Params** (StoredProc.cpp:261-274):
  1. `@nRet` out, `uint8_t*`, tinyint OUTPUT
  2. `@type` in, `uint8_t`, tinyint
  3. `@rank` in, `uint8_t`, tinyint
  4. `@nation` in, `uint8_t`, tinyint
  5. `@charId` in, `const char*`, varchar(21)
- **C++ use.** `responseCode` (initialised to 255) is read after destruction (`DBAgent.cpp:1379-1385`).
- **Tables.** Reads and updates USER_KNIGHTS_RANK (`shIndex`, `isClaimedKarus/Elmo`, `strKarus/ElmoUserID`) or USER_PERSONAL_RANK (`nRank`, same columns).
- **T-SQL features.**
  - `IF … NOT IN (0,1)` input validation.
  - `BEGIN TRAN` / `ROLLBACK` / `COMMIT`.
  - `IIF()`, which needs SQL Server 2012 or later.
  - **Variable comparison `@rankHolder != @charId`** (line 56), collation-dependent.
  - `@@ERROR` after the UPDATE.
  - **NULL logic.** If no rank row exists, `@rankHolder` is NULL, the comparison is UNKNOWN, execution falls through, the UPDATE matches 0 rows, and the proc returns 0 (success).
  - **Schema drift.** Both `isClaimed*` columns are missing from `5_CreateTable_USER_KNIGHTS_RANK.sql` and `5_CreateTable_USER_PERSONAL_RANK.sql` (§4.6).

#### 4. CREATE_KNIGHTS (`8_CreateStoredProc_CREATE_KNIGHTS.sql`, created as `[dbo].[CREATE_KNIGHTS]`)
- **Params** (StoredProc.cpp:335-349):
  1. `@nRet` out, `int16_t*`, smallint
  2. `@index` in, `int16_t`, smallint (the C++ caller passes `int knightsId`)
  3. `@nation` in, `uint8_t`, tinyint
  4. `@community` in, `uint8_t`, tinyint (C++ passes `flag`)
  5. `@strName` in, `const char*`, varchar(21)
  6. `@strChief` in, `const char*`, varchar(21)
- **C++ use.** A DB exception becomes 6 (`DBAgent.cpp:708-712`).
- **Tables.** Reads KNIGHTS (`COUNT`). Inserts into KNIGHTS (5 columns, the rest from defaults) and KNIGHTS_USER.
- **T-SQL features.**
  - `COUNT(*) … WHERE IDNum = @index or IDName = @strName`: clan-name uniqueness is case-insensitive. A UNIQUE index `IX_KNIGHTS(IDName)` also exists.
  - `BEGIN TRAN`.
  - **`@@ERROR` is checked only after the second INSERT**, so a failure of the KNIGHTS insert is masked if the KNIGHTS_USER insert succeeds.
  - `ROLLBACK` / `COMMIT`; bare `RETURN`.

#### 5. CREATE_NEW_CHAR (`8_CreateStoredProc_CREATE_NEW_CHAR.sql`)
- **Params** (StoredProc.cpp:421-445):
  1. `@nRet` out, `int16_t*`, smallint
  2. `@AccountID` in, `const char*`, varchar(21)
  3. `@index` in, `uint8_t`, tinyint
  4. `@CharID` in, `const char*`, varchar(21)
  5. `@Race` in, `uint8_t`, tinyint
  6. `@Class` in, `int16_t`, smallint
  7. `@Hair` in, `uint8_t`, tinyint
  8. `@Face` in, `uint8_t`, tinyint
  9. to 13. `@Str`, `@Sta`, `@Dex`, `@Intel`, `@Cha` in, `uint8_t`, tinyint
- **C++ use.** Returns `retCode`; a DB exception becomes -1 (`DBAgent.cpp:538-544`).
- **Tables.**
  - Reads ACCOUNT_CHAR, USERDATA (`COUNT`) and ZONE_INFO (`InitX`, `InitZ` where `ZoneNo = 21`; the seed has that row).
  - Updates ACCOUNT_CHAR (`strCharIDn`, `bCharNum + 1` where `strCharIDn IS NULL`).
  - Inserts into USERDATA (14 columns; 25 NOT NULL columns come from defaults, §4.4).
- **T-SQL features.**
  - **`IF @nRet > 0 RETURN`** relies on `@nRet` starting NULL. The C++ side binds it `PARAM_OUT`, so no value is sent.
  - NULL logic: if the account has no ACCOUNT_CHAR row, every `IF` is UNKNOWN and execution falls through to the INSERT. That INSERT then violates `Nation NOT NULL`.
  - Scalar subquery comparison `(SELECT bCharNum …) != @bCharNum + 1`.
  - tinyint arithmetic.
  - `BEGIN TRAN` / `ROLLBACK` / `COMMIT`; `@@ERROR`; bare `RETURN`.
  - Name uniqueness (`COUNT … WHERE strUserId = @CharID`) is case-insensitive.

#### 6. DELETE_KNIGHTS (`8_CreateStoredProc_DELETE_KNIGHTS.sql`, created as `[dbo].[DELETE_KNIGHTS]`)
- **Params** (StoredProc.cpp:513-522):
  1. `@nRet` out, `int16_t*`, smallint
  2. `@knightsindex` in, `int16_t`, smallint
- **C++ use.** A DB exception becomes 7 (`DBAgent.cpp:753-757`).
- **Tables.** Deletes from KNIGHTS and KNIGHTS_USER.
- **T-SQL features.** `COUNT(*)`, `BEGIN TRAN`, two DELETEs, `@@ERROR` (checks the last one only), `COMMIT`.

#### 7. LOAD_ACCOUNT_CHARID (`8_CreateStoredProc_LOAD_ACCOUNT_CHARID.sql`)
- **Params** (StoredProc.cpp:1276-1285):
  0. RETURN value, ret, `int*`
  1. `@Accountid` in, `const char*`, varchar(21)
- **Result.** `SELECT strCharID1, strCharID2, strCharID3, NULL, NULL`. C++ reads columns 0–2 as `std::string` with `is_null` guards (`DBAgent.cpp:650-657`), then `rtrim`s them (`:665-669`). No row means failure (`:644-648`).
- **T-SQL features.** `RETURN @@ROWCOUNT`. The C++ binds the return value but never reads it.

#### 8. LOAD_CHAR_INFO (`8_CreateStoredProc_LOAD_CHAR_INFO.sql`)
- **Params** (StoredProc.cpp:1314-1323):
  1. `@CharId` in, `const char*`, varchar(21). The C++ `rtrim`s it first (`DBAgent.cpp:551-553`).
  2. `@nRet` out, `int16_t*`, smallint. Never read.
- **Result columns** (`DBAgent.cpp:586-597`):
  - `Race` → `uint8_t`
  - `Class` → `int16_t`
  - `HairColor` → `uint8_t`
  - `[Level]` → `uint8_t`
  - `Face` → `uint8_t`
  - `Zone` → `uint8_t`
  - `strItem` → bytes into `ByteBuffer`. Only the first 14 slots × 8 bytes are used (`:614-626`).
- **T-SQL features.**
  - `SELECT @nRet = COUNT(strUserId)`.
  - **If the count is 0, a bare `RETURN` produces no result set.** `result->next()` then throws (invalid cursor state), the error is caught, and the function returns false.
  - The proc spells the column `strUserID`; the DDL has `strUserId`. This works because identifiers are case-insensitive.

#### 9. LOAD_KNIGHTS_MEMBERS (`8_CreateStoredProc_LOAD_KNIGHTS_MEMBERS.sql`)
- **Params** (StoredProc.cpp:1352-1360): 1. `@knightsindex` in, `int16_t`, smallint.
- **Result.** All rows of `strUserId, Fame, [Level], Class FROM USERDATA WHERE Knights = @k`, with **no ORDER BY**. C++ reads `std::string`, `uint8_t`, `uint8_t`, `int16_t` (`DBAgent.cpp:776-806`) and `rtrim`s the name.
- **T-SQL features.** A single SELECT.

#### 10. LOAD_USER_DATA (`8_CreateStoredProc_LOAD_USER_DATA.sql`)
- **Params** (StoredProc.cpp:1493-1503):
  1. `@AccountID` in, `const char*`, varchar(21)
  2. `@id` in, `const char*`, varchar(21)
  3. `@nRet` out, `int16_t*`, smallint. Set to `@@RowCount`; never read.
- **Result.** 37 columns, **read by ordinal** (`DBAgent.cpp:124-185`):

  | Ordinal | Column | C++ read as |
  |---|---|---|
  | 0 | Nation | `uint8_t` |
  | 1 | Race | `uint8_t` |
  | 2 | Class | `int16_t` |
  | 3–6 | HairColor, Rank, Title, Level | `uint8_t` |
  | 7–8 | Exp, Loyalty | `uint32_t` |
  | 9–10 | Face, City | `uint8_t` |
  | 11 | Knights | `int16_t` |
  | 12 | Fame | `uint8_t` |
  | 13–15 | Hp, Mp, Sp | `int16_t` |
  | 16–20 | Strong, Sta, Dex, Intel, Cha | `uint8_t` |
  | 21–22 | Authority, Points | `uint8_t` |
  | 23 | Gold | `uint32_t` |
  | 24 | Zone | `uint8_t` |
  | 25 | Bind | `int16_t`, guarded by `is_null` |
  | 26–29 | PX, PZ, PY, dwTime | `uint32_t` |
  | 30–32 | strSkill, strItem, strSerial | `vector<uint8_t>`, guarded by `is_null` |
  | 33 | sQuestCount | `int16_t` |
  | 34 | strQuest | `vector<uint8_t>`, guarded by `is_null` |
  | 35–36 | MannerPoint, LoyaltyMonthly | `uint32_t` |

  The `int` columns are read as `uint32_t`, so the C-cast wraps them.
- **T-SQL features.**
  - `ISNULL(strCharIDn, '')`.
  - **`IF @id NOT IN (@charid1, @charid2, @charid3) RETURN`** (line 16): a variable comparison under the default collation, case-insensitive.
  - If the account has no ACCOUNT_CHAR row, the variables stay NULL, `NOT IN` is UNKNOWN, and the proc **does not return**. It goes on to load the character.
  - An early `RETURN` yields no result set; the C++ throws and the load fails.
  - `@@RowCount`.

#### 11. LOAD_PREMIUM_SERVICE_USER (`8_CreateStoredProc_LOAD_PREMIUM_SERVICE_USER.sql`)
- **Params** (StoredProc.cpp:1384-1394):
  1. `@AccountID` in, `const char*`, varchar(21)
  2. `@type` out, `int32_t*`, int OUTPUT
  3. `@days` out, `int32_t*`, int OUTPUT
- **C++ use.** Read after destruction. `days` is narrowed to `int16_t` (`DBProcess.cpp:252`); `type` is unused.
- **Tables.** Reads `TB_USER.PremiumExpire` (datetime).
- **T-SQL features.**
  - `DateDiff(dd, GetDate(), @PremiumExpire)`: counts midnight boundaries in **server local time**.
  - `IF @days <= 0 OR @@ERROR <> 0 OR @days IS NULL` (the `@@ERROR` here refers to the preceding `SET`).

#### 12. NATION_SELECT (`8_CreateStoredProc_NATION_SELECT.sql`)
- **Params** (StoredProc.cpp:1564-1574):
  1. `@nRet` out, `int16_t*`, smallint
  2. `@AccountID` in, `const char*`, varchar(21)
  3. `@Nation` in, `uint8_t`, tinyint (C++ passes `int nation`)
- **C++ use.** `retCode != 1` means false (`DBAgent.cpp:517-521`).
- **Tables.**
  - ACCOUNT_CHAR: `COUNT`, then UPDATE `bNation` or INSERT `(strAccountID, bNation)`.
  - WAREHOUSE: `COUNT`, then INSERT `(strAccountID)`, relying on defaults.
- **T-SQL features.** `BEGIN TRAN`; `COUNT(*)`; `IF`/`ELSE`; **a single `@@ERROR` check after the last statement only**; `ROLLBACK` / `COMMIT`.

#### 13. UPDATE_KNIGHTS (`8_CreateStoredProc_UPDATE_KNIGHTS.sql`)
- **Params** (StoredProc.cpp:2090-2103):
  1. `@nRet` out, `int16_t*`, smallint
  2. `@Type` in, `uint8_t`, tinyint
  3. `@UserId` in, `const char*`, varchar(21)
  4. `@KnightsIndex` in, `int16_t`, smallint
  5. `@Domanation` in, `uint8_t`, tinyint (unused)
- **C++ use.** A DB exception becomes 2 (`DBAgent.cpp:734-738`).
- **Tables.**
  - Reads KNIGHTS and USERDATA (`COUNT`).
  - Updates KNIGHTS (`Members`, `Chief`, `ViceChief_1..3`).
  - Inserts into or deletes from KNIGHTS_USER.
  - Updates USERDATA (`Knights = -1, Fame = 0` for type 20).
- **T-SQL features.**
  - `BEGIN TRAN`.
  - **An early `COMMIT TRAN; RETURN` inside the type-26 branch.**
  - `@@ERROR` checks only the last statement of the branch; `ROLLBACK`.
  - **Variable comparisons `@ViceChief_n = @UserId`** (case-insensitive).
  - `DELETE … WHERE strUserID = @UserId` (case-insensitive).
  - `@Members tinyint` is assigned from a `smallint` column, so a value above 255 would raise an overflow error in SQL Server.

#### 14. UPDATE_USER_DATA (`8_CreateStoredProc_UPDATE_USER_DATA.sql`)
- **Params** (StoredProc.cpp:2532-2589). 38 in params, in this order:

  | # | Param | C++ type | SQL type |
  |---|---|---|---|
  | 1 | `id` | `const char*` | varchar(21) |
  | 2 | `Nation` | `uint8_t` | tinyint |
  | 3 | `Race` | `uint8_t` | tinyint |
  | 4 | `Class` | `int16_t` | smallint |
  | 5–8 | `HairColor`, `Rank`, `Title`, `Level` | `uint8_t` | tinyint |
  | 9–10 | `Exp`, `Loyalty` | `int32_t` | int |
  | 11–12 | `Face`, `City` | `uint8_t` | tinyint |
  | 13 | `Knights` | `int16_t` | smallint |
  | 14 | `Fame` | `uint8_t` | tinyint |
  | 15–17 | `Hp`, `Mp`, `Sp` | `int16_t` | smallint |
  | 18–24 | `Str`, `Sta`, `Dex`, `Intel`, `Cha`, `Authority`, `Points` | `uint8_t` | tinyint |
  | 25 | `Gold` | `int32_t` | int |
  | 26 | `Zone` | `uint8_t` | tinyint |
  | 27 | `Bind` | `int16_t` | smallint |
  | 28–31 | `PX`, `PZ`, `PY`, `dwTime` | `int32_t` | int |
  | 32 | `QuestTotal` | `int16_t` | smallint |
  | 33 | `strSkill` | `vector<uint8_t>` | **varchar(20)** (the column is varchar(10)) |
  | 34–36 | `strItem`, `strSerial`, `strQuest` | `vector<uint8_t>` | varchar(400) |
  | 37–38 | `MannerPoint`, `LoyaltyMonthly` | `int32_t` | int |

  The C++ passes `uint32_t` members (`m_iExp`, `m_iGold`, …) into the `int32_t` parameters, which reinterprets the sign (`DBAgent.cpp:450-458`).
- **C++ use.** `affected_rows() == 0` means failure (`DBAgent.cpp:466-471`).
- **Tables.** Reads USERDATA (`Knights` if `@Zone > 2`); updates USERDATA (all game columns plus `UpdateTime`).
- **T-SQL features.**
  - `IF @Zone > 2` followed by an assignment SELECT. With NOCOUNT off, that SELECT is the first statement to produce a row count, so for battle-zone saves `SQLRowCount` may reflect the SELECT rather than the UPDATE. The exact ODBC behaviour cannot be determined statically.
  - `UpdateTime = getdate()`.
  - Binary params land in varchar columns as-is: no CONVERT, no SUBSTRING.

#### 15. UPDATE_WAREHOUSE (`8_CreateStoredProc_UPDATE_WAREHOUSE.sql`)
- **Params** (StoredProc.cpp:2613-2626):
  1. `@accountid` in, `const char*`, varchar(21)
  2. `@Money` in, `int32_t`, int
  3. `@dwTime` in, `int32_t`, int
  4. `@strItem` in, `vector<uint8_t>`, varchar(1600)
  5. `@strSerial` in, `vector<uint8_t>`, varchar(1600)
- **C++ use.** `affected_rows() == 0` means failure (`DBAgent.cpp:999-1005`).
- **Tables.** A single UPDATE of WAREHOUSE. No other T-SQL features.

### 1.4 The 55 procs never called by server code

Columns: R = FROM/JOIN targets (this includes `DELETE FROM` targets), I = INSERT, U = UPDATE, D = DELETE. These come from `procscan.py` with comments stripped; treat them as indicative, not verified.

| Proc | File | Tables | Features (regex) | Notes |
|---|---|---|---|---|
| CHANGE_CASTLE_COMMERCE | 8_CreateStoredProc_CHANGE_CASTLE_COMMERCE.sql | I:DELLOS_TAX U:KNIGHTS_SIEGE_WARFARE | schema prefix | inserts into DELLOS_TAX (table not in schema) |
| CHANGE_COPY_SERIAL_ITEM | 8_CreateStoredProc_CHANGE_COPY_SERIAL_ITEM.sql |  | nested EXEC | EXECs CHANGE_COPY_SERIAL_ITEM_DATA (proc not in OpenKO-db) |
| CHANGE_COPY_SERIAL_ITEM_TABLE | 8_CreateStoredProc_CHANGE_COPY_SERIAL_ITEM_TABLE.sql |  | IDENTITY col, DDL in proc, schema prefix | ALTER TABLE ... ADD sIndex INT IDENTITY |
| CHANGE_KNIGHTS_CAPE | 8_CreateStoredProc_CHANGE_KNIGHTS_CAPE.sql | U:KNIGHTS | - |  |
| CHECK_KNIGHTS | 8_CreateStoredProc_CHECK_KNIGHTS.sql | R:KNIGHTS,KNIGHTS_USER U:USERDATA D:KNIGHTS,KNIGHTS_USER | SET NOCOUNT, BEGIN TRAN, COMMIT, ROLLBACK, @@ERROR, CURSOR, WHILE |  |
| CLEAR_REMAIN_USERS | 8_CreateStoredProc_CLEAR_REMAIN_USERS.sql | R:CURRENTUSER D:CURRENTUSER | - |  |
| CREATE_KNIGHTS2 | 8_CreateStoredProc_CREATE_KNIGHTS2.sql | R:KNIGHTS I:KNIGHTS U:USERDATA | BEGIN TRAN, COMMIT, ROLLBACK, @@ERROR, bare RETURN, OUTPUT param, COUNT(*), schema prefix |  |
| DELETE_FRIEND_LIST | 8_CreateStoredProc_DELETE_FRIEND_LIST.sql | R:FRIEND_LIST U:FRIEND_LIST | @@ROWCOUNT, @@ERROR, bare RETURN, OUTPUT param, schema prefix |  |
| EDITER_KNIGHTS | 8_CreateStoredProc_EDITER_KNIGHTS.sql | R:KNIGHTS,USERDATA U:KNIGHTS D:KNIGHTS | SET NOCOUNT, CURSOR, WHILE, COUNT(*) |  |
| EXEC_KNIGHTS_USER | 8_CreateStoredProc_EXEC_KNIGHTS_USER.sql | R:USERDATA I:KNIGHTS_USER | SET NOCOUNT, CURSOR, WHILE, schema prefix |  |
| GIVE_PREMIUM | 8_CreateStoredProc_GIVE_PREMIUM.sql | R:KN_World,TB_USER U:TB_USER | GETDATE, DATEADD, DATEDIFF | cross-DB [KN_World]..[ACCOUNT_CHAR] |
| INSERT_FRIEND_LIST | 8_CreateStoredProc_INSERT_FRIEND_LIST.sql | R:FRIEND_LIST I:FRIEND_LIST U:FRIEND_LIST | @@ROWCOUNT, @@ERROR, bare RETURN, OUTPUT param, schema prefix |  |
| INSERT_HACKTOOL_USER | 8_CreateStoredProc_INSERT_HACKTOOL_USER.sql | I:HACKTOOL_USERLOG | schema prefix |  |
| INSERT_PROGRAM_CHECK_USER | 8_CreateStoredProc_INSERT_PROGRAM_CHECK_USER.sql | I:PROGRAMLIST_LOG | - |  |
| KING_CANDIDACY_NOTICE_BOARD_PROC | 8_CreateStoredProc_KING_CANDIDACY_NOTICE_BOARD_PROC.sql | R:KING_CANDIDACY_NOTICE_BOARD I:KING_CANDIDACY_NOTICE_BOARD U:KING_CANDIDACY_NOTICE_BOARD | binary/varbinary/image, COUNT(*), schema prefix |  |
| KING_CANDIDACY_RECOMMEND | 8_CreateStoredProc_KING_CANDIDACY_RECOMMEND.sql | R:KING_ELECTION_LIST,USERDATA I:KING_ELECTION_LIST D:KING_ELECTION_LIST | @@ROWCOUNT, bare RETURN, OUTPUT param, schema prefix |  |
| KING_CHANGE_TAX | 8_CreateStoredProc_KING_CHANGE_TAX.sql | U:KING_SYSTEM | - |  |
| KING_ELECTION_PROC | 8_CreateStoredProc_KING_ELECTION_PROC.sql | R:KING_BALLOT_BOX I:KING_BALLOT_BOX U:KING_ELECTION_LIST | bare RETURN, OUTPUT param, COUNT(*) |  |
| KING_IMPEACHMENT_ELECTION | 8_CreateStoredProc_KING_IMPEACHMENT_ELECTION.sql | R:KING_BALLOT_BOX I:KING_BALLOT_BOX | bare RETURN, OUTPUT param, COUNT(*), schema prefix |  |
| KING_IMPEACHMENT_REQUEST_ELECTION | 8_CreateStoredProc_KING_IMPEACHMENT_REQUEST_ELECTION.sql | U:KING_ELECTION_LIST | schema prefix |  |
| KING_IMPEACHMENT_RESULT | 8_CreateStoredProc_KING_IMPEACHMENT_RESULT.sql | R:KING_BALLOT_BOX | OUTPUT param, COUNT(*) |  |
| KING_INSERT_PRIZE_EVENT | 8_CreateStoredProc_KING_INSERT_PRIZE_EVENT.sql | U:KING_SYSTEM | schema prefix |  |
| KING_UPDATE_ELECTION_LIST | 8_CreateStoredProc_KING_UPDATE_ELECTION_LIST.sql | R:KING_ELECTION_LIST I:KING_ELECTION_LIST U:KING_SYSTEM D:KING_ELECTION_LIST | - |  |
| KING_UPDATE_ELECTION_SCHDULE | 8_CreateStoredProc_KING_UPDATE_ELECTION_SCHDULE.sql | U:KING_SYSTEM | - |  |
| KING_UPDATE_ELECTION_STATUS | 8_CreateStoredProc_KING_UPDATE_ELECTION_STATUS.sql | R:KING_BALLOT_BOX,KING_CANDIDACY_NOTICE_BOARD,KING_ELECTION_LIST U:KING_SYSTEM D:KING_BALLOT_BOX,KING_CANDIDACY_NOTICE_BOARD,KING_ELECTION_LIST | - |  |
| KING_UPDATE_IMPEACHMENT_STATUS | 8_CreateStoredProc_KING_UPDATE_IMPEACHMENT_STATUS.sql | R:KING_BALLOT_BOX U:KING_ELECTION_LIST,KING_SYSTEM D:KING_BALLOT_BOX | schema prefix |  |
| KING_UPDATE_NOAH_OR_EXP_EVENT | 8_CreateStoredProc_KING_UPDATE_NOAH_OR_EXP_EVENT.sql | U:KING_SYSTEM | - |  |
| KNIGHTS_RATING_UPDATE | 8_CreateStoredProc_KNIGHTS_RATING_UPDATE.sql | R:KNIGHTS I:rating_temp | IDENTITY col, IF EXISTS, nested EXEC, DDL in proc, ORDER BY, schema prefix | sysobjects, sp_rename, CREATE/DROP TABLE rating_temp |
| LOAD_RENTAL_DATA | 8_CreateStoredProc_LOAD_RENTAL_DATA.sql | R:USER_RENTAL_ITEM U:USER_RENTAL_ITEM D:USER_RENTAL_ITEM | GETDATE, DATEDIFF, schema prefix |  |
| LOAD_SAVED_MAGIC | 8_CreateStoredProc_LOAD_SAVED_MAGIC.sql | R:USER_SAVED_MAGIC | bare RETURN, OUTPUT param, COUNT(*) |  |
| LOAD_WEB_ITEMMALL | 8_CreateStoredProc_LOAD_WEB_ITEMMALL.sql | R:WEB_ITEMMALL D:WEB_ITEMMALL | - |  |
| PROC_INSERT_CURRENTUSER | 8_CreateStoredProc_PROC_INSERT_CURRENTUSER.sql | I:CURRENTUSER | OUTPUT param | (Aujard does this with raw SQL instead, §2) |
| RANK_KNIGHTS | 8_CreateStoredProc_RANK_KNIGHTS.sql | R:KNIGHTS,KNIGHTS_RATING,USERDATA U:KNIGHTS | SET NOCOUNT, CURSOR, WHILE, nested EXEC, SUM/MAX/MIN/AVG | EXEC KNIGHTS_RATING_UPDATE |
| RENTAL_ITEM_CANCEL | 8_CreateStoredProc_RENTAL_ITEM_CANCEL.sql | R:RENTAL_ITEM,USER_RENTAL_ITEM D:RENTAL_ITEM,USER_RENTAL_ITEM | OUTPUT param |  |
| RENTAL_ITEM_DESTORY | 8_CreateStoredProc_RENTAL_ITEM_DESTORY.sql | R:USER_RENTAL_ITEM D:USER_RENTAL_ITEM | OUTPUT param, nested EXEC | EXEC RENTAL_ITEM_DURABILITY_UPDATE |
| RENTAL_ITEM_DURABILITY_UPDATE | 8_CreateStoredProc_RENTAL_ITEM_DURABILITY_UPDATE.sql | R:RENTAL_ITEM_LIST U:USER_RENTAL_ITEM | - |  |
| RENTAL_ITEM_LEND | 8_CreateStoredProc_RENTAL_ITEM_LEND.sql | R:RENTAL_ITEM I:RENTAL_ITEM_LIST,USER_RENTAL_ITEM U:USER_RENTAL_ITEM D:RENTAL_ITEM | bare RETURN, OUTPUT param, GETDATE |  |
| RENTAL_ITEM_REGISTER | 8_CreateStoredProc_RENTAL_ITEM_REGISTER.sql | R:RENTAL_ITEM I:RENTAL_ITEM,USER_RENTAL_ITEM | BEGIN TRAN, COMMIT, ROLLBACK, @@ERROR, bare RETURN, OUTPUT param, GETDATE, COUNT(*) |  |
| RESET_LOYALTY_MONTHLY | 8_CreateStoredProc_RESET_LOYALTY_MONTHLY.sql | U:USERDATA | - |  |
| SKILLSHORTCUT_LOAD | 8_CreateStoredProc_SKILLSHORTCUT_LOAD.sql | R:USERDATA_SKILLSHORTCUT | - | dbo.USERDATA_SKILLSHORTCUT |
| SKILLSHORTCUT_SAVE | 8_CreateStoredProc_SKILLSHORTCUT_SAVE.sql | R:USERDATA_SKILLSHORTCUT I:USERDATA_SKILLSHORTCUT U:USERDATA_SKILLSHORTCUT | @@ROWCOUNT |  |
| UPDATE_BATTLE_HERO | 8_CreateStoredProc_UPDATE_BATTLE_HERO.sql | R:HERO_USER I:HERO_USER U:HERO_USER | COUNT(*) |  |
| UPDATE_BATTLE_RESULT | 8_CreateStoredProc_UPDATE_BATTLE_RESULT.sql | R:BATTLE U:BATTLE | - | (Aujard does the BATTLE update with raw SQL instead, §2) |
| UPDATE_EDITOR_ITEM_DATA | 8_CreateStoredProc_UPDATE_EDITOR_ITEM_DATA.sql | I:USER_EDITOR_ITEM | bare RETURN, GETDATE |  |
| UPDATE_KNIGHTS_ALLIANCE | 8_CreateStoredProc_UPDATE_KNIGHTS_ALLIANCE.sql | R:KNIGHTS_ALLIANCE I:KNIGHTS_ALLIANCE U:KNIGHTS,KNIGHTS_ALLIANCE D:KNIGHTS_ALLIANCE | - |  |
| UPDATE_KNIGHTS_MARK | 8_CreateStoredProc_UPDATE_KNIGHTS_MARK.sql | U:KNIGHTS | BEGIN TRAN, COMMIT, bare RETURN, OUTPUT param, binary/varbinary/image |  |
| UPDATE_KNIGHTS_WAR | 8_CreateStoredProc_UPDATE_KNIGHTS_WAR.sql | R:KNIGHTS U:KNIGHTS | - |  |
| UPDATE_PERSONAL_RANK | 8_CreateStoredProc_UPDATE_PERSONAL_RANK.sql | R:EPERSONAL_RANK,USERDATA,USER_PERSONAL_RANK I:EPERSONAL_RANK,KPERSONAL_RANK,USER_PERSONAL_RANK U:EPERSONAL_RANK,KPERSONAL_RANK D:USER_PERSONAL_RANK | IDENTITY col, TOP, IF EXISTS, IIF/CASE, UPDATE...FROM, DDL in proc, ORDER BY, schema prefix | sysobjects, CREATE/DROP KPERSONAL_RANK/EPERSONAL_RANK |
| UPDATE_PREMIUM_SERVICE_USER | 8_CreateStoredProc_UPDATE_PREMIUM_SERVICE_USER.sql |  | RETURN value | wrapper uses `{? = CALL …}` |
| UPDATE_SAVED_MAGIC | 8_CreateStoredProc_UPDATE_SAVED_MAGIC.sql | R:USER_SAVED_MAGIC I:USER_SAVED_MAGIC U:USER_SAVED_MAGIC | bare RETURN, COUNT(*) |  |
| UPDATE_SIEGE | 8_CreateStoredProc_UPDATE_SIEGE.sql | U:KNIGHTS_SIEGE_WARFARE | - |  |
| UPDATE_SIEGE_CHALLENGER | 8_CreateStoredProc_UPDATE_SIEGE_CHALLENGER.sql | U:KNIGHTS_SIEGE_WARFARE | - |  |
| UPDATE_SIEGE_CHALLENGER2 | 8_CreateStoredProc_UPDATE_SIEGE_CHALLENGER2.sql | U:KNIGHTS_SIEGE_WARFARE | - |  |
| UPDATE_SIEGE_DECIDE_CHALLENGER | 8_CreateStoredProc_UPDATE_SIEGE_DECIDE_CHALLENGER.sql | U:KNIGHTS_SIEGE_WARFARE | - |  |
| USER_KNIGHTS_RATING_UPDATE | 8_CreateStoredProc_USER_KNIGHTS_RATING_UPDATE.sql | R:EUSER_RATING,KNIGHTS,KUSER_RATING,USERDATA I:EUSER_RATING,KUSER_RATING U:USER_KNIGHTS_RANK | SET NOCOUNT, IDENTITY col, CURSOR, WHILE, TOP, IF EXISTS, DDL in proc, ORDER BY, schema prefix | sysobjects, CREATE/DROP KUSER_RATING/EUSER_RATING |

The cursor-, DDL- and `sysobjects`-based ranking procs are the hardest T-SQL in the repo. The servers never call them, so a SQLite port can skip them unless an external job runs them; no such job is referenced from the server source.

---

## 2. Non-procedure queries

### 2.1 Exact SQL text emitted by `SqlBuilder<M>` (`db-library/SqlBuilder.h`)

**`SelectString()`** (`:36-111`)
- Builds `"SELECT "`, then `"TOP <Limit> "` if `Limit > 0` (`:40-41`).
- Then the selected columns, each wrapped as `[col]` and joined with `", "`.
  - The column set is `selectCols`, a **`std::unordered_set<std::string>`** (`:248`). Column order in the emitted text is therefore STL-implementation hash order and **cannot be determined statically**. It differs between libc++, libstdc++ and MSVC.
  - Columns listed in `M::BlobColumns()` are moved to the end (`:51-82`).
- Then `" FROM [" + M::TableName() + "]"` (`:84`).
- If `Where` is non-empty: `" WHERE " + Where` (`:86-90`). Otherwise, if `IsWherePK`: `" WHERE [pk1] = ? AND [pk2] = ?…"` (`:91-102`).
- Then `" " + PostWhereClause` if set (`:104-107`).
- No schema prefix is ever added. `SetSelectColumns` silently drops (with a warning) names that are not in `M::ColumnNames()` (`:217-227`).

**`SelectCountString()`** (`:114-139`)
- Builds `"SELECT COUNT(*) FROM [T]"`. When `Where` is set it appends `" " + Where` **without** the `WHERE` keyword (`:117-120`). That is a latent bug: it is never reached today, because no caller combines `Where` with `fetchRowCount=true`.
- It also appends `PostWhereClause`, so an `ORDER BY` would be added to a `COUNT(*)`, which is invalid in SQL Server.
- Used only when `fetchRowCount=true` (`ModelRecordSet.h:128-131, 153-167`).

**`InsertString()`** (`:143-161`)
- `"INSERT INTO [T] ([c1], [c2], …) VALUES (?, ?, …)"`, in `M::OrderedColumnNames()` order, which is deterministic.

**`DeleteByIdString()`** (`:187-208`)
- `"DELETE FROM [T] WHERE [pk1] = ? AND …"`; empty if `T` has no PK.

**`UpdateString()`** is commented out (`:163-185`).

**Syntax check against SQLite.**
- SQLite accepts `[ident]` quoting, `?` placeholders, `<>`, `ORDER BY … DESC`.
- **`TOP n` is not SQLite syntax**, but no server sets `Limit`.
- Everything the servers actually emit therefore parses on SQLite unchanged. The divergences are semantic: collation, ordering, typing (§4–5).

### 2.2 Every non-proc query the servers issue

| # | Site | Kind | Exact SQL (columns in `{}` are an unordered set) | Params / C++ handling |
|---|---|---|---|---|
| Q1 | `Aujard/DBAgent.cpp:827-855` (UpdateConCurrentUserCount; timer every 5 min, `AujardApp.cpp:44-45, 679`) | raw, `fmt::format` | `UPDATE CONCURRENT SET [zone{zoneId}_count] = ? WHERE [serverid] = ?` | `zoneId` (from Aujard.ini `ZONE_INFO`, `AujardApp.cpp:139`) is **interpolated into a column name**, so only 1–3 are valid. Binds `int userCount`, `int serverId`. Uses **DbType::ACCOUNT**. No affected-rows check. |
| Q2 | `Aujard/DBAgent.cpp:857-950` (LoadWarehouseData; `AujardApp.cpp:282`) | SqlBuilder `aujard_model::Warehouse` + `SetSelectColumns` + `IsWherePK` | `SELECT [nMoney], {[WarehouseData], [strSerial]} FROM [WAREHOUSE] WHERE [strAccountID] = ?` (blob columns last) | `bind(0, accountId)`. Blobs are read as `optional<vector<uint8_t>>` (`AujardBinder.cpp:192,197`) and then `std::ranges::copy` into `char[1600]` (`:859, 898-902`). |
| Q3 | `Aujard/DBAgent.cpp:1016-1069` (LoadKnightsInfo; `AujardApp.cpp:1009`) | SqlBuilder `aujard_model::Knights` + `SetSelectColumns` + `IsWherePK` | `SELECT {[IDNum],[Nation],[IDName],[Members],[Points]} FROM [KNIGHTS] WHERE [IDNum] = ?` | `bind(0, &knightsId)` (int). The name is `rtrim`med and checked against `MAX_ID_SIZE` (`:1045-1054`). |
| Q4 | `Aujard/DBAgent.cpp:1077-1081` (SetLogInInfo init=1; `AujardApp.cpp:1041`) | raw, `fmt::format` (**unescaped literals**) | `INSERT INTO [CURRENTUSER] ([strAccountID], [strCharID], [nServerNo], [strServerIP], [strClientIP]) VALUES ('{acc}','{char}',{srv},'{ip}','{cip}')` | Values come from SMQ packet fields (`AujardApp.cpp:1027-1039`). `affected_rows()==0` means false (`:1109-1113`). A duplicate PK raises an error and returns false. |
| Q5 | `Aujard/DBAgent.cpp:1083-1088` (SetLogInInfo init=2) | raw, `fmt::format` | `UPDATE [CURRENTUSER] SET [nServerNo]={srv}, [strServerIP]='{ip}' WHERE [strAccountID] = '{acc}'` | Same as Q4; the WHERE compare is case-insensitive under SQL Server. |
| Q6 | `Aujard/DBAgent.cpp:1173-1177` (CheckUserData type 1; `AujardApp.cpp:437, 464`) | raw, `fmt::format` | `SELECT [dwTime], [nMoney] FROM [WAREHOUSE] WHERE [strAccountID] = '{acc}'` | Reads `get<uint32_t>(0)`, `(1)` (`:1204-1205`) and compares them to the in-memory values. This is the read-back check after a save. |
| Q7 | `Aujard/DBAgent.cpp:1179-1183` (CheckUserData type 2; `AujardApp.cpp:439, 473`) | raw, `fmt::format` | `SELECT [dwTime], [Exp] FROM [USERDATA] WHERE [strUserID] = '{charId}'` | The column is spelled `strUserID` while the DDL has `strUserId`; identifiers are case-insensitive. |
| Q8 | `Aujard/DBAgent.cpp:1224-1316` (LoadKnightsAllList; `AujardApp.cpp:771`) | SqlBuilder `aujard_model::Knights` with `Where` + `PostWhereClause` | `SELECT {[IDNum],[Nation],[Ranking],[IDName],[Members],[Points]} FROM [KNIGHTS] WHERE [Nation] = {n} AND [Points] <> 0 ORDER BY [Points] DESC`. When nation==3: `… WHERE [Points] <> 0 ORDER BY [Points] DESC`. | `n` is an int, interpolated. **The order of ties is unspecified.** |
| Q9 | `Aujard/DBAgent.cpp:1318-1352` (UpdateBattleEvent; `AujardApp.cpp:1087`) | raw, bound params | `UPDATE BATTLE SET byNation = ?, strUserName = ? WHERE sIndex = 1` (identifiers unbracketed) | Binds `int nation` into a tinyint and `charId`. `affected_rows()==0` means false. |
| Q10 | `VersionManager/DBProcess.cpp:63-110` (AccountLogin; `User.cpp:188`) | SqlBuilder `versionmanager_model::TbUser` + `IsWherePK` | `SELECT {[strAccountID],[strPasswd],[strAuthority]} FROM [TB_USER] WHERE [strAccountID] = ?` | **The password is compared in C++ with `user.Password != password`** (case-sensitive and exact; `:91`). ACCOUNT_LOGIN, used on the Aujard path, compares case-insensitively (§1.3 #1). |
| Q11 | `VersionManager/DBProcess.cpp:112-143` (InsertVersion) | SqlBuilder `InsertString` | `INSERT INTO [VERSION] ([sVersion], [strFileName], [strCompressName], [sHistoryVersion]) VALUES (?, ?, ?, ?)` | **Dead code**: no callers (only `DBProcess.h:39`). |
| Q12 | `VersionManager/DBProcess.cpp:145-171` (DeleteVersion) | SqlBuilder `DeleteByIdString` | `DELETE FROM [VERSION] WHERE [sVersion] = ?` | **Dead code**: no callers. |
| Q13 | `VersionManager/DBProcess.cpp:173-202` (LoadUserCountList; `User.cpp:123` on every LS_SERVERLIST) | ModelRecordSet `open()` | `SELECT {[serverid],[zone1_count],[zone2_count],[zone3_count]} FROM [CONCURRENT]` | All rows. Model DbType GAME, whereas Q1 writes the same table through ACCOUNT. |
| Q14 | `VersionManager/DBProcess.cpp:204-235` (IsCurrentUser; `User.cpp:198`) | SqlBuilder `IsWherePK` | `SELECT {[nServerNo],[strServerIP],[strAccountID]} FROM [CURRENTUSER] WHERE [strAccountID] = ?` | `strServerIP` is sent to the client. |
| Q15 | `shared-server/TelnetClientThread.cpp:178-235` (Authenticate; optional telnet in every AppThread server) | SqlBuilder `full_model::TbUser` + `IsWherePK` | `SELECT {[strAccountID],[strPasswd],[strSocNo],[strEmail],[strAuthority],[PremiumExpire]} FROM [TB_USER] WHERE [strAccountID] = ?` | C++ password compare plus a check for `AUTHORITY_MANAGER`. `PremiumExpire` is converted by `CTimeFromDbTime` (§4.4). ItemManager configures no DSN and never calls `ConnectionManager::Create()`, so telnet auth there cannot work. |
| Q16 | `Ebenezer/EbenezerApp.cpp:3205-3358` (LoadKnightsRankTable; runtime only, `OperationMessage.cpp:546`) | RecordSetLoader `Base<KnightsRating>` | `SELECT {[nRank],[shIndex],[strName],[nPoints]} FROM [KNIGHTS_RATING]` | **No ORDER BY.** The first 5 knights per nation encountered become captains (`:3231-3304`), so row order decides the outcome. |

On top of these, every startup loader (§3) emits `SELECT {all model columns, blobs last} FROM [T]`. For `K_MONSTER_ITEM`, `K_NPCPOS` and `LEVEL_UP` the loader first emits `SELECT COUNT(*) FROM [T]` (`fetchRowCount=true`: `AIServerApp.cpp:477, 641`; `EbenezerApp.cpp:1130`). `Model::BatchSelect` (`Model.h:32-84`) is never used.

**Other checks.**
- No `knight.` or `dbo.` prefix appears in any server SQL.
- `[bracket]` quoting is used everywhere except Q1's table name and all of Q9.
- Nothing above fails to parse on SQLite.
- Q4–Q7 build string literals from packet data without escaping. A `'` in an ID would break the statement on either engine. A port should switch these to bound parameters.

**Count:** 16 distinct non-proc query sites, of which 14 are live and 2 are dead code (Q11, Q12). Startup template queries are counted in §3.

---

## 3. Startup loads

**Row-count method.** Each `6_InsertData_<T>.sql` file is a single `INSERT INTO [T] (…) VALUES` header line followed by one tuple per line. I counted rows two ways:
1. A character-level tokenizer that counts depth-0 `(` after `VALUES`, honouring `'…''…'` string quoting (`parse_models.py: count_seed_rows`).
2. Physical line count minus 1.

The two agree for all 60 seed files. A table with no `6_` file counts as 0 rows.

Failure fatality: "Fatal" means the loader returns false, `OnStart()` returns false, and the process exits.

### AIServer (`Server/AIServer/AIServerApp.cpp`, `OnStart` :150-339; DSN GAME only, `:1577-1582`)

| Table | Model (bound type) | Loader / function, line (call site) | Empty? | Fatal | Seed rows |
|---|---|---|---|---|---|
| MAGIC | `aiserver_model::Magic` | `STLMap`, `GetMagicTableData` :382-394 (:205) | Forbid | yes | 1522 |
| MAGIC_TYPE1 | `MagicType1` | `STLMap`, `GetMagicType1Data` :1337-1349 (:211) | Forbid | yes | 170 |
| MAGIC_TYPE2 | `MagicType2` | `STLMap` :1351-1363 (:217) | Forbid | yes | 60 |
| MAGIC_TYPE3 | `MagicType3` | `STLMap` :1365-1377 (:223) | Forbid | yes | 618 |
| MAGIC_TYPE4 | `MagicType4` | `STLMap` :1379-1391 (:229) | Forbid | yes | 465 |
| MAGIC_TYPE7 | `MagicType7` | `STLMap` :1393-1405 (:235) | Forbid | yes | 17 |
| K_MONSTER_ITEM | `MonsterItem` | `Vector` + `COUNT(*)`, `GetNpcItemTable` :470-516 (:244). `m_nField = loader.GetColumnCount()` (:483) | Forbid | yes | 666 |
| MAKE_WEAPON | `MakeWeapon` | `STLMap` :396-408 (:250) | Forbid | yes | 20 |
| MAKE_DEFENSIVE | rows of `MakeDefensive` bound into `MakeWeapon` | `STLMap<MakeWeaponTableMap, MakeDefensive>` :410-423 (:256). All 8 selected columns exist in the MakeWeapon binder (checked). | Forbid | yes | 20 |
| MAKE_ITEM_GRADECODE | `MakeItemGradeCode` | `STLMap` :425-437 (:262) | Forbid | yes | 9 |
| MAKE_ITEM_LARECODE | `MakeItemRareCode` | `STLMap` :439-451 (:268) | Forbid | yes | 25 |
| MAKE_ITEM_GROUP | `MakeItemGroup` | `STLMap` :453-465 (:274) | Forbid | yes | 46 |
| ZONE_INFO | `ZoneInfo` | `STLMap`, `GetZoneInfoTableData` :518-530 (:280) | Forbid | yes | 20 |
| K_MONSTER | rows of `Monster` bound into `Npc` | `STLMap<NpcTableMap, Monster>` :533-556 (:291). All 46 columns match the Npc binder (checked). `strName` is `rtrim`med (:544-550). | Forbid | yes | 723 |
| K_NPC | `Npc` | `STLMap` :559-581 (:298); `rtrim` (:569-575) | Forbid | yes | 474 |
| ZONE_INFO (2nd load) | `ZoneInfo` | `Base` callback, `MapFileLoad` :943-1018 (:307). Opens `_mapDir / row.Name` (:960; **not trimmed**). Pushes `MAP*` into `_zones` **in row order** (:1002). | Forbid | yes | 20 |
| K_NPCPOS | `NpcPos` (blob column `path`, text) | `Vector` + `COUNT(*)`, `LoadNpcPosTable` :636-893 (via `CreateNpcThread` :313/:590). **NPC serials are assigned in row order** (`nSerial++` :665). `path` is parsed as `%04d%04d` pairs (:777-804). | Forbid | yes | 1730 |

That is 16 distinct tables and 17 loads. Models present in the AIServer library but never loaded: `MakeItem`, `MonsterSummonList`, `NpcMoveItem`, `LevelUp` (0 references).

### Ebenezer (`Server/Ebenezer/EbenezerApp.cpp`, `OnStart` :247-518; DSN GAME only, `:1228-1233`)

| Table | Model | Loader / function, line (call site) | Empty? | Fatal | Seed rows |
|---|---|---|---|---|---|
| ITEM | `ebenezer_model::Item` (57 columns) | `STLMap`, `LoadItemTable` :991-1001 (:317) | Forbid | yes | 61530 |
| ITEM_UPGRADE | `ItemUpgrade` | `Vector` :1003-1016 (:324). Scanned linearly, first match wins (`User.cpp:13662`). | Forbid | yes | 5013 |
| MAGIC | `Magic` | `STLMap` :1018-1028 (:331) | Forbid | yes | 1522 |
| MAGIC_TYPE1 | `MagicType1` | `STLMap` :1030-1040 (:338) | Forbid | yes | 170 |
| MAGIC_TYPE2 | `MagicType2` | `STLMap` :1042-1052 (:345) | Forbid | yes | 60 |
| MAGIC_TYPE3 | `MagicType3` | `STLMap` :1054-1064 (:352) | Forbid | yes | 618 |
| MAGIC_TYPE4 | `MagicType4` | `STLMap` :1066-1076 (:359) | Forbid | yes | 465 |
| MAGIC_TYPE5 | `MagicType5` | `STLMap` :1078-1088 (:366) | Forbid | yes | 38 |
| MAGIC_TYPE7 | `MagicType7` | `STLMap` :1090-1100 (:373) | Forbid | yes | 17 |
| MAGIC_TYPE8 | `MagicType8` | `STLMap` :1102-1112 (:380) | Forbid | yes | 30 |
| COEFFICIENT | `Coefficient` (float → `double`) | `STLMap` :1114-1125 (:387) | Forbid | yes | 24 |
| LEVEL_UP | `LevelUp` | `Vector` + `COUNT(*)` :1127-1137 (:394). **Indexed positionally as `m_LevelUpTableArray[level-1]`** (`User.cpp:2875, 3797, 3851, 14499`). | Forbid | yes | 90 |
| KNIGHTS | `Knights` (24 columns; `Mark` image blob) | `Base` callback, `LoadAllKnights` :2689-2806 (:401). Names `rtrim`med (:2709-2766). | **Allow** | yes (DB error) | 201 |
| KNIGHTS_USER | `KnightsUser` | `Base`, `LoadAllKnightsUserData` :2808-2837 (:408); `rtrim` (:2821-2823) | Allow | yes (DB error) | 201 |
| KNIGHTS_SIEGE_WARFARE | `KnightsSiegeWarfare` (38 columns, includes `strChallengeList` char(50) as bytes) | `Base` :2839-2865 (:415). **The last row wins** (:2851-2852). | Allow | yes (DB error) | **0** |
| USER_KNIGHTS_RANK | `UserKnightsRank` (selects `isClaimedElmo/Karus`) | `STLMap` :2867-2878 (:422) | Forbid | yes | 100 (but see §4.6) |
| USER_PERSONAL_RANK | `UserPersonalRank` (same) | `STLMap` :2880-2891 (:429) | Forbid | yes | 100 (but see §4.6) |
| HOME | `Home` | `STLMap` :2665-2675 (:436) | Forbid | yes | 2 |
| START_POSITION | `StartPosition` | `STLMap` :2630-2641 (:443) | Forbid | yes | 19 |
| BATTLE | `Battle` | `Base` :3160-3185 (:450). **Last row's `byNation` wins** (:3173). | Forbid | yes | 1 |
| SERVER_RESOURCE | `ServerResource` | `STLMap` :2643-2663 (:457); `rtrim` (:2656-2659) | Forbid | yes | 135 |
| EVENT_TRIGGER | `EventTrigger` | `Base` :3429-3466 (:464). A duplicate key only logs an error. | Allow | yes (DB error) | 100 |
| ITEM_EXCHANGE | `ItemExchange` | `STLMap` :2677-2687 (:471) | Forbid | yes | 431 |
| ZONE_INFO | `ZoneInfo` | `Base` callback, `MapFileLoad` :915-989 (:478). Opens `_mapDir / row.Name` (:932, **not trimmed**). Appends to `m_ZoneArray` **in row order** (:964). | Forbid | yes | 20 |
| EVENT | `Event` (no PK) | `Base`, `C3DMap::LoadEvent` `Map.cpp:511-555`, called from `C3DMap::LoadMap` `Map.cpp:106` once per ZONE_INFO row (EbenezerApp.cpp:955). **It reads the whole table each time** and filters in C++ (`Map.cpp:524`). | Forbid | yes (fails that map, then the whole MapFileLoad) | 28 |

That is 25 distinct tables. Models in the Ebenezer library that are never used: CouponSerialList, KnightsAlliance, KnightsCape, MagicType6, MagicType9, MonsterChallenge, MonsterChallengeSummonList, RentalItem, WebpageAddress (0 references each). KnightsRating is runtime only (Q16).

### Aujard (`Server/Aujard/AujardApp.cpp`)
- Connection probes for GAME and ACCOUNT: `DBAgent.cpp:34-67`, called at `AujardApp.cpp:146`. Fatal.
- ITEM: `aujard_model::Item` (Num, Countable only); `STLMap`, `LoadItemTable` :230-240 (:149); Forbid; fatal; 61530 rows.

### VersionManager
- Connection probe for ACCOUNT: `DBProcess.cpp:33-49`, called at `VersionManagerApp.cpp:71`. Fatal.
- VERSION: `versionmanager_model::Version`; `STLMap`, `CDBProcess::LoadVersionList` `DBProcess.cpp:51-61`, via `VersionManagerApp.cpp:209-230` (:77); Forbid; fatal; 1 row.

### ItemManager
No DB access at all: no `ConnectionManager::Create`, no DSN.

**Totals.** 44 loader call sites (AIServer 17, Ebenezer 25, Aujard 1, VersionManager 1) covering **35 distinct tables**. EVENT runs once per zone.

**Tables loaded at startup with zero seed rows:**
- `KNIGHTS_SIEGE_WARFARE` has no `6_` file. It is loaded with `AllowEmpty`, so this is not fatal, but Ebenezer then keeps its default siege state.
- Every `ForbidEmpty` table has at least 1 seed row.
- **However**, `USER_KNIGHTS_RANK` and `USER_PERSONAL_RANK` effectively end up with 0 rows if built from the `5_` DDL. Their seed INSERTs name two columns the DDL lacks, and Ebenezer's SELECT names them too, so Ebenezer cannot start (§4.6).

---

## 4. T-SQL behaviours the servers rely on

### 4.1 Collation and case-insensitive comparison

**Declared collation.**
- All 173 char/varchar/text columns carry an explicit `COLLATE SQL_Latin1_General_CP1_CI_AS`: 160 varchar, 12 char, 1 text (grep of `5_CreateTable_*.sql`). jsonSchema mirrors this with `collationName`/`characterSet: iso_1`.
- **The database collation is not declared.** `1_CreateDatabase_KN_online.sql` is just `CREATE DATABASE [KN_online]`, so it inherits the server default, which cannot be determined statically. That default governs **variable-to-variable comparisons** inside procs and the varchar parameters themselves.
- `CI_AS` means case-insensitive, accent-sensitive, code page 1252. SQL Server's `=` also **ignores trailing spaces** for varchar/char under every collation (ANSI padding semantics).

**Places that depend on case- and trailing-space-insensitive comparison:**

| Where | Comparison |
|---|---|
| ACCOUNT_LOGIN lines 7, 14 | `TB_USER.strAccountID = @AccountID`, `ACCOUNT_CHAR.strAccountID = @AccountID`. **Line 8: `@pwd != @Password`** (variables, so the password check is case-insensitive). |
| LOAD_USER_DATA lines 15, 16, 21 | `ACCOUNT_CHAR.strAccountID`, **`@id NOT IN (@charid1..3)`** (variables), `USERDATA.strUserId = @id` |
| NATION_SELECT lines 26-34 | `ACCOUNT_CHAR.strAccountID`, `WAREHOUSE.strAccountID` |
| CREATE_NEW_CHAR lines 30, 39, 53-59 | `ACCOUNT_CHAR.strAccountID`; **name uniqueness `USERDATA.strUserId = @CharID`** |
| LOAD_CHAR_INFO lines 12, 16 | `USERDATA.strUserId/strUserID = @CharId` |
| LOAD_ACCOUNT_CHARID line 12 | `ACCOUNT_CHAR.strAccountID` |
| CREATE_KNIGHTS line 19 | **clan-name uniqueness `KNIGHTS.IDName = @strName`** |
| UPDATE_KNIGHTS lines 51, 72-87 | `USERDATA.strUserId`, `KNIGHTS_USER.strUserID = @UserId`, **`@ViceChief_n = @UserId`** (variables) |
| UPDATE_USER_DATA lines 54, 102 | `USERDATA.strUserId = @id`. `@id` is the **client-supplied** character name (`AujardApp.cpp:255, 281` → `DBAgent.cpp:193-198` copies it into `m_id`). |
| UPDATE_WAREHOUSE line 18 | `WAREHOUSE.strAccountID = @accountid`. The account ID is **typed by the user** (`AujardApp.cpp:253, 295`). |
| ACCOUNT_LOGOUT line 7 | `CURRENTUSER.strAccountID` |
| CLAIM_USER_RANK_STIPEND line 56 | **`@rankHolder != @charId`** (variables) |
| LOAD_PREMIUM_SERVICE_USER line 9 | `TB_USER.strAccountID` |
| Q2, Q10, Q14, Q15 (`IsWherePK`) | `WAREHOUSE`/`TB_USER`/`CURRENTUSER.strAccountID = ?` |
| Q5, Q6, Q7 | `CURRENTUSER`/`WAREHOUSE.strAccountID = '…'`, `USERDATA.strUserID = '…'` |
| Primary keys and unique indexes (`5_` DDL) | CI uniqueness on `USERDATA(strUserId)`, `ACCOUNT_CHAR(strAccountID)`, `TB_USER(strAccountID)`, `WAREHOUSE(strAccountID)`, `CURRENTUSER(strAccountID)`, `KNIGHTS_USER(sIDNum, strUserID)`, `UNIQUE IX_KNIGHTS(IDName)` |

**The C++ side assumes the same semantics.**
- `strnicmp` (which maps to `strncasecmp`/`_strnicmp`, `shared/StringUtils.h:11-16`): `Aujard/AujardApp.cpp:716`, `Aujard/DBAgent.cpp:395, 962`, `Ebenezer/EbenezerApp.cpp:535, 544`, `Ebenezer/KnightsManager.cpp:192, 508`, `Ebenezer/User.cpp:1071, 7364`.
- `stricmp`: `AIServer/Npc.cpp:4486-4498, 5654`.
- Exceptions: the password checks in VersionManager (`DBProcess.cpp:91`) and telnet (`TelnetClientThread.cpp:216`) are **case-sensitive**, which contradicts ACCOUNT_LOGIN.
- `strncasecmp` in the C locale folds ASCII only. The SQL collation also folds CP1252 letters. This only matters for non-ASCII names.

### 4.2 CHAR(n)/NCHAR padding

**Fixed-width columns** (all 12 are `char`; there are no nchar/nvarchar columns anywhere):
- `COPY_SERIAL_ITEM.strUserId` char(21)
- `KNIGHTS_SIEGE_WARFARE.strChallengeList` char(50)
- `USER_EDITOR`: `strOld/NewUserValue` char(600), `strOld/NewUserSkill` char(10), `strOld/NewUserItem` char(400), `strOld/NewWHValue` char(100), `strOld/NewWHItem` char(1600)

**Server reads of char columns.** Only `KNIGHTS_SIEGE_WARFARE.strChallengeList`, through the Ebenezer model as `optional<vector<uint8_t>>` (`EbenezerBinder.cpp:1386`). The value is unused (`EbenezerApp.cpp:2851-2852`). **No server logic depends on CHAR padding of the current schema.** Every name column is varchar.

**Trimming.**
- `DB_COMPAT_PADDED_NAMES` is defined by default (`shared-server/server_config.h.default:6`; copied to `server_config.h` at build). It `rtrim`s on read:
  - Aujard: `DBAgent.cpp:194-196, 551-553, 665-669, 785-787, 1045-1047`
  - AIServer: `AIServerApp.cpp:544-550, 569-575`
  - Ebenezer: `EbenezerApp.cpp:2656-2659, 2709-2766, 2821-2823, 3227-3229`
- **Not trimmed:**
  - `ZONE_INFO.strZoneName`, which is used directly as a filename (`AIServerApp.cpp:960`, `EbenezerApp.cpp:932`)
  - `TB_USER.strPasswd` (C++ `!=` compares)
  - `CURRENTUSER.strServerIP`
  - `ITEM.strName`
  - `EVENT.Exec1..5` (consumed via `atoi`, which tolerates padding)

**Padded data in the seed.** Trailing spaces in varchar values appear only in `K_MONSTER.strName` (4 rows, e.g. `'Succubus '`) and `K_NPC.strName` (2 rows). AIServer trims both. No IDs or names used as keys have trailing spaces.

### 4.3 Binary data stored in varchar/char/image columns

| Column (declared type) | Layout (little-endian) | Written by | Read by | Seed data |
|---|---|---|---|---|
| `USERDATA.strSkill` varchar(10), NULL, `DEFAULT 0x00` | 9 bytes = `m_bstrSkill[9]` (`shared-server/_USER_DATA.h:70`) | UPDATE_USER_DATA `@strSkill varchar(20)` (`DBAgent.cpp:406, 457`) | LOAD_USER_DATA column 30 (`DBAgent.cpp:158-162, 243-244`) | `CONVERT(varchar(10),0x…)` ×208; 8 rows contain 0x00 |
| `USERDATA.strItem` varchar(400) | 42 × {int32 id, int16 dur, int16 count} = 336 B | UPDATE_USER_DATA (`:429-439`) | LOAD_USER_DATA column 31 (`:164-168, 247-252`); LOAD_CHAR_INFO column 6 (`:593-597, 614-626`) | ×208, all 208 contain 0x00, max 400 B |
| `USERDATA.strSerial` varchar(400) | 42 × int64 = 336 B | UPDATE_USER_DATA (`:441`) | LOAD_USER_DATA column 32 (`:170-174, 253`) | ×208, all contain 0x00 |
| `USERDATA.strQuest` varchar(400) | 100 × {int16 id, uint8 state} = 300 B | UPDATE_USER_DATA (`:408-423`) | LOAD_USER_DATA column 34 (`:178-182, 305-309`) | ×208, 11 contain 0x00 |
| `WAREHOUSE.WarehouseData` varchar(1600) | 192 × 8 = 1536 B | UPDATE_WAREHOUSE (`:973-991`) | Q2 (`AujardBinder.cpp:192`; copied into `char[1600]` at `DBAgent.cpp:899`; parsed at `:913-915`) | `CONVERT(varchar(1600),0x…)` ×204, all contain 0x00, max 1536 B |
| `WAREHOUSE.strSerial` varchar(1600) | 192 × int64 = 1536 B | UPDATE_WAREHOUSE | Q2 (`AujardBinder.cpp:197`; `DBAgent.cpp:902, 917`) | ×204, all contain 0x00 |
| `KNIGHTS.Mark` image | clan mark | UPDATE_KNIGHTS_MARK (not called) | Ebenezer KNIGHTS load (`EbenezerBinder.cpp:1114`) | — |
| `KNIGHTS.Stash` varchar(1600) | — | — | not in any server model | `CONVERT(varchar(1600), NULL)` ×201 |
| `USERDATA_SKILLSHORTCUT.strSkillData` varchar(260) `DEFAULT 0x00` | — | SKILLSHORTCUT_SAVE (not called) | — | ×11 with 0x00 |
| `KING_CANDIDACY_NOTICE_BOARD.strNotice` varbinary(1024) | — | the proc (not called) | — | 1 |
| `COPY_SERIAL_ITEM.ItemNum` binary(4), `.ItemSerial` binary(8) | — | — | — | — |

**How nanodbc moves these bytes.**
- **Write.** `bind_binary` binds `SQL_C_BINARY` using the SQL type and size reported by `SQLDescribeParam` (`SQL_VARCHAR(n)` from the proc header). The length indicator is `value_size`, so the data is **length-delimited, not NUL-terminated** (`nanodbc.cpp:2443-2478`). If `value_size` exceeds the described size, nanodbc switches to `SQL_SS_LENGTH_UNLIMITED` (`:2276-2287`). ODBC's binary-to-character conversion sends the bytes unchanged.
- **Code pages.** `AutoTranslate=no` stops the SQL Server driver from re-encoding char data (`db-library/ConnectionManager.cpp:71-72`).
- **Read.** `get_ref<std::vector<uint8_t>>` (`nanodbc.cpp:4679-4750`) fetches unbound (blob) columns with `SQLGetData(SQL_C_BINARY)` in 1 KB chunks, and bound varchar columns from the bound buffer using the length indicator. Both paths are length-delimited, so **embedded 0x00 bytes survive**.
- **Contrast.** `get_ref<std::string>` on a bound char column uses `convert(const char*)` (`:4480-4484`) and **stops at the first 0x00**.

**Embedded 0x00 is certain.** Empty slots are all zeros, and the high bytes of int16/int32 values are usually zero. The seed confirms it (table above).

**Proc handling.** The called procs pass these values straight through: `col = @param`, `SELECT col`. There is no CONVERT, SUBSTRING, LEN or varbinary cast. SQL Server *silently truncates* a value assigned to a too-short varchar procedure parameter (`@strSkill varchar(20)` versus a 9-byte payload is fine today).

### 4.4 Other behaviours

**OUTPUT parameters.**
- Used by 13 of the 15 called procs. LOAD_KNIGHTS_MEMBERS has none; LOAD_ACCOUNT_CHARID uses only a RETURN value.
- All are bound `PARAM_OUT`, so the T-SQL variable starts NULL. CREATE_NEW_CHAR line 36 depends on that.
- They are read only after the proc object is destroyed (§0).
- Read and used: ACCOUNT_LOGIN, NATION_SELECT, CREATE_NEW_CHAR, CREATE_KNIGHTS, UPDATE_KNIGHTS, DELETE_KNIGHTS, CLAIM_USER_RANK_STIPEND and LOAD_PREMIUM_SERVICE_USER (`days`).
- Read only for a debug log: ACCOUNT_LOGOUT.
- Bound but never read: LOAD_USER_DATA, LOAD_CHAR_INFO.

**RETURN codes.** Only LOAD_ACCOUNT_CHARID (`RETURN @@ROWCOUNT`, read via `{? = CALL}`) returns a value, and the C++ ignores it. UPDATE_PREMIUM_SERVICE_USER also has a `{? = CALL}` wrapper but is never called. Every other RETURN is a bare early exit.

**Early RETURN with no result set.** LOAD_USER_DATA (line 17) and LOAD_CHAR_INFO (line 14) can return before their SELECT. The C++ then calls `result->next()`, nanodbc throws a `database_error`, and the caller treats it as "not found" (`DBAgent.cpp:118-121, 581-584`). In both cases the outcome is a failed load, but via an exception rather than an empty result.

**`@@ROWCOUNT` and row counts.**
- In procs: ACCOUNT_LOGIN lines 8 and 15 (after assignment SELECTs), LOAD_USER_DATA line 23, LOAD_ACCOUNT_CHARID line 14.
- In C++, `affected_rows()` (= `SQLRowCount`, `nanodbc.cpp:2115-2124`) is called right after execute, before results are drained:
  - Failure when it is 0: `DBAgent.cpp:467` (UPDATE_USER_DATA), `:1000` (UPDATE_WAREHOUSE), `:1144` (ACCOUNT_LOGOUT), `:1109` (Q4/Q5), `:1339` (Q9). A value of -1 is treated as success.
  - Success when it is greater than 0: `DBProcess.cpp:133, 162` (dead Q11/Q12).
- None of the procs sets NOCOUNT, so for a `{CALL}` the count is whatever the first count-producing statement reports. For UPDATE_USER_DATA with `@Zone > 2` that may be the assignment SELECT. The exact value cannot be determined statically.

**`@@ERROR`.** NATION_SELECT, CREATE_NEW_CHAR, CREATE_KNIGHTS, UPDATE_KNIGHTS, DELETE_KNIGHTS and CLAIM_USER_RANK_STIPEND each check `@@ERROR` once, after the *last* DML statement of a group. A failure in an earlier statement is invisible and the transaction commits partially (for example CREATE_KNIGHTS lines 37-48). Errors also travel to the client in the result stream; whether nanodbc throws during `execute()` or during `flush()` (where the destructor swallows it, `StoredProcedure.cpp:31-42`) cannot be determined statically.

**TOP.** Never present in server SQL: SqlBuilder emits `TOP` only when `Limit > 0` (`SqlBuilder.h:40-41`), and no caller sets it. Only the uncalled procs UPDATE_PERSONAL_RANK and USER_KNIGHTS_RATING_UPDATE use `TOP 100`.

**ORDER BY.**
- The only explicit `ORDER BY` in server SQL is Q8 (`ORDER BY [Points] DESC`).
- None of the 15 called procs orders its result.
- Code that relies on **implicit row order** (SQL Server returns clustered-PK order or heap allocation order in practice, but guarantees nothing):
  - LEVEL_UP positional indexing
  - K_NPCPOS (a heap with no PK) NPC serial numbers
  - ZONE_INFO zone-array order (AIServer and Ebenezer)
  - KNIGHTS_RATING captain selection
  - BATTLE and KNIGHTS_SIEGE_WARFARE "last row wins"
  - ITEM_UPGRADE first-match scan
  - LOAD_KNIGHTS_MEMBERS member order
  - STLMap "first duplicate wins"

**NULL handling.**
- Explicit `is_null` guards in C++: `DBAgent.cpp:149, 158, 164, 170, 178, 593, 650-657`.
- Nullable columns map to `std::optional` in the binders (e.g. `AujardBinder.cpp:31, 192, 197`).
- Non-optional binders on NULL silently produce `T{}`, because `NANODBC_DISABLE_NULL_ACCESS_ERROR` is on.
- Procs use `ISNULL` (LOAD_USER_DATA) and three-valued logic inside `IF` (LOAD_USER_DATA, CREATE_NEW_CHAR, CLAIM_USER_RANK_STIPEND; §1.3).

**datetime/smalldatetime.**
- Columns: one datetime (`TB_USER.PremiumExpire`, NOT NULL, `DEFAULT getdate()+(3)`) and 16 smalldatetime (`USERDATA.CreateTime/UpdateTime`, `KNIGHTS.CreateTime`, `USER_PERSONAL_RANK.UpdateDate`, the rental/log/editor/itemmall timestamps).
- **C++ reads exactly one of them**: `full_model::TbUser.PremiumExpire` in telnet auth (`FullBinder.cpp:5585-5590`). That goes through `binderUtil::CTimeFromDbTime` (`db-models/BinderUtil/BinderUtil.cpp:6-17`), which has bugs:
  - `tm_mon = ts.month` with no `-1`, so the month is off by one.
  - `tm_isdst` is left uninitialised.
  - It uses `mktime`, i.e. local time, while the inverse `DbTimeFromCTime` uses `gmtime`, i.e. UTC.

  The value is then unused (`TelnetClientThread.cpp:216-232`).
- No AIServer, Aujard, Ebenezer or VersionManager model binds a datetime column. All 11 timestamp binders are in `Full`.
- Proc-side date logic:
  - `UPDATE_USER_DATA` line 101 sets `UpdateTime = getdate()`.
  - `LOAD_PREMIUM_SERVICE_USER` line 10 uses `DateDiff(dd, GetDate(), …)`.
  - INSERTs in CREATE_NEW_CHAR and CREATE_KNIGHTS pick up `getdate()` defaults.
- Seeds use `CAST(N'2016-05-26T21:48:24.15' AS DateTime)`: 10,851 occurrences across the KNIGHTS, TB_USER, USER_EDITOR_ITEM, USERDATA and USER_PERSONAL_RANK seed files.

**Integer types.**
- tinyint is unsigned 0–255 in SQL Server and maps to `uint8_t` throughout. Values up to 255 are present in the seed (for example `Dex 255`).
- There are no `bit` columns in the schema.
- smallint maps to `int16_t`; int maps to `int32_t`, except where Aujard reads it as `uint32_t` (Exp, Loyalty, Gold, PX/PZ/PY, dwTime, MannerPoint, LoyaltyMonthly; `DBAgent.cpp:131-185, 1204-1205`). bigint maps to `int64_t` (`KNIGHTS.Gold`, `EbenezerBinder.cpp:1089`).
- Writes bind exactly typed C values. Reads go through int64 and a C-cast, which wraps silently.
- SQL Server **raises an error** on out-of-range writes and arithmetic, e.g. `bCharNum + 1` past 255 in CREATE_NEW_CHAR, or `@Members tinyint` in UPDATE_KNIGHTS.

**IDENTITY.** No table has an IDENTITY column. The only IDENTITY uses are temp tables or `ALTER TABLE` inside uncalled procs (KNIGHTS_RATING_UPDATE, UPDATE_PERSONAL_RANK, USER_KNIGHTS_RATING_UPDATE, CHANGE_COPY_SERIAL_ITEM_TABLE). Clan IDs are allocated in C++ and passed in. No server reads a generated key.

**Defaults the servers rely on** (`ALTER TABLE … ADD CONSTRAINT DF_… DEFAULT … FOR …` in `5_` files):
- **CREATE_NEW_CHAR**'s 14-column INSERT into USERDATA relies on:
  - `Rank 0`, `Title 0`, `Level 1`, `Exp 5`, `Loyalty 500`, `City 0`, `Knights 0`, `Fame 0`
  - `Hp/Mp/Sp 100`, `Authority 1`, `Points 0`, `Gold 50000`, `PY 0`, `dwTime 0`
  - `strSkill 0x00` (a binary default on a varchar column)
  - `sQuestCount 0`, `MannerPoint 0`, `LoyaltyMonthly 0`, `CreateTime getdate()`
- **NATION_SELECT** relies on `ACCOUNT_CHAR.bCharNum 0`, `WAREHOUSE.nMoney 0`, `WAREHOUSE.dwTime 0`.
- **CREATE_KNIGHTS** relies on `KNIGHTS.Ranking 0`, `Members 1`, `Gold 0`, `Points 0`, `CreateTime getdate()`, `sCape -1`, and the other zero defaults.

**Triggers.** None. There is no `CREATE TRIGGER` in OpenKO-db; "EVENT_TRIGGER" is a table name.

**Views.** One, `7_CreateView_ACCOUNT_CHAR_VIEW.sql`, which hard-codes `[dbo].[ACCOUNT_CHAR]`. Neither the servers nor db-models reference it.

**Schema prefixes.**
- Server SQL never uses one.
- OpenKO-db creates tables and most procs unqualified, i.e. in the default schema of whoever runs the script. The `knight` user has default schema `knight` (`3_CreateUser_knight.sql`), but the login is `sysadmin` (`4_CreateLogin_knight.sql`), so in practice it maps to `dbo`.
- `CREATE_KNIGHTS`, `DELETE_KNIGHTS` and 14 uncalled procs (16 in total) are explicitly `[dbo].`.

### 4.5 ODBC connection string, DSN and attributes

- **Connection string:** `"DSN={};UID={};PWD={};AutoTranslate=no"` (`db-library/ConnectionManager.cpp:66-73`), opened with `nanodbc::connection(connstr, timeout)` (`:128-131`).
  - `AutoTranslate` is a **SQL Server driver attribute**. It is the only server-specific attribute.
  - No MARS, no `Database=`, no isolation level, no statement timeouts. Autocommit is the nanodbc default; the servers never open client-side transactions.
- **Reconnect path differs.** `Connection::Reconnect` calls `_conn->connect(DSN, UID, PWD, timeout)`, i.e. `SQLConnect` (`db-library/Connection.cpp:25-31`). That path **does not carry `AutoTranslate=no`**, so after a reconnect the DSN's own setting applies. This is an existing bug that can corrupt binary data stored in varchar under SQL Server.
- **Login timeouts:**
  - Aujard: `DB_PROCESS_TIMEOUT = 10` (`Aujard/Define.h:19`, `AujardApp.cpp:35`)
  - VersionManager: `100` (`VersionManager/Define.h:25`, `VersionManagerApp.cpp:23`)
  - AIServer and Ebenezer: the static default of 0 (`ConnectionManager.cpp:13`)
- **Pooling.** One pool per DbType, with mutex-guarded pull and restore (`ConnectionManager.cpp:179-201`). Idle connections expire after 600 s, checked every minute in Aujard and VersionManager (`AujardApp.cpp:38-39`, `VersionManagerApp.cpp:26-27`).
- **DSN sources:**
  - Aujard: `[ODBC] ACCOUNT_DSN/UID/PWD` and `GAME_DSN/...` (`AujardApp.cpp:124-136`)
  - VersionManager: `[ODBC] DSN/UID/PWD`, applied to both DbTypes (`VersionManagerApp.cpp:110-119`)
  - AIServer and Ebenezer: `[ODBC] GAME_*` (`AIServerApp.cpp:1577-1582`, `EbenezerApp.cpp:1228-1233`)
  - Every default is `KN_online` / `knight` / `knight`.
- **Logging.** VersionManager logs the full connection string, password included, at debug level (`VersionManagerApp.cpp:69`).

### 4.6 Additional findings that affect a port

1. **Schema drift in OpenKO-db.** `5_CreateTable_USER_KNIGHTS_RANK.sql` and `5_CreateTable_USER_PERSONAL_RANK.sql` lack `isClaimedElmo` and `isClaimedKarus`. Those columns exist in jsonSchema (`user_knights_rank.json`, `user_personal_rank.json`; tinyint, default 0), in the seed INSERT column lists, in the Ebenezer models, and in CLAIM_USER_RANK_STIPEND. These are the only DDL/jsonSchema/seed/Full-model mismatches (checked with `consistency.py`). **Generate the SQLite schema from jsonSchema, not from the `5_` files.**
2. **Seed file encoding and raw NUL bytes.**
   - The `6_` files are UTF-8. `ITEM.strName` has 234 rows containing U+2019, `SERVER_RESOURCE.strResource` has 3, and `KNIGHTS_CAPE.strName` has 1 mojibake row.
   - Loaded into a SQL Server varchar with code page 1252, these become single CP1252 bytes (U+2019 becomes 0x92), and the C++ sees those raw bytes because of `AutoTranslate=no`.
   - Separately, **12 seed files contain raw 0x00 bytes inside `N'…'` string literals**: MAGIC, MAGIC_TYPE1–9, ITEM_UPGRADE and ITEM_EXCHANGE, between 1,203 and 462,430 NUL bytes per file, for example in `MAGIC.KrName`, `ITEM_UPGRADE.strName/strNote`, `ITEM_EXCHANGE.strNpcName/strNote`.
3. **Concurrency.** Four server processes share one database. Aujard also writes from several threads (the read-queue thread, a heartbeat timer and a 5-minute CONCURRENT timer, `AujardApp.cpp:38-53`) through pooled connections. The save path retries up to 10 times with 10 ms sleeps (`AujardApp.cpp:400-476`).
4. **Column-name binding.** Every model load relies on `result.column_name(i)` exactly matching the generated binder key (`Model.h:114-123`); a mismatch silently leaves the field at 0. Proc result sets are read **by ordinal** instead (§1.3 #7–10).

---

## 5. Summary

| Item | Count |
|---|---|
| Stored procs defined in OpenKO-db | 70 (all 70 have generated wrappers in `StoredProc.cpp`) |
| Stored procs called by server code | **15** (Aujard 14, VersionManager 1). AIServer, Ebenezer and ItemManager call none. |
| Stored procs never called | 55 |
| Procs referenced only in commented-out code (and absent from OpenKO-db) | 3 (CHECK_COUPON_EVENT, UPDATE_COUPON_EVENT, DELETE_CHAR) |
| Non-proc query sites | **16** (14 live, 2 dead). By kind: 6 raw-SQL strings, 9 SqlBuilder/ModelRecordSet, 1 runtime RecordSetLoader. |
| Startup loader call sites / distinct tables | **44 / 35** (AIServer 17/16, Ebenezer 25/25, Aujard 1/1, VersionManager 1/1; the per-server sets overlap) |
| Startup tables with 0 seed rows | 1 (KNIGHTS_SIEGE_WARFARE, AllowEmpty). Effectively 3 if built from the `5_` DDL (§4.6). |
| Tables / seeded tables / views / triggers / IDENTITY columns | 76 / 60 / 1 (unused) / 0 / 0 |
| Fixed-width CHAR columns / server-read CHAR columns | 12 / 1 (value unused) |
| Binary-in-varchar columns read or written by the servers | 6 (USERDATA ×4, WAREHOUSE ×2) plus `KNIGHTS.Mark` (image, read only) |

### Ranked porting hazards: where a naive SQLite port would silently diverge

1. **Case- and trailing-space-insensitive string equality.**
   - Account IDs are typed by users; character and clan names are compared in procs, in WHERE clauses, and by PK/UNIQUE constraints (§4.1).
   - With SQLite's default `BINARY` collation, lookups such as WAREHOUSE or ACCOUNT_LOGIN by a differently-cased account ID silently match nothing. UPDATE_USER_DATA and UPDATE_WAREHOUSE then affect 0 rows, so saves fail into the retry loop. Uniqueness checks allow duplicates like "Bob"/"bob" (USERDATA PK, `IX_KNIGHTS`).
   - Fix: a custom collation (case-insensitive *and* trailing-space-insensitive) on every former CI column, and the same semantics in C++ wherever proc logic is reimplemented. `NOCASE` alone does not ignore trailing spaces, and `RTRIM` alone is case-sensitive.
   - Decide whether passwords stay case-insensitive (ACCOUNT_LOGIN) or case-sensitive (VersionManager and telnet).
2. **Packed binary in "varchar" with embedded 0x00.**
   - Must be bound with `sqlite3_bind_blob` and the explicit length, and read with `sqlite3_column_blob`/`_bytes`.
   - Any TEXT path (`sqlite3_column_text` into a C string, `std::string(const char*)`, SQL string functions) silently truncates at the first NUL, which zeroes inventory, serials and quests.
   - Seed `CONVERT(varchar(n), 0x…)` (1,252 occurrences) must become `X'…'` blobs.
3. **Implicit row order.**
   - LEVEL_UP is indexed by position.
   - K_NPCPOS has no PK, and NPC serials follow row order.
   - ZONE_INFO order drives the zone arrays; KNIGHTS_RATING order picks captains; BATTLE/KNIGHTS_SIEGE_WARFARE use the last row; ITEM_UPGRADE and STLMap use the first match.
   - SQLite returns rowid or index order, not clustered-PK order.
   - Fix: add an explicit `ORDER BY <PK>` (or `rowid` for the heaps) to every loader, and import rows in their original order.
4. **Reimplementing 15 procs without losing their quirks.**
   - OUTPUT parameters start NULL.
   - An early RETURN produces *no* result set, which the C++ sees as an exception.
   - `@@ERROR` only checks the last statement, which allows partial commits (CREATE_KNIGHTS, NATION_SELECT).
   - UPDATE_KNIGHTS has a COMMIT before its RETURN in one branch.
   - Three-valued NULL logic: a missing rank row makes the stipend claim return success; a missing ACCOUNT_CHAR row lets LOAD_USER_DATA continue.
   - Variable comparisons use the database default collation, which is case-insensitive.
5. **`affected_rows()` contract.**
   - C++ treats 0 as failure and -1 as success.
   - SQL Server reports the first count-producing statement in a NOCOUNT-off proc (for UPDATE_USER_DATA with `Zone > 2` that is an assignment SELECT).
   - A SQLite backend that returns -1 hides real failures. One that returns `sqlite3_changes()` from the wrong statement changes save and logout success semantics.
6. **Date/time.**
   - Seed `CAST(N'…' AS DateTime)` (10,851 occurrences) gets NUMERIC affinity in SQLite, so it would store `2016`, silently.
   - `getdate()` is server local time; SQLite `CURRENT_TIMESTAMP` is UTC text.
   - `DATEDIFF(dd, …)` counts midnight crossings; a `julianday` difference is fractional. This changes the premium days reported at login.
   - smalldatetime rounds to the minute.
   - `CTimeFromDbTime` has the month off by one.
7. **No length, range or NOT NULL enforcement unless declared.**
   - SQL Server rejects over-length INSERT/UPDATE (error 8152) and tinyint/smallint overflow (error 220). SQLite stores any value.
   - The C++ copies blobs into fixed buffers assuming the limits hold (`DBAgent.cpp:899, 902` copy into `char[1600]`).
   - It reads integers through int64 with wraparound.
   - A missing NOT NULL or default yields NULLs that `NANODBC_DISABLE_NULL_ACCESS_ERROR` silently turns into 0 or "".
   - Fix: declare NOT NULL, DEFAULT and `CHECK(length(x) <= n)`, plus CHECKs for the 0–255 and int16 ranges.
8. **Seed text encoding and raw NULs.**
   - The seed files are UTF-8, but SQL Server stores CP1252 bytes, so `ITEM.strName` and `SERVER_RESOURCE` strings would change byte length and content on the wire.
   - Raw 0x00 bytes inside string literals in 12 seed files truncate statements if fed to `sqlite3_prepare(..., -1)` or similar NUL-terminated APIs.
   - Write a dedicated importer (convert to CP1252 or decide on UTF-8 deliberately; bind values, don't exec text).
9. **Result-column naming and positional reads.**
   - Model loads bind by exact, case-sensitive column name, and a mismatch silently zeroes the field. SQLite's column name without `AS` is officially unspecified.
   - Proc results (LOAD_USER_DATA's 37 columns) are read by ordinal, so a reimplemented query must keep the exact column order.
   - Fix: alias every column (`[col] AS [col]`).
10. **Concurrency model.**
    - SQL Server row and page locking across four processes and several Aujard threads becomes SQLite's single writer, with `SQLITE_BUSY` surfacing as `database_error`. Saves fail after 10 × 10 ms retries.
    - Needs WAL, `busy_timeout`, and short transactions.
    - The existing "pool connection restored before flush" ordering (§0) must not be carried over into the new backend.

**Lower-ranked, mostly loud rather than silent:**
- **Schema drift.** The `isClaimed*` columns are missing from the `5_` DDL, so a naive build makes Ebenezer fail to start (§4.6).
- **ACCOUNT vs GAME DbType.** Both must point at the same SQLite file: CONCURRENT is written through ACCOUNT (Q1) and read through GAME (Q13).
- **T-SQL-only syntax** that a SQLite engine rejects outright:
  - in the seed and DDL files: `N'…'`, `CONVERT`, `ALTER TABLE … ADD CONSTRAINT … DEFAULT … FOR`, `GO`, `USE`, `CREATE UNIQUE NONCLUSTERED INDEX`
  - in server SQL: `TOP` (SqlBuilder) and the latent `SelectCountString` bug
- **SQLDescribeParam.** If the port keeps nanodbc and uses a SQLite ODBC driver, every bind depends on `SQLDescribeParam` (`nanodbc.cpp:2239`), and how well a given driver supports it cannot be determined statically.
- **SQL injection.** Q4–Q7 interpolate packet strings into SQL; the port should switch them to bound parameters.
- **`AutoTranslate=no`** is lost on reconnect (`Connection.cpp:28-29`). This is SQL Server-only; SQLite does no translation, so the bug disappears.
