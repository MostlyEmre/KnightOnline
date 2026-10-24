#ifndef DBLIBRARY_SQLITE_PROCCONTEXT_H
#define DBLIBRARY_SQLITE_PROCCONTEXT_H

#pragma once

#include "Backend.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace db::sqlite
{

/// \brief A prepared SQLite statement used by procedure implementations.
class Query
{
public:
	Query(sqlite3* db, std::string_view sql);
	~Query();

	Query(const Query&)            = delete;
	Query& operator=(const Query&) = delete;

	/// \brief Binds parameters 1..N in order.
	Query& Bind(std::initializer_list<Value> values);

	/// \brief Steps once. Returns true when a row is available.
	/// Constraint violations are returned through Error() instead of throwing.
	bool Step();

	/// \brief Runs to completion and returns sqlite3_changes() (or -1 on a constraint error).
	int64_t Run();

	Value Column(int index) const;
	int ColumnCount() const;
	std::string ColumnName(int index) const;

	/// \brief The SQLite result code of the last failing Step(), or 0.
	int Error() const
	{
		return _error;
	}

private:
	sqlite3* _db;
	sqlite3_stmt* _stmt = nullptr;
	int _error          = 0;
};

/// \brief Everything a T-SQL procedure reimplementation needs: its arguments (in declared
/// parameter order), the database, and the sequence it returns to the client.
class ProcContext
{
public:
	ProcContext(sqlite3* db, std::vector<Value> args);

	sqlite3* Db() const
	{
		return _db;
	}

	// --- arguments, converted to the declared T-SQL parameter type -------------------

	/// \brief varchar(n) parameter: NULL stays empty; longer values are truncated to n bytes.
	std::optional<std::string> ArgText(size_t index, size_t maxLength) const;

	/// \brief Integer parameter (tinyint/smallint/int). NULL stays empty.
	std::optional<int64_t> ArgInt(size_t index) const;

	// --- what goes back to the client --------------------------------------------------

	void SetOutput(size_t index, Value value);
	void SetReturn(int64_t value);

	/// \brief A row count from a count-producing statement (NOCOUNT is OFF in every proc).
	void RowCount(int64_t count);

	/// \brief A result set.
	void Rows(RowSet rows);

	/// \brief An error raised by a statement; T-SQL carries on to the next statement.
	void Error(std::string sqlState, long native, const std::string& message);

	// --- transaction control ------------------------------------------------------------

	void BeginTran();
	void CommitTran();
	void RollbackTran();

	// --- statement helpers --------------------------------------------------------------

	/// \brief Runs a count-producing DML statement, emits its row count (or the error
	/// SQL Server would raise) and returns @@ERROR (0 on success).
	int Dml(std::string_view sql, std::initializer_list<Value> params, int64_t* rowCount = nullptr);

	/// \brief SELECT @var = ... FROM ...: returns the last matching row (T-SQL assigns once per
	/// row, so the last row wins), emits the row count, and reports @@ROWCOUNT.
	std::optional<std::vector<Value>> AssignSelect(
		std::string_view sql, std::initializer_list<Value> params, int64_t* rowCount = nullptr);

	/// \brief A scalar SELECT used inside an expression (no row count is produced).
	Value Scalar(std::string_view sql, std::initializer_list<Value> params);

	/// \brief Runs a SELECT and returns its rows as a result set with the given column names
	/// (the names SQL Server reports for the procedure's select list).
	RowSet Select(std::string_view sql, std::initializer_list<Value> params,
		std::vector<std::string> columnNames);

	std::vector<Step>& Steps()
	{
		return _steps;
	}

	const std::map<size_t, Value>& Outputs() const
	{
		return _outputs;
	}

	const std::optional<int64_t>& ReturnValue() const
	{
		return _return;
	}

private:
	sqlite3* _db;
	std::vector<Value> _args;
	std::vector<Step> _steps;
	std::map<size_t, Value> _outputs;
	std::optional<int64_t> _return;
	bool _inTransaction = false;
};

using ProcedureFn = void (*)(ProcContext&);

/// \brief Looks up a procedure reimplementation by its T-SQL name (case-insensitive).
ProcedureFn FindProcedure(std::string_view name);

/// \brief Names of all reimplemented procedures (for diagnostics and tests).
std::vector<std::string> ProcedureNames();

/// \brief Helpers for T-SQL three-valued logic on nullable values.
inline bool IsTrue(const std::optional<bool>& value)
{
	return value.has_value() && *value;
}

} // namespace db::sqlite

#endif // DBLIBRARY_SQLITE_PROCCONTEXT_H
