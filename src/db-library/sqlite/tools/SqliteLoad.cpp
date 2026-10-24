// openko-sqlite-load <database> <script.sql> [--check]
//
// Creates <database> (replacing any existing file) from a SQL script with the SQLite backend's
// collations registered, then runs an integrity check. The seed database is built this way
// because its indexes are ordered by KO_CI, which plain sqlite3 doesn't know.

#include "Collation.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>

#include <sqlite3.h>

namespace
{

bool Exec(sqlite3* db, const std::string& sql)
{
	char* error = nullptr;
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
	{
		std::cerr << "openko-sqlite-load: " << (error != nullptr ? error : "error") << '\n';
		sqlite3_free(error);
		return false;
	}
	return true;
}

int Run(const std::filesystem::path& databasePath, const std::filesystem::path& scriptPath)
{
	std::ifstream script(scriptPath, std::ios::binary);
	if (!script)
	{
		std::cerr << "openko-sqlite-load: cannot read " << scriptPath << '\n';
		return 1;
	}
	std::stringstream buffer;
	buffer << script.rdbuf();

	std::error_code ec;
	for (const char* suffix : { "", "-wal", "-shm", "-journal" })
		std::filesystem::remove(databasePath.string() + suffix, ec);

	sqlite3* db = nullptr;
	if (sqlite3_open_v2(
			databasePath.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)
		!= SQLITE_OK)
	{
		std::cerr << "openko-sqlite-load: cannot create " << databasePath << ": "
				  << sqlite3_errmsg(db) << '\n';
		sqlite3_close(db);
		return 1;
	}

	db::sqlite::RegisterCollations(db);

	bool ok = Exec(db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=OFF;") && Exec(db, "BEGIN")
			  && Exec(db, buffer.str()) && Exec(db, "COMMIT")
			  && Exec(db, "PRAGMA journal_mode=WAL; VACUUM;");

	if (ok)
	{
		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &stmt, nullptr);
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			std::string line = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
			if (line != "ok")
			{
				std::cerr << "openko-sqlite-load: integrity check: " << line << '\n';
				ok = false;
			}
		}
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);
	if (!ok)
	{
		std::filesystem::remove(databasePath, ec);
		return 1;
	}

	std::cout << "openko-sqlite-load: wrote " << databasePath.string() << '\n';
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	std::span<char*> args(argv, static_cast<size_t>(argc));
	if (args.size() < 3)
	{
		std::cerr << "usage: openko-sqlite-load <database> <script.sql>\n";
		return 2;
	}

	try
	{
		return Run(args[1], args[2]);
	}
	catch (const std::exception& ex)
	{
		std::cerr << "openko-sqlite-load: " << ex.what() << '\n';
		return 1;
	}
}
