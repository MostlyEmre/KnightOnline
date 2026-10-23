#include <gtest/gtest.h>

#include "Backend.h"
#include "Collation.h"

#include <nanodbc/nanodbc.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

using db::sqlite::KoCompare;
using db::sqlite::KoEquals;

namespace
{

// A scratch database file with the schema bits the tests need, removed afterwards.
class SqliteBackendTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
		_path            = (std::filesystem::temp_directory_path()
                 / (std::string("openko-sqlite-") + info->name() + ".sqlite"))
					.string();
		RemoveFiles();

		sqlite3* db = nullptr;
		ASSERT_EQ(sqlite3_open_v2(
					  _path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr),
			SQLITE_OK);
		db::sqlite::RegisterCollations(db);
		const char* schema = R"(
			CREATE TABLE [CURRENTUSER] ([strAccountID] TEXT COLLATE KO_CI NOT NULL PRIMARY KEY,
				[nServerNo] INTEGER NOT NULL) WITHOUT ROWID;
			CREATE TABLE [T] ([id] INTEGER PRIMARY KEY, [name] TEXT COLLATE KO_CI, [data] BLOB,
				[stamp] TEXT, [ratio] REAL);
			INSERT INTO [CURRENTUSER] VALUES ('Probe', 1);
			INSERT INTO [T] VALUES (1, 'Alpha', X'0100FF00', '2016-05-26 21:48:24.150', 1.5),
				(2, 'beta  ', NULL, NULL, NULL), (3, 'GAMMA', X'', '2026-03-31 21:50:00.000', -2.25);
		)";
		ASSERT_EQ(sqlite3_exec(db, schema, nullptr, nullptr, nullptr), SQLITE_OK);
		sqlite3_close(db);

		_conn = nanodbc::connection(_path);
	}

	void TearDown() override
	{
		_conn.disconnect();
		RemoveFiles();
	}

	void RemoveFiles() const
	{
		std::error_code ec;
		for (const char* suffix : { "", "-wal", "-shm" })
			std::filesystem::remove(_path + suffix, ec);
	}

	std::string _path;
	nanodbc::connection _conn;
};

} // namespace

TEST(SqliteCollation, CaseInsensitiveAccentSensitiveIgnoresTrailingSpaces)
{
	EXPECT_TRUE(KoEquals("BitShift", "bitshift"));
	EXPECT_TRUE(KoEquals("BitShift", "BITSHIFT   "));
	EXPECT_FALSE(KoEquals("BitShift", " BitShift"));
	EXPECT_FALSE(KoEquals("BitShift", "BitShif"));
	// CP1252: É (0xC9) / é (0xE9) fold together, but e and é don't.
	EXPECT_TRUE(KoEquals("\xC9t\xE9", "\xE9T\xC9"));
	EXPECT_FALSE(KoEquals("e", "\xE9"));
	// Š (0x8A) / š (0x9A), Ÿ (0x9F) / ÿ (0xFF)
	EXPECT_TRUE(KoEquals("\x8A\x9F", "\x9A\xFF"));
	EXPECT_LT(KoCompare("abc", "ABD"), 0);
	EXPECT_GT(KoCompare("abcd", "ABC"), 0);
	EXPECT_EQ(KoCompare("", "   "), 0);
}

TEST(SqliteDateTime, ParsesStoredForm)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
	ASSERT_TRUE(db::sqlite::ParseDateTime("2016-05-26 21:48:24.15", y, mo, d, h, mi, s, ms));
	EXPECT_EQ(y, 2016);
	EXPECT_EQ(mo, 5);
	EXPECT_EQ(d, 26);
	EXPECT_EQ(h, 21);
	EXPECT_EQ(mi, 48);
	EXPECT_EQ(s, 24);
	EXPECT_EQ(ms, 150);
	ASSERT_TRUE(db::sqlite::ParseDateTime("2026-03-31", y, mo, d, h, mi, s, ms));
	EXPECT_EQ(h, 0);
	EXPECT_FALSE(db::sqlite::ParseDateTime("yesterday", y, mo, d, h, mi, s, ms));
}

TEST(SqliteDateTime, RoundsLikeSqlServer)
{
	std::time_t t  = 1700000000; // fixed instant; compare only the parts rounding touches
	std::string dt = db::sqlite::FormatDateTime(t, 2, false);
	EXPECT_EQ(dt.substr(dt.size() - 4), ".003");      // 1/300 s ticks: 2 ms -> .003
	dt = db::sqlite::FormatDateTime(t, 999, false);
	EXPECT_EQ(dt.substr(dt.size() - 4), ".000");      // rounds up into the next second
	std::string sdt = db::sqlite::FormatDateTime(t, 0, true);
	EXPECT_EQ(sdt.substr(sdt.size() - 7), ":00.000"); // smalldatetime: minute precision
}

TEST_F(SqliteBackendTest, TopIsRewrittenAndCollationAppliesToParameters)
{
	nanodbc::statement stmt(
		_conn, "SELECT TOP 2 [id], [name] FROM [T] WHERE [name] <> ? ORDER BY [id]");
	stmt.bind(0, "ALPHA");
	nanodbc::result r = stmt.execute();
	ASSERT_EQ(r.columns(), 2);
	EXPECT_EQ(r.column_name(1), "name");
	EXPECT_EQ(r.affected_rows(), -1);
	std::vector<int> ids;
	while (r.next())
		ids.push_back(r.get<int>(0));
	EXPECT_EQ(ids, (std::vector<int> { 2, 3 }));
}

TEST_F(SqliteBackendTest, TypedReadsFollowNanodbc)
{
	nanodbc::statement stmt(
		_conn, "SELECT [name], [data], [stamp], [ratio] FROM [T] ORDER BY [id]");
	nanodbc::result r = stmt.execute();

	ASSERT_TRUE(r.next());
	EXPECT_EQ(r.get<std::string>(0), "Alpha");
	EXPECT_EQ(r.get<std::vector<uint8_t>>(1), (std::vector<uint8_t> { 0x01, 0x00, 0xFF, 0x00 }));
	nanodbc::timestamp ts = r.get<nanodbc::timestamp>(2);
	EXPECT_EQ(ts.year, 2016);
	EXPECT_EQ(ts.sec, 24);
	EXPECT_EQ(ts.fract, 150000000);
	EXPECT_DOUBLE_EQ(r.get<double>(3), 1.5);

	ASSERT_TRUE(r.next());
	// NULLs: T{} through get(), nullopt through optional (NANODBC_DISABLE_NULL_ACCESS_ERROR).
	EXPECT_TRUE(r.is_null(1));
	EXPECT_TRUE(r.get<std::vector<uint8_t>>(1).empty());
	EXPECT_EQ(r.get<double>(3), 0.0);
	std::optional<std::string> stamp = std::string("x");
	r.get_ref(2, stamp);
	EXPECT_FALSE(stamp.has_value());
	EXPECT_EQ(r.get<std::string>(0), "beta  "); // trailing spaces kept as stored

	ASSERT_TRUE(r.next());
	EXPECT_FALSE(r.is_null(1));                 // zero-length blob is not NULL
	EXPECT_TRUE(r.get<std::vector<uint8_t>>(1).empty());
	EXPECT_FALSE(r.next());
}

TEST_F(SqliteBackendTest, DmlReportsRowCountAndHasNoCursor)
{
	nanodbc::statement stmt(
		_conn, "UPDATE [CURRENTUSER] SET [nServerNo] = 2 WHERE [strAccountID] = 'PROBE'");
	nanodbc::result r = stmt.execute();
	EXPECT_EQ(r.affected_rows(), 1);
	EXPECT_EQ(r.columns(), 0);
	try
	{
		r.next();
		FAIL() << "next() on a statement without a result set should throw";
	}
	catch (const nanodbc::database_error& ex)
	{
		EXPECT_EQ(ex.state(), "24000");
	}
	EXPECT_FALSE(r.next_result());
}

TEST_F(SqliteBackendTest, ConstraintViolationMapsToSqlServerError)
{
	nanodbc::statement stmt(_conn, "INSERT INTO [CURRENTUSER] VALUES ('probe', 3)");
	try
	{
		stmt.execute();
		FAIL() << "duplicate key (case-insensitive) should fail";
	}
	catch (const nanodbc::database_error& ex)
	{
		EXPECT_EQ(ex.state(), "23000");
		EXPECT_EQ(ex.native(), 2627);
	}
}

TEST_F(SqliteBackendTest, OutputParametersArriveAfterTheResultsAreDrained)
{
	int16_t ret1 = -1, ret2 = -1;
	const int32_t logoutCode = 1;
	nanodbc::statement stmt(_conn, "{CALL ACCOUNT_LOGOUT(?,?,?,?)}");
	stmt.bind(0, "PROBE");
	stmt.bind(1, &logoutCode);
	stmt.bind(2, &ret1, nanodbc::statement::PARAM_OUT);
	stmt.bind(3, &ret2, nanodbc::statement::PARAM_OUT);
	nanodbc::result r = stmt.execute();

	EXPECT_EQ(r.affected_rows(), 1); // the DELETE's row count is the first result
	EXPECT_EQ(ret1, -1);             // not yet: SQL Server sends outputs after the results
	EXPECT_FALSE(r.next_result());
	EXPECT_EQ(ret1, 1);
	EXPECT_EQ(ret2, 1);
}

TEST_F(SqliteBackendTest, UnknownProcedureIsReportedLikeSqlServer)
{
	nanodbc::statement stmt(_conn, "{CALL NO_SUCH_PROC(?)}");
	stmt.bind(0, "x");
	try
	{
		stmt.execute();
		FAIL();
	}
	catch (const nanodbc::database_error& ex)
	{
		EXPECT_EQ(ex.native(), 2812);
	}
}
