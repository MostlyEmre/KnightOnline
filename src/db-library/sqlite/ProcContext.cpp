#include "ProcContext.h"

#include <nanodbc/nanodbc.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <sqlite3.h>

namespace db::sqlite
{

namespace
{

// sqlite3.h defines SQLITE_TRANSIENT as a C-style cast of -1 to a destructor pointer.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
const sqlite3_destructor_type Transient     = SQLITE_TRANSIENT;

// Whether "SELECT @var = col FROM ..." hands the client a row count, as INSERT/UPDATE/DELETE
// do while NOCOUNT is OFF. Measured on rig B (SQL Server 2022, ODBC Driver 18): it doesn't.
constexpr bool AssignSelectProducesRowCount = false;

void BindValue(sqlite3_stmt* stmt, int index, const Value& value)
{
	switch (value.Kind)
	{
		case Value::Type::Null:
			sqlite3_bind_null(stmt, index);
			break;
		case Value::Type::Integer:
			sqlite3_bind_int64(stmt, index, value.Int);
			break;
		case Value::Type::Real:
			sqlite3_bind_double(stmt, index, value.Real);
			break;
		case Value::Type::Text:
			sqlite3_bind_text(
				stmt, index, value.Bytes.data(), static_cast<int>(value.Bytes.size()), Transient);
			break;
		case Value::Type::Blob:
			sqlite3_bind_blob(
				stmt, index, value.Bytes.data(), static_cast<int>(value.Bytes.size()), Transient);
			break;
	}
}

bool IsConstraintError(int rc)
{
	return (rc & 0xFF) == SQLITE_CONSTRAINT;
}

// The error SQL Server raises for the equivalent constraint failure.
void ConstraintStep(ProcContext& ctx, sqlite3* db, int rc, int& nativeOut)
{
	std::string detail = sqlite3_errmsg(db);
	switch (rc)
	{
		case SQLITE_CONSTRAINT_PRIMARYKEY:
			nativeOut = 2627;
			ctx.Error("23000", nativeOut, "Violation of PRIMARY KEY constraint (" + detail + ")");
			break;
		case SQLITE_CONSTRAINT_UNIQUE:
			nativeOut = 2601;
			ctx.Error("23000", nativeOut, "Cannot insert duplicate key row (" + detail + ")");
			break;
		case SQLITE_CONSTRAINT_NOTNULL:
			nativeOut = 515;
			ctx.Error("23000", nativeOut, "Cannot insert the value NULL (" + detail + ")");
			break;
		default:
			if (detail.find("len_") != std::string::npos)
			{
				nativeOut = 2628;
				ctx.Error("42000", nativeOut, // ODBC Driver 18 reports 2628 as 42000 (rig B)
					"String or binary data would be truncated (" + detail + ")");
			}
			else
			{
				nativeOut = 220;
				ctx.Error("22003", nativeOut, "Arithmetic overflow error (" + detail + ")");
			}
			break;
	}
}

} // namespace

// --- Query ----------------------------------------------------------------------------

Query::Query(sqlite3* db, std::string_view sql) : _db(db)
{
	int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &_stmt, nullptr);
	if (rc != SQLITE_OK)
		ThrowSqliteError(db, rc, "prepare");
}

Query::~Query()
{
	sqlite3_finalize(_stmt);
}

Query& Query::Bind(std::initializer_list<Value> values)
{
	int index = 1;
	for (const Value& value : values)
		BindValue(_stmt, index++, value);
	return *this;
}

bool Query::Step()
{
	int rc = sqlite3_step(_stmt);
	if (rc == SQLITE_ROW)
		return true;
	if (rc == SQLITE_DONE)
		return false;
	if (IsConstraintError(rc))
	{
		_error = rc;
		return false;
	}
	ThrowSqliteError(_db, rc, "step");
}

int64_t Query::Run()
{
	while (Step())
	{
	}
	if (_error != 0)
		return -1;
	return sqlite3_changes64(_db);
}

Value Query::Column(int index) const
{
	switch (sqlite3_column_type(_stmt, index))
	{
		case SQLITE_INTEGER:
			return Value::Integer(sqlite3_column_int64(_stmt, index));
		case SQLITE_FLOAT:
			return Value::Float(sqlite3_column_double(_stmt, index));
		case SQLITE_TEXT:
			return Value::Text(
				std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(_stmt, index)),
					static_cast<size_t>(sqlite3_column_bytes(_stmt, index))));
		case SQLITE_BLOB:
		{
			const void* data = sqlite3_column_blob(_stmt, index);
			size_t size      = static_cast<size_t>(sqlite3_column_bytes(_stmt, index));
			return Value::Blob(std::string_view(static_cast<const char*>(data), size));
		}
		default:
			return Value::Null();
	}
}

int Query::ColumnCount() const
{
	return sqlite3_column_count(_stmt);
}

std::string Query::ColumnName(int index) const
{
	const char* name = sqlite3_column_name(_stmt, index);
	return name != nullptr ? name : "";
}

// --- ProcContext ------------------------------------------------------------------------

ProcContext::ProcContext(sqlite3* db, std::vector<Value> args) : _db(db), _args(std::move(args))
{
}

std::optional<std::string> ProcContext::ArgText(size_t index, size_t maxLength) const
{
	if (index >= _args.size() || _args[index].IsNull())
		return std::nullopt;

	const Value& value = _args[index];
	std::string text;
	if (value.Kind == Value::Type::Integer)
		text = std::to_string(value.Int);
	else if (value.Kind == Value::Type::Real)
		text = std::to_string(value.Real);
	else
		text = value.Bytes;

	if (text.size() > maxLength)
		text.resize(maxLength);
	return text;
}

std::optional<int64_t> ProcContext::ArgInt(size_t index) const
{
	if (index >= _args.size() || _args[index].IsNull())
		return std::nullopt;

	const Value& value = _args[index];
	if (value.Kind == Value::Type::Integer)
		return value.Int;
	if (value.Kind == Value::Type::Real)
		return static_cast<int64_t>(value.Real);
	return std::strtoll(value.Bytes.c_str(), nullptr, 10);
}

void ProcContext::SetOutput(size_t index, Value value)
{
	_outputs[index] = std::move(value);
}

void ProcContext::SetReturn(int64_t value)
{
	_return = value;
}

void ProcContext::RowCount(int64_t count)
{
	Step step;
	step.Kind  = Step::Type::RowCount;
	step.Count = count;
	_steps.push_back(std::move(step));
}

void ProcContext::Rows(RowSet rows)
{
	Step step;
	step.Kind  = Step::Type::Rows;
	step.Count = static_cast<int64_t>(rows.Rows.size());
	step.Data  = std::move(rows);
	_steps.push_back(std::move(step));
}

void ProcContext::Error(std::string sqlState, long native, const std::string& message)
{
	Step step;
	step.Kind     = Step::Type::Error;
	step.SqlState = std::move(sqlState);
	step.Native   = native;
	step.Message  = "[SQLite][proc] " + message;
	_steps.push_back(std::move(step));
}

void ProcContext::BeginTran()
{
	char* error = nullptr;
	int rc      = sqlite3_exec(_db, "BEGIN IMMEDIATE", nullptr, nullptr, &error);
	sqlite3_free(error);
	if (rc != SQLITE_OK)
		ThrowSqliteError(_db, rc, "BEGIN TRAN");
	_inTransaction = true;
}

void ProcContext::CommitTran()
{
	if (!_inTransaction)
		return;
	char* error = nullptr;
	int rc      = sqlite3_exec(_db, "COMMIT", nullptr, nullptr, &error);
	sqlite3_free(error);
	_inTransaction = false;
	if (rc != SQLITE_OK)
		ThrowSqliteError(_db, rc, "COMMIT TRAN");
}

void ProcContext::RollbackTran()
{
	if (!_inTransaction)
		return;
	sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, nullptr);
	_inTransaction = false;
}

int ProcContext::Dml(std::string_view sql, std::initializer_list<Value> params, int64_t* rowCount)
{
	Query query(_db, sql);
	query.Bind(params);
	int64_t changes = query.Run();
	if (query.Error() != 0)
	{
		int native = 0;
		ConstraintStep(*this, _db, query.Error(), native);
		if (rowCount != nullptr)
			*rowCount = 0;
		return native;
	}

	RowCount(changes);
	if (rowCount != nullptr)
		*rowCount = changes;
	return 0;
}

std::optional<std::vector<Value>> ProcContext::AssignSelect(
	std::string_view sql, std::initializer_list<Value> params, int64_t* rowCount)
{
	Query query(_db, sql);
	query.Bind(params);

	std::optional<std::vector<Value>> last;
	int64_t count = 0;
	while (query.Step())
	{
		std::vector<Value> row;
		row.reserve(static_cast<size_t>(query.ColumnCount()));
		for (int i = 0; i < query.ColumnCount(); i++)
			row.push_back(query.Column(i));
		last = std::move(row);
		++count;
	}

	if constexpr (AssignSelectProducesRowCount)
		RowCount(count);

	if (rowCount != nullptr)
		*rowCount = count;
	return last;
}

Value ProcContext::Scalar(std::string_view sql, std::initializer_list<Value> params)
{
	Query query(_db, sql);
	query.Bind(params);
	if (!query.Step())
		return Value::Null();
	return query.Column(0);
}

RowSet ProcContext::Select(
	std::string_view sql, std::initializer_list<Value> params, std::vector<std::string> columnNames)
{
	Query query(_db, sql);
	query.Bind(params);

	RowSet rows;
	rows.Columns = std::move(columnNames);
	while (query.Step())
	{
		std::vector<Value> row;
		row.reserve(rows.Columns.size());
		for (int i = 0; i < query.ColumnCount(); i++)
			row.push_back(query.Column(i));
		rows.Rows.push_back(std::move(row));
	}
	return rows;
}

// --- registry ---------------------------------------------------------------------------

// Defined in Procedures*.cpp
void Proc_ACCOUNT_LOGIN(ProcContext& ctx);
void Proc_ACCOUNT_LOGOUT(ProcContext& ctx);
void Proc_CLAIM_USER_RANK_STIPEND(ProcContext& ctx);
void Proc_CREATE_KNIGHTS(ProcContext& ctx);
void Proc_CREATE_NEW_CHAR(ProcContext& ctx);
void Proc_DELETE_KNIGHTS(ProcContext& ctx);
void Proc_LOAD_ACCOUNT_CHARID(ProcContext& ctx);
void Proc_LOAD_CHAR_INFO(ProcContext& ctx);
void Proc_LOAD_KNIGHTS_MEMBERS(ProcContext& ctx);
void Proc_LOAD_PREMIUM_SERVICE_USER(ProcContext& ctx);
void Proc_LOAD_USER_DATA(ProcContext& ctx);
void Proc_NATION_SELECT(ProcContext& ctx);
void Proc_UPDATE_KNIGHTS(ProcContext& ctx);
void Proc_UPDATE_USER_DATA(ProcContext& ctx);
void Proc_UPDATE_WAREHOUSE(ProcContext& ctx);

namespace
{

struct ProcEntry
{
	const char* Name;
	ProcedureFn Fn;
};

constexpr ProcEntry Procedures[] = {
	{ "ACCOUNT_LOGIN", &Proc_ACCOUNT_LOGIN },
	{ "ACCOUNT_LOGOUT", &Proc_ACCOUNT_LOGOUT },
	{ "CLAIM_USER_RANK_STIPEND", &Proc_CLAIM_USER_RANK_STIPEND },
	{ "CREATE_KNIGHTS", &Proc_CREATE_KNIGHTS },
	{ "CREATE_NEW_CHAR", &Proc_CREATE_NEW_CHAR },
	{ "DELETE_KNIGHTS", &Proc_DELETE_KNIGHTS },
	{ "LOAD_ACCOUNT_CHARID", &Proc_LOAD_ACCOUNT_CHARID },
	{ "LOAD_CHAR_INFO", &Proc_LOAD_CHAR_INFO },
	{ "LOAD_KNIGHTS_MEMBERS", &Proc_LOAD_KNIGHTS_MEMBERS },
	{ "LOAD_PREMIUM_SERVICE_USER", &Proc_LOAD_PREMIUM_SERVICE_USER },
	{ "LOAD_USER_DATA", &Proc_LOAD_USER_DATA },
	{ "NATION_SELECT", &Proc_NATION_SELECT },
	{ "UPDATE_KNIGHTS", &Proc_UPDATE_KNIGHTS },
	{ "UPDATE_USER_DATA", &Proc_UPDATE_USER_DATA },
	{ "UPDATE_WAREHOUSE", &Proc_UPDATE_WAREHOUSE },
};

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
	return a.size() == b.size()
		   && std::equal(a.begin(), a.end(), b.begin(),
			   [](char x, char y)
			   {
				   return std::toupper(static_cast<unsigned char>(x))
						  == std::toupper(static_cast<unsigned char>(y));
			   });
}

} // namespace

ProcedureFn FindProcedure(std::string_view name)
{
	for (const ProcEntry& entry : Procedures)
	{
		if (EqualsIgnoreCase(entry.Name, name))
			return entry.Fn;
	}
	return nullptr;
}

std::vector<std::string> ProcedureNames()
{
	std::vector<std::string> names;
	for (const ProcEntry& entry : Procedures)
		names.emplace_back(entry.Name);
	return names;
}

} // namespace db::sqlite
