#include "ApplyScript.h"
#include "Collation.h"

#include <sqlite3.h>

namespace db::sqlite
{

namespace
{

bool Exec(sqlite3* db, const char* sql, std::string& error)
{
	char* message = nullptr;
	if (sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK)
		return true;

	error = message != nullptr ? message : sqlite3_errmsg(db);
	sqlite3_free(message);
	return false;
}

} // namespace

bool ApplyScript(
	const std::filesystem::path& database, const std::string& script, std::string& error)
{
	sqlite3* db = nullptr;
	if (sqlite3_open_v2(database.string().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)
		!= SQLITE_OK)
	{
		error = db != nullptr ? sqlite3_errmsg(db) : "out of memory";
		sqlite3_close(db);
		return false;
	}

	RegisterCollations(db);
	sqlite3_busy_timeout(db, 5000);

	bool ok = Exec(db, "BEGIN IMMEDIATE", error);
	if (ok)
	{
		ok = Exec(db, script.c_str(), error) && Exec(db, "COMMIT", error);
		if (!ok)
		{
			std::string ignored;
			Exec(db, "ROLLBACK", ignored);
		}
	}

	sqlite3_close(db);
	return ok;
}

} // namespace db::sqlite
