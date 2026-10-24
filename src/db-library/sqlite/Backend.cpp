#include "Backend.h"
#include "Collation.h"

#include <nanodbc/nanodbc.h>

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <string>

#include <sqlite3.h>

namespace db::sqlite
{

std::tm LocalTime(std::time_t when)
{
	std::tm parts {};
#if defined(_WIN32)
	localtime_s(&parts, &when);
#else
	localtime_r(&when, &parts);
#endif
	return parts;
}

std::string FormatDateTime(std::time_t when, int milliseconds, bool smallDateTime)
{
	if (smallDateTime)
	{
		// smalldatetime is accurate to the minute; 29.998s and up rounds to the next minute.
		std::tm parts = LocalTime(when);
		if (parts.tm_sec >= 30 || (parts.tm_sec == 29 && milliseconds >= 998))
			parts = LocalTime(when + 60);
		return std::format("{:04}-{:02}-{:02} {:02}:{:02}:00.000", parts.tm_year + 1900,
			parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min);
	}

	// datetime stores 1/300 s ticks, so milliseconds always end in 0, 3 or 7.
	int ticks = (milliseconds * 3 + 5) / 10;
	if (ticks >= 300)
	{
		ticks = 0;
		++when;
	}

	std::tm parts = LocalTime(when);
	return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", parts.tm_year + 1900,
		parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
		(ticks * 10 + 1) / 3);
}

std::string GetDate(bool smallDateTime)
{
	auto now          = std::chrono::system_clock::now();
	auto sinceEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch())
							.count();
	auto secs = static_cast<std::time_t>(sinceEpochMs / 1000);
	return FormatDateTime(secs, static_cast<int>(sinceEpochMs % 1000), smallDateTime);
}

namespace
{

// Reads an unsigned decimal field of at most maxDigits digits at text[pos].
bool ReadField(std::string_view text, size_t& pos, int maxDigits, int& out)
{
	size_t end = pos;
	while (end < text.size() && end - pos < static_cast<size_t>(maxDigits) && text[end] >= '0'
		   && text[end] <= '9')
		++end;
	if (end == pos)
		return false;
	std::from_chars(text.data() + pos, text.data() + end,
		out); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	pos = end;
	return true;
}

bool Expect(std::string_view text, size_t& pos, char separator)
{
	if (pos >= text.size() || (text[pos] != separator && !(separator == ' ' && text[pos] == 'T')))
		return false;
	++pos;
	return true;
}

} // namespace

bool ParseDateTime(std::string_view text, int& year, int& month, int& day, int& hour, int& minute,
	int& second, int& millisecond)
{
	year = month = day = hour = minute = second = millisecond = 0;
	size_t pos                                                = 0;
	if (!ReadField(text, pos, 4, year) || !Expect(text, pos, '-') || !ReadField(text, pos, 2, month)
		|| !Expect(text, pos, '-') || !ReadField(text, pos, 2, day))
		return false;

	// The time part is optional; each component is too.
	if (!Expect(text, pos, ' ') || !ReadField(text, pos, 2, hour) || !Expect(text, pos, ':')
		|| !ReadField(text, pos, 2, minute))
		return true;
	if (!Expect(text, pos, ':') || !ReadField(text, pos, 2, second))
		return true;
	if (!Expect(text, pos, '.'))
		return true;

	int digits = 0, fraction = 0;
	while (pos < text.size() && digits < 3 && text[pos] >= '0' && text[pos] <= '9')
	{
		fraction = fraction * 10 + (text[pos] - '0');
		++pos;
		++digits;
	}
	for (; digits < 3; digits++)
		fraction *= 10;
	millisecond = fraction;
	return true;
}

std::string ResolveDatabasePath(std::string_view dsn)
{
	if (const char* overridePath = std::getenv("OPENKO_SQLITE_DB");
		overridePath != nullptr && *overridePath != '\0')
		return overridePath;

	std::string name(dsn);
	auto endsWith = [&](std::string_view suffix)
	{
		return name.size() >= suffix.size()
			   && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
	};

	if (name.find('/') != std::string::npos || endsWith(".sqlite") || endsWith(".db"))
		return name;

	return name + ".sqlite";
}

sqlite3* OpenDatabase(const std::string& path)
{
	sqlite3* db = nullptr;
	int rc      = sqlite3_open_v2(
        path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr);
	if (rc != SQLITE_OK)
	{
		std::string message = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
		sqlite3_close_v2(db);
		throw nanodbc::database_error(
			"08001", "[SQLite] cannot open database file '" + path + "': " + message, rc);
	}

	sqlite3_extended_result_codes(db, 1);
	sqlite3_busy_timeout(db, 10000);
	RegisterCollations(db);

	char* error = nullptr;
	rc          = sqlite3_exec(
        db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", nullptr, nullptr, &error);
	if (rc != SQLITE_OK)
	{
		std::string message = error != nullptr ? error : sqlite3_errstr(rc);
		sqlite3_free(error);
		sqlite3_close_v2(db);
		throw nanodbc::database_error(
			"08001", "[SQLite] cannot configure '" + path + "': " + message, rc);
	}

	return db;
}

void ThrowSqliteError(sqlite3* db, int rc, std::string_view context)
{
	std::string message = std::string("[SQLite] ") + std::string(context) + ": "
						  + sqlite3_errmsg(db);
	switch (rc)
	{
		case SQLITE_CONSTRAINT_PRIMARYKEY:
			throw nanodbc::database_error("23000", message, 2627);
		case SQLITE_CONSTRAINT_UNIQUE:
			throw nanodbc::database_error("23000", message, 2601);
		case SQLITE_CONSTRAINT_NOTNULL:
			throw nanodbc::database_error("23000", message, 515);
		case SQLITE_CONSTRAINT_CHECK:
			// Schema CHECKs are named len_* (SQL Server: string truncation) or range_* (overflow).
			if (message.find("len_") != std::string::npos)
				throw nanodbc::database_error("42000", message, 2628);
			throw nanodbc::database_error("22003", message, 220);
		case SQLITE_BUSY:
		case SQLITE_BUSY_SNAPSHOT:
		case SQLITE_BUSY_TIMEOUT:
		case SQLITE_LOCKED:
			throw nanodbc::database_error("HYT00", message, 1222);
		default:
			throw nanodbc::database_error("HY000", message, rc);
	}
}

} // namespace db::sqlite
