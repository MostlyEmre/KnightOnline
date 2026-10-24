#ifndef DBLIBRARY_SQLITE_COLLATION_H
#define DBLIBRARY_SQLITE_COLLATION_H

#pragma once

#include <string_view>

struct sqlite3;

namespace db::sqlite
{

/// \brief Name of the collation every former SQL Server character column is declared with.
inline constexpr const char* KoCollationName = "KO_CI";

/// \brief Compares two CP1252 byte strings the way SQL_Latin1_General_CP1_CI_AS compares
/// varchar values for equality: case-insensitive, accent-sensitive, trailing spaces ignored.
/// Ordering is by case-folded byte value, which matches SQL Server for the identifiers the
/// game stores (letters, digits, '_'), but not its dictionary order for punctuation.
int KoCompare(std::string_view lhs, std::string_view rhs);

inline bool KoEquals(std::string_view lhs, std::string_view rhs)
{
	return KoCompare(lhs, rhs) == 0;
}

/// \brief Registers KO_CI on a connection. Must be done on every connection that touches
/// the database, including the tool that builds it, because indexes are ordered by it.
int RegisterCollations(sqlite3* db);

} // namespace db::sqlite

#endif // DBLIBRARY_SQLITE_COLLATION_H
