#ifndef DBLIBRARY_SQLITE_APPLYSCRIPT_H
#define DBLIBRARY_SQLITE_APPLYSCRIPT_H

#pragma once

#include <filesystem>
#include <string>

namespace db::sqlite
{

/// \brief Runs a SQL script on an existing database, with KO_CI registered, in one transaction:
/// all of it or none of it. Never creates the database. On failure, error says why.
bool ApplyScript(
	const std::filesystem::path& database, const std::string& script, std::string& error);

} // namespace db::sqlite

#endif // DBLIBRARY_SQLITE_APPLYSCRIPT_H
