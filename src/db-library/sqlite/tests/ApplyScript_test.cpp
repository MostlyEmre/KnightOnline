#include <gtest/gtest.h>

#include "ApplyScript.h"
#include "Collation.h"

#include <filesystem>
#include <string>

#include <sqlite3.h>

namespace
{

class SqliteApplyScriptTest : public testing::Test
{
protected:
	std::filesystem::path _path;

	void SetUp() override
	{
		_path = std::filesystem::temp_directory_path() / "SqliteApplyScriptTest.sqlite";
		std::filesystem::remove(_path);

		// A table ordered by KO_CI, which only works with the collation registered.
		sqlite3* db = nullptr;
		ASSERT_EQ(sqlite3_open(_path.string().c_str(), &db), SQLITE_OK);
		db::sqlite::RegisterCollations(db);
		ASSERT_EQ(sqlite3_exec(db,
					  "CREATE TABLE T (Num int NOT NULL, strName TEXT COLLATE KO_CI NOT NULL, "
					  "PRIMARY KEY (Num)) WITHOUT ROWID;"
					  "CREATE INDEX T_name ON T (strName);"
					  "INSERT INTO T VALUES (1, 'one');",
					  nullptr, nullptr, nullptr),
			SQLITE_OK);
		sqlite3_close(db);
	}

	void TearDown() override
	{
		std::filesystem::remove(_path);
	}

	std::string Names() const
	{
		sqlite3* db = nullptr;
		sqlite3_open(_path.string().c_str(), &db);
		db::sqlite::RegisterCollations(db);

		std::string names;
		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(db, "SELECT strName FROM T ORDER BY Num", -1, &stmt, nullptr);
		while (sqlite3_step(stmt) == SQLITE_ROW)
			names += std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) + ";";
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return names;
	}
};

TEST_F(SqliteApplyScriptTest, AppliesToAnExistingDatabase)
{
	const std::string script = "INSERT OR IGNORE INTO T VALUES (2, 'TWO');"
							   "INSERT OR IGNORE INTO T VALUES (3, 'three');";
	std::string error;
	EXPECT_TRUE(db::sqlite::ApplyScript(_path, script, error)) << error;
	EXPECT_EQ(Names(), "one;TWO;three;");

	// Again: nothing changes.
	EXPECT_TRUE(db::sqlite::ApplyScript(_path, script, error)) << error;
	EXPECT_EQ(Names(), "one;TWO;three;");
}

TEST_F(SqliteApplyScriptTest, AFailingScriptChangesNothing)
{
	std::string error;
	EXPECT_FALSE(db::sqlite::ApplyScript(
		_path, "INSERT INTO T VALUES (2, 'two'); INSERT INTO T VALUES (1, 'taken');", error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(Names(), "one;");
}

TEST_F(SqliteApplyScriptTest, NeverCreatesADatabase)
{
	const std::filesystem::path missing = _path.parent_path()
										  / "SqliteApplyScriptTest-missing.sqlite";
	std::filesystem::remove(missing);

	std::string error;
	EXPECT_FALSE(db::sqlite::ApplyScript(missing, "CREATE TABLE X (a int);", error));
	EXPECT_FALSE(std::filesystem::exists(missing));
}

} // namespace
