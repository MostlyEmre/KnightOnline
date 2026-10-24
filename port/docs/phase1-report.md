# Phase 1: database (SQLite backend)

Branch `macos-port`. Phase 1's scope was a SQLite backend in db-library alongside the ODBC one, the stored procedures the servers need reimplemented, a converter from OpenKO-db to a seed `.sqlite` file, and the servers running natively on the Mac against it.

## Result

| | |
|---|---|
| Servers on this Mac (arm64) | All five run natively against the seed database and shut down cleanly. Startup row counts equal SQL Server's for every table. |
| Upstream unit tests on arm64 | FileIO, MathUtils and Ebenezer pass, plus 14 new backend tests. Locally and on `macos-latest`. |
| Proof 2, database parity (rig B) | **Passed.** 174 of 174 cases identical between SQL Server 2022 and SQLite: 86 procedure calls, 12 of the servers' raw queries, and all 76 tables dumped after the mutating cases. The two probe outputs are byte-identical (21.7 MB each, SHA-256 `1f51f79154bda1d5…`). Startup row counts identical for all 63 table loads ([report](phase1/parity-report.md), [SQL Server](phase1/startup-row-counts-sqlserver.txt) / [SQLite](phase1/startup-row-counts-sqlite.txt) row counts; probe outputs in the rig B run artifacts). |
| Proof 5, sanitizers | All five servers run under ASan+UBSan with zero reports, after one upstream UB fix (below). The probe is clean under both too. |
| Proof 8, upstream workflows | CMake (Windows/Linux/macOS + tests), MSBuild, clang-format and clang-tidy all green on the branch (in-fork PR #1). |
| Windows build / SQL Server backend | Unchanged: ODBC stays the default, and the SQLite backend is opt-in at configure time. |

Upstream files touched: 5, +58/−8 lines. New code: about 4,900 lines under `src/db-library/sqlite/` and `port/db/`.

## What was built

**The backend: `src/db-library/sqlite/`, selected with `-DOPENKO_DB_BACKEND=SQLITE`.**
- Everything above the database is written directly against nanodbc's classes, including the generated models and procedure wrappers in `deps/db-models`. So the backend implements exactly the subset of nanodbc's API those callers use, measured in the Phase 0 census, and provides the `nanodbc` target in its place. db-library, db-models and the five servers compile against it unchanged.
- Plain statements run on SQLite. The one T-SQL construct the servers emit (`SELECT TOP n`) is rewritten to `LIMIT n`.
- `{CALL NAME(?,…)}` and `{? = CALL …}` dispatch to C++ reimplementations of the 15 procedures the servers call (`Procedures.cpp`). Each follows its T-SQL statement by statement: NULL-initialised variables and OUTPUT parameters, three-valued logic, `@@ROWCOUNT`, and `@@ERROR` meaning only the previous statement (so partial commits happen as on SQL Server). Failing statements raise their error and execution carries on. Transactions are explicit.
- Comparisons use `KO_CI`, a collation matching `SQL_Latin1_General_CP1_CI_AS` equality: case-insensitive over CP1252, accent-sensitive, trailing spaces ignored. It's registered on every connection.
- The database file comes from `OPENKO_SQLITE_DB`, or `<DSN>.sqlite`. WAL mode and a busy timeout let the five processes share it.

**The converter: `port/db/openkodb_to_sqlite.py` → `openko-sqlite-load`.**
- The schema comes from jsonSchema, as kodb-util builds SQL Server, because the `5_CreateTable` scripts are stale.
- Clustered primary keys become `WITHOUT ROWID` tables, so unordered scans return key order the way SQL Server's clustered scans do. Heaps keep file order.
- Text is stored as CP1252 bytes, `char(n)` is padded, binary data becomes BLOBs, and datetimes are rounded the way `datetime`/`smalldatetime` store them.
- Ranges and lengths are `CHECK` constraints mapped to SQL Server's error numbers.
- Python writes a plain SQL script. A small C++ loader that shares the backend's collation code builds the file, because the indexes are ordered by that collation.
- Result: 76 tables and 96,476 rows, identical per-table counts to SQL Server, an 11 MB file, built in about 5 seconds.

**Proof tooling.**
- `port/db/parity/db-parity-probe` is built against each backend. It drives all 15 procedures through the same generated wrappers and connection pool the servers use, plus the servers' hand-written queries. That's 98 cases (86 procedure calls, 12 raw queries), including mixed-case account, character and clan IDs, empty character slots, full 400-byte inventories and 1600-byte warehouses, duplicates, unknown IDs and invalid parameters. For each case it records every result set, row count, error, output parameter and return value, and finally dumps all 76 tables. `compare.py` diffs the SQL Server and SQLite runs.
- `RecordSetLoader` logs per-table row counts (`recordset_loader: ITEM: 61530 rows`), so startup loads can be compared.
- Workflows:
  - `port-rig-b-database.yml`: SQL Server vs SQLite on Linux, servers plus probe.
  - `port-macos-servers.yml`: `macos-latest` arm64 build, upstream tests, all five servers, probe.
  - `port-lint.yml`: upstream's `.clang-tidy` applied to the backend.

## What SQL Server actually does (measured on rig B)

The first comparisons disagreed with assumptions I'd made, including one of the Phase 0 census's hazards. The backend now reproduces each of these, and the unit tests pin them:

1. **Assignment `SELECT @v = …` produces no row count.** Only `INSERT`/`UPDATE`/`DELETE` do. The census predicted otherwise (its hazard 5), but measurement wins.
2. **A procedure that returns nothing** (for example `ACCOUNT_LOGIN`, which only assigns variables) leaves no cursor. Its output parameters are available at once, and `columns()`/`affected_rows()` fail with SQLSTATE 24000.
3. **Row count of a result set:** 0 up front when it's empty, -1 while rows are pending.
4. **Strings end at the first NUL.** nanodbc reads ordinary bound character columns as C strings, so text with embedded NULs (12 seed tables have them) ends there, while `text` columns keep every byte. SQL Server stores the bytes; the servers just see the shorter string. The backend reproduces what they see.
5. **An empty `bind_binary()` value is stored as NULL** (zero length with a null buffer).
6. **Error 2628** (string truncation) arrives as SQLSTATE 42000.
7. **Import detail:** kodb-util writes NULL `text` values as `''`. The converter mirrors it, and the servers read both as `""` anyway.
8. **A probe artifact, not data:** nanodbc's number-to-string conversion has no room for the sign, so `-10000` in a `smallint` reads back as `"-1000"`. The servers read integers as integers, so the probe now does too.

## Upstream fixes made along the way

- **`shared-server/utilities.cpp`:** the `GetShort`/`GetInt`/`GetDWORD`/`GetFloat`/`GetInt64` packet readers dereferenced misaligned pointers. UBSan reports it on AIServer↔Ebenezer traffic, and it's undefined behaviour. They now use `memcpy`, like the `Set*` writers already did; the generated code is the same.
- **Not fixed** (upstream test code only, not the servers): the Ebenezer unit tests read packet fields through misaligned references. All 27 tests pass, but UBSan reports 20 sites. Left alone to avoid touching upstream's tests.
- **Noticed, not changed:** upstream's `$<CXX_COMPILER_ID:Clang>` warning flags don't match Apple's `AppleClang`, so their macOS builds don't get `-Wall -Wextra`. My targets match both. Phase 2 will need this for the client.

## Known differences and limits

- **Sort order of strings:** `KO_CI` orders by case-folded bytes. SQL Server's collation uses dictionary order for punctuation. Equality is identical. No query the servers send orders by a string column, and the probe compares table dumps order-independently.
- **Concurrency:** SQLite allows one writer at a time. Aujard is effectively the only writer, saves are short transactions, and WAL plus a 10-second busy timeout cover the rest. Phase 5's force-quit test will exercise durability: WAL with `synchronous=NORMAL` keeps committed transactions across a process crash.
- **The other 55 procedures** in OpenKO-db are not reimplemented, because nothing calls them. Calling one fails like SQL Server's "Could not find stored procedure" (2812).

## Proof status

| Proof | Status after Phase 1 |
|---|---|
| 1 Image parity | Phase 4. |
| 2 Database parity | **Passed** on rig B (`port-rig-b-database.yml`, run 40054126192): 174/174 cases identical; startup row counts identical. |
| 3 Persistence | Database side ready (WAL); tested in Phase 5. |
| 4 Protocol | Server-side baseline noise identical on both backends (`Unhandled opcode 32` from AIServer, quest-script warnings). |
| 5 Tests and sanitizers | Upstream tests pass on arm64 (local and CI). ASan+UBSan: servers and probe clean; client in later phases. |
| 6 Native proof | Server executables are arm64 Mach-O; bundle checks in Phase 5. Servers still listen on `0.0.0.0` (Phase 5). |
| 7 Instruments | Xcode 27.1 installed; its license still needs accepting (below). |
| 8 Upstream workflows | Green on the branch. |

## Needs your action

Xcode is installed, but until its license is accepted the Xcode shims (`git`, `clang`, `python3` in `/usr/bin`) and Homebrew refuse to run. I'm working around it by pointing builds at the Command Line Tools. Please run `sudo xcodebuild -license accept` once.

## Next: Phase 2

Get N3Base and WarFare compiling for arm64 with CMake, Ninja and Apple Clang, warnings as errors, with platform code behind clean boundaries in N3Base. The MinGW cross-build stays the Windows check for every change.
