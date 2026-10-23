#ifndef DBLIBRARY_SQLITE_BACKEND_H
#define DBLIBRARY_SQLITE_BACKEND_H

#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

struct sqlite3;

namespace db::sqlite
{

/// \brief A single SQL value. Text and blob payloads are raw bytes (CP1252 for text,
/// as SQL Server stores varchar).
struct Value
{
	enum class Type : uint8_t
	{
		Null,
		Integer,
		Real,
		Text,
		Blob
	};

	Type Kind   = Type::Null;
	int64_t Int = 0;
	double Real = 0.0;
	std::string Bytes;

	static Value Null()
	{
		return {};
	}

	static Value Integer(int64_t value)
	{
		Value v;
		v.Kind = Type::Integer;
		v.Int  = value;
		return v;
	}

	static Value Float(double value)
	{
		Value v;
		v.Kind = Type::Real;
		v.Real = value;
		return v;
	}

	static Value Text(std::string_view value)
	{
		Value v;
		v.Kind  = Type::Text;
		v.Bytes = value;
		return v;
	}

	static Value Blob(std::string_view value)
	{
		Value v;
		v.Kind  = Type::Blob;
		v.Bytes = value;
		return v;
	}

	template <typename T>
	static Value FromOptional(const std::optional<T>& value)
	{
		if (!value.has_value())
			return Null();
		if constexpr (std::is_integral_v<T>)
			return Integer(static_cast<int64_t>(*value));
		else
			return Text(*value);
	}

	bool IsNull() const
	{
		return Kind == Type::Null;
	}
};

/// \brief A result set, fully materialised (stored procedure results are small).
struct RowSet
{
	std::vector<std::string> Columns;
	std::vector<std::vector<Value>> Rows;
};

/// \brief One item of the sequence a statement returns to the ODBC client: a row count
/// (each count-producing T-SQL statement while NOCOUNT is OFF), a result set, or an error
/// raised by a statement inside a procedure.
struct Step
{
	enum class Type : uint8_t
	{
		RowCount,
		Rows,
		Error
	};

	Type Kind     = Type::RowCount;
	int64_t Count = -1;
	RowSet Data;
	std::string SqlState;
	std::string Message;
	long Native = 0;
};

/// \brief localtime_r / localtime_s.
std::tm LocalTime(std::time_t when);

/// \brief SQL Server's datetime text form as stored in the SQLite database:
/// "YYYY-MM-DD HH:MM:SS.fff". smalldatetime values carry ":00.000" seconds.
std::string FormatDateTime(std::time_t when, int milliseconds, bool smallDateTime);

/// \brief GETDATE(): local time, rounded the way the given column type stores it.
std::string GetDate(bool smallDateTime);

/// \brief Parses the stored datetime text; returns false if it isn't one.
bool ParseDateTime(std::string_view text, int& year, int& month, int& day, int& hour, int& minute,
	int& second, int& millisecond);

/// \brief Resolves the database file for an ODBC data source name.
/// OPENKO_SQLITE_DB (if set) wins; otherwise a DSN that looks like a path is used as-is,
/// and anything else maps to "<DSN>.sqlite" in the working directory.
std::string ResolveDatabasePath(std::string_view dsn);

/// \brief Opens a database file read-write, registers the collations and applies the
/// connection pragmas. Throws nanodbc::database_error.
sqlite3* OpenDatabase(const std::string& path);

/// \brief Throws nanodbc::database_error for a failed SQLite call, mapped to the SQLSTATE /
/// native error SQL Server would report where the game cares (constraint violations).
[[noreturn]] void ThrowSqliteError(sqlite3* db, int rc, std::string_view context);

} // namespace db::sqlite

#endif // DBLIBRARY_SQLITE_BACKEND_H
