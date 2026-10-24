#include <nanodbc/nanodbc.h>

#include "Backend.h"
#include "ProcContext.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <regex>

#include <sqlite3.h>

namespace nanodbc
{

using db::sqlite::Step;
using db::sqlite::Value;

namespace
{
// sqlite3.h defines SQLITE_TRANSIENT as a C-style cast of -1 to a destructor pointer.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
const sqlite3_destructor_type Transient = SQLITE_TRANSIENT;
} // namespace

// --- database_error ---------------------------------------------------------------------

database_error::database_error(void* /*handle*/, short /*handle_type*/, const std::string& info) :
	std::runtime_error(info), _native(0), _state("HY000"), _message(info)
{
}

database_error::database_error(std::string sqlState, const std::string& message, long native) :
	std::runtime_error(message), _native(native), _state(std::move(sqlState)),
	_message(_state + ": " + std::to_string(native) + ": " + message)
{
}

const char* database_error::what() const noexcept
{
	return _message.c_str();
}

long database_error::native() const noexcept
{
	return _native;
}

const std::string& database_error::state() const noexcept
{
	return _state;
}

namespace detail
{

namespace
{

// SQL Server's ODBC driver reports -1 from SQLRowCount for a SELECT.
// Calibrated by the rig B parity probe.
constexpr long SelectAffectedRows = -1;

[[noreturn]] void ThrowInvalidCursorState()
{
	throw database_error("24000", "[SQLite] Invalid cursor state", 0);
}

[[noreturn]] void ThrowProgrammingError(const char* what)
{
	throw database_error("HY010", std::string("[SQLite] ") + what, 0);
}

std::string ExtractDsn(const std::string& connectionString)
{
	// "DSN=KN_online;UID=knight;PWD=knight;AutoTranslate=no"
	size_t pos = 0;
	while (pos < connectionString.size())
	{
		size_t end = connectionString.find(';', pos);
		if (end == std::string::npos)
			end = connectionString.size();
		std::string part = connectionString.substr(pos, end - pos);
		size_t eq        = part.find('=');
		if (eq != std::string::npos)
		{
			std::string key = part.substr(0, eq);
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			if (key == "DSN")
				return part.substr(eq + 1);
		}
		pos = end + 1;
	}
	// Not a connection string: treat the whole thing as the DSN.
	return connectionString;
}

Value ReadScalar(const void* ptr, int kind)
{
	// kind matches statement::scalar_kind
	switch (kind)
	{
		case 0:
			return Value::Integer(*static_cast<const int8_t*>(ptr));
		case 1:
			return Value::Integer(*static_cast<const uint8_t*>(ptr));
		case 2:
			return Value::Integer(*static_cast<const int16_t*>(ptr));
		case 3:
			return Value::Integer(*static_cast<const uint16_t*>(ptr));
		case 4:
			return Value::Integer(*static_cast<const int32_t*>(ptr));
		case 5:
			return Value::Integer(*static_cast<const uint32_t*>(ptr));
		case 6:
			return Value::Integer(*static_cast<const int64_t*>(ptr));
		case 7:
			return Value::Integer(static_cast<int64_t>(*static_cast<const uint64_t*>(ptr)));
		case 8:
			return Value::Float(*static_cast<const float*>(ptr));
		default:
			return Value::Float(*static_cast<const double*>(ptr));
	}
}

void WriteScalar(void* ptr, int kind, const Value& value)
{
	int64_t i = 0;
	double d  = 0.0;
	switch (value.Kind)
	{
		case Value::Type::Integer:
			i = value.Int;
			d = static_cast<double>(value.Int);
			break;
		case Value::Type::Real:
			i = static_cast<int64_t>(value.Real);
			d = value.Real;
			break;
		case Value::Type::Text:
		case Value::Type::Blob:
			i = std::strtoll(value.Bytes.c_str(), nullptr, 10);
			d = std::strtod(value.Bytes.c_str(), nullptr);
			break;
		case Value::Type::Null:
			return; // ODBC leaves the buffer untouched for a NULL output
	}

	switch (kind)
	{
		case 0:
			*static_cast<int8_t*>(ptr) = static_cast<int8_t>(i);
			break;
		case 1:
			*static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(i);
			break;
		case 2:
			*static_cast<int16_t*>(ptr) = static_cast<int16_t>(i);
			break;
		case 3:
			*static_cast<uint16_t*>(ptr) = static_cast<uint16_t>(i);
			break;
		case 4:
			*static_cast<int32_t*>(ptr) = static_cast<int32_t>(i);
			break;
		case 5:
			*static_cast<uint32_t*>(ptr) = static_cast<uint32_t>(i);
			break;
		case 6:
			*static_cast<int64_t*>(ptr) = i;
			break;
		case 7:
			*static_cast<uint64_t*>(ptr) = static_cast<uint64_t>(i);
			break;
		case 8:
			*static_cast<float*>(ptr) = static_cast<float>(d);
			break;
		default:
			*static_cast<double*>(ptr) = d;
			break;
	}
}

} // namespace

// ODBC SQL type codes (sql.h / sqlext.h), as SQL Server's driver reports them.
constexpr int SqlChar        = 1;
constexpr int SqlVarchar     = 12;
constexpr int SqlLongVarchar = -1;
constexpr int SqlBit         = -7;
constexpr int SqlTinyInt     = -6;
constexpr int SqlSmallInt    = 5;
constexpr int SqlInteger     = 4;
constexpr int SqlBigInt      = -5;
constexpr int SqlDouble      = 8;
constexpr int SqlVarBinary   = -3;

int DatatypeFromDeclared(std::string type)
{
	std::transform(type.begin(), type.end(), type.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (type.starts_with("tinyint"))
		return SqlTinyInt;
	if (type.starts_with("smallint"))
		return SqlSmallInt;
	if (type.starts_with("bigint"))
		return SqlBigInt;
	if (type.starts_with("int") || type == "integer")
		return SqlInteger;
	if (type.starts_with("bit"))
		return SqlBit;
	if (type.starts_with("float") || type.starts_with("real"))
		return SqlDouble;
	if (type.starts_with("varchar"))
		return SqlVarchar;
	if (type.starts_with("char"))
		return SqlChar;
	if (type.starts_with("text") || type.starts_with("ntext"))
		return SqlLongVarchar;
	if (type == "blob")
		return SqlVarBinary;
	return 0;
}

int DatatypeFromValue(const Value& v)
{
	switch (v.Kind)
	{
		case Value::Type::Integer:
			return SqlInteger;
		case Value::Type::Real:
			return SqlDouble;
		case Value::Type::Blob:
			return SqlVarBinary;
		default:
			return SqlVarchar;
	}
}

class connection_impl
{
public:
	~connection_impl()
	{
		disconnect();
	}

	void connect(const std::string& dsn)
	{
		disconnect();
		_path = db::sqlite::ResolveDatabasePath(dsn);
		_db   = db::sqlite::OpenDatabase(_path);
	}

	void disconnect()
	{
		if (_db != nullptr)
		{
			sqlite3_close_v2(_db);
			_db = nullptr;
		}
	}

	bool connected() const
	{
		return _db != nullptr;
	}

	sqlite3* db() const
	{
		if (_db == nullptr)
			throw database_error("08003", "[SQLite] connection is not open", 0);
		return _db;
	}

	const std::string& path() const
	{
		return _path;
	}

private:
	sqlite3* _db = nullptr;
	std::string _path;
};

struct bound_param
{
	enum class kind : uint8_t
	{
		none,
		scalar,
		cstring,
		owned_text,
		binary_ptr,
		owned_binary,
		null
	};

	kind Kind                            = kind::none;
	statement::param_direction Direction = statement::PARAM_IN;
	const void* Ptr                      = nullptr;
	int ScalarKind                       = 0;
	size_t Size                          = 0;
	std::string Owned;

	Value Read() const
	{
		switch (Kind)
		{
			case kind::scalar:
				return ReadScalar(Ptr, ScalarKind);
			case kind::cstring:
				return Ptr != nullptr ? Value::Text(static_cast<const char*>(Ptr)) : Value::Null();
			case kind::owned_text:
				return Value::Text(Owned);
			// nanodbc sends an empty binary as a zero length with a null buffer, which
			// SQL Server stores as NULL (measured on rig B).
			case kind::binary_ptr:
				return Size > 0 ? Value::Blob(std::string_view(static_cast<const char*>(Ptr), Size))
								: Value::Null();
			case kind::owned_binary:
				return !Owned.empty() ? Value::Blob(Owned) : Value::Null();
			default:
				return Value::Null();
		}
	}
};

class result_impl
{
public:
	virtual ~result_impl()                              = default;

	virtual long affected_rows() const                  = 0;
	virtual short columns() const                       = 0;
	virtual std::string column_name(short column) const = 0;
	virtual bool next()                                 = 0;
	virtual bool next_result()                          = 0;
	virtual bool at_end() const                         = 0;
	virtual bool first()                                = 0;
	virtual const Value& value(short column) const      = 0;

	/// \brief ODBC SQL type code of a column.
	virtual int datatype(short column) const            = 0;

	/// \brief Whether the column is long data (text/ntext), which nanodbc reads with
	/// SQLGetData and lengths; ordinary character columns are bound and read as C strings.
	virtual bool is_long(short /*column*/) const
	{
		return false;
	}

	short column_index(const std::string& name) const
	{
		short n = columns();
		for (short i = 0; i < n; i++)
		{
			if (column_name(i) == name)
				return i;
		}
		for (short i = 0; i < n; i++)
		{
			std::string candidate = column_name(i);
			if (candidate.size() == name.size()
				&& std::equal(candidate.begin(), candidate.end(), name.begin(),
					[](char a, char b)
					{
						return std::tolower(static_cast<unsigned char>(a))
							   == std::tolower(static_cast<unsigned char>(b));
					}))
				return i;
		}
		throw database_error("07009", "[SQLite] invalid column name: " + name, 0);
	}
};

class statement_impl
{
public:
	std::shared_ptr<connection_impl> Conn;
	std::string Query;
	bool Opened      = false;

	// Parsed form
	bool IsProcedure = false;
	bool HasReturn   = false;
	std::string ProcedureName;
	std::string Sql;

	std::vector<bound_param> Params;
	sqlite3_stmt* Stmt = nullptr;

	~statement_impl()
	{
		finalize();
	}

	void finalize()
	{
		if (Stmt != nullptr)
		{
			sqlite3_finalize(Stmt);
			Stmt = nullptr;
		}
	}

	void set_query(const std::string& query)
	{
		finalize();
		Query = query;
		Params.clear();
		parse();
	}

	bound_param& param(short index)
	{
		if (index < 0)
			throw database_error("07009", "[SQLite] invalid parameter index", 0);
		if (static_cast<size_t>(index) >= Params.size())
			Params.resize(static_cast<size_t>(index) + 1);
		return Params[static_cast<size_t>(index)];
	}

private:
	void parse()
	{
		static const std::regex callPattern(
			R"(^\s*\{\s*(\?\s*=\s*)?call\s+([^\s(}]+)\s*(\((.*)\))?\s*\}\s*$)", std::regex::icase);
		static const std::regex topPattern(
			R"(^(\s*select\s+)top\s+\(?\s*(\d+)\s*\)?\s+)", std::regex::icase);

		std::smatch match;
		if (std::regex_match(Query, match, callPattern))
		{
			IsProcedure   = true;
			HasReturn     = match[1].matched;
			std::string n = match[2].str();
			// [dbo].[NAME] / dbo.NAME / [NAME] -> NAME
			n.erase(std::remove(n.begin(), n.end(), '['), n.end());
			n.erase(std::remove(n.begin(), n.end(), ']'), n.end());
			size_t dot = n.rfind('.');
			if (dot != std::string::npos)
				n = n.substr(dot + 1);
			ProcedureName = n;
			return;
		}

		IsProcedure = false;
		Sql         = Query;
		if (std::regex_search(Sql, match, topPattern))
		{
			// SELECT TOP n ... -> SELECT ... LIMIT n
			std::string limit = match[2].str();
			Sql               = match[1].str() + Sql.substr(static_cast<size_t>(match.length(0)));
			while (!Sql.empty()
				   && (Sql.back() == ';' || std::isspace(static_cast<unsigned char>(Sql.back()))))
				Sql.pop_back();
			Sql += " LIMIT " + limit;
		}
	}
};

// Rows streamed straight from a prepared SQLite statement (plain SELECTs).
class live_result : public result_impl
{
public:
	explicit live_result(std::shared_ptr<statement_impl> stmt) : _stmt(std::move(stmt))
	{
		sqlite3_stmt* s = _stmt->Stmt;
		int n           = sqlite3_column_count(s);
		_columns.reserve(static_cast<size_t>(n));
		for (int i = 0; i < n; i++)
		{
			const char* name = sqlite3_column_name(s, i);
			_columns.emplace_back(name != nullptr ? name : "");

			const char* decl = sqlite3_column_decltype(s, i);
			std::string type = decl != nullptr ? decl : "";
			_types.push_back(DatatypeFromDeclared(type));
			std::transform(type.begin(), type.end(), type.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			_long.push_back(type.starts_with("text") || type.starts_with("ntext"));
		}
		_row.resize(static_cast<size_t>(n));

		// SQL Server reports a row count of 0 up front for an empty result set and -1 while
		// rows are pending (measured on rig B), so look one row ahead.
		_pending = fetch();
		_empty   = !_pending;
	}

	long affected_rows() const override
	{
		return _empty ? 0 : SelectAffectedRows;
	}

	bool is_long(short column) const override
	{
		check_column(column);
		return _long[static_cast<size_t>(column)];
	}

	int datatype(short column) const override
	{
		check_column(column);
		int type = _types[static_cast<size_t>(column)];
		return type != 0
				   ? type
				   : (_hasRow || _pending ? DatatypeFromValue(_row[static_cast<size_t>(column)])
										  : SqlVarchar);
	}

	short columns() const override
	{
		return static_cast<short>(_columns.size());
	}

	std::string column_name(short column) const override
	{
		check_column(column);
		return _columns[static_cast<size_t>(column)];
	}

	bool next() override
	{
		if (_pending)
		{
			_pending = false;
			_hasRow  = true;
			return true;
		}
		_hasRow = fetch();
		return _hasRow;
	}

	bool next_result() override
	{
		return false;
	}

	bool at_end() const override
	{
		return _done;
	}

	bool first() override
	{
		if (_hasRow || (_done && !_pending))
			ThrowProgrammingError("first() on a forward-only result");
		return next();
	}

	const Value& value(short column) const override
	{
		check_column(column);
		if (!_hasRow)
			ThrowInvalidCursorState();
		return _row[static_cast<size_t>(column)];
	}

private:
	// Steps the statement into _row; false at the end.
	bool fetch()
	{
		if (_done)
			return false;

		sqlite3_stmt* s = _stmt->Stmt;
		int rc          = sqlite3_step(s);
		if (rc == SQLITE_ROW)
		{
			for (size_t i = 0; i < _row.size(); i++)
				_row[i] = read_column(s, static_cast<int>(i));
			return true;
		}

		_done = true;
		if (rc != SQLITE_DONE)
			db::sqlite::ThrowSqliteError(sqlite3_db_handle(s), rc, "fetch");
		return false;
	}

	static Value read_column(sqlite3_stmt* s, int i)
	{
		switch (sqlite3_column_type(s, i))
		{
			case SQLITE_INTEGER:
				return Value::Integer(sqlite3_column_int64(s, i));
			case SQLITE_FLOAT:
				return Value::Float(sqlite3_column_double(s, i));
			case SQLITE_TEXT:
				return Value::Text(
					std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(s, i)),
						static_cast<size_t>(sqlite3_column_bytes(s, i))));
			case SQLITE_BLOB:
				return Value::Blob(
					std::string_view(static_cast<const char*>(sqlite3_column_blob(s, i)),
						static_cast<size_t>(sqlite3_column_bytes(s, i))));
			default:
				return Value::Null();
		}
	}

	void check_column(short column) const
	{
		if (column < 0 || static_cast<size_t>(column) >= _columns.size())
			throw database_error("07009", "[SQLite] column index out of range", 0);
	}

	std::shared_ptr<statement_impl> _stmt;
	std::vector<std::string> _columns;
	std::vector<Value> _row;
	std::vector<bool> _long;
	std::vector<int> _types;
	bool _hasRow  = false;
	bool _pending = false;
	bool _empty   = false;
	bool _done    = false;
};

// The ODBC result sequence of a procedure call or a DML statement: row counts, result
// sets and errors, with output parameters written once the sequence is drained.
class sequence_result : public result_impl
{
public:
	// The caller's output buffer (as ODBC binds it) and the value it receives.
	struct pending_output
	{
		void* Ptr      = nullptr;
		int ScalarKind = 0;
		Value Data;
	};

	sequence_result(std::shared_ptr<statement_impl> stmt, std::vector<Step> steps,
		std::vector<pending_output> outputs) :
		_stmt(std::move(stmt)), _steps(std::move(steps)), _outputs(std::move(outputs))
	{
		if (_steps.empty())
			apply_outputs();
		else if (_steps.front().Kind == Step::Type::Error)
			throw_step(_steps.front());
	}

	long affected_rows() const override
	{
		// A procedure that produced nothing at all leaves no cursor, so SQLRowCount fails
		// too (measured on rig B).
		if (_steps.empty())
			ThrowInvalidCursorState();
		if (_index >= _steps.size())
			return -1;
		const Step& step = _steps[_index];
		if (step.Kind == Step::Type::Rows)
			return step.Data.Rows.empty() ? 0 : SelectAffectedRows;
		return static_cast<long>(step.Count);
	}

	short columns() const override
	{
		if (_steps.empty())
			ThrowInvalidCursorState();
		const Step* step = current_rows();
		return step != nullptr ? static_cast<short>(step->Data.Columns.size()) : 0;
	}

	int datatype(short column) const override
	{
		const Step* step = current_rows();
		if (step == nullptr || column < 0
			|| static_cast<size_t>(column) >= step->Data.Columns.size())
			throw database_error("07009", "[SQLite] column index out of range", 0);
		for (const auto& row : step->Data.Rows)
		{
			if (!row[static_cast<size_t>(column)].IsNull())
				return DatatypeFromValue(row[static_cast<size_t>(column)]);
		}
		return SqlVarchar;
	}

	std::string column_name(short column) const override
	{
		const Step* step = current_rows();
		if (step == nullptr || column < 0
			|| static_cast<size_t>(column) >= step->Data.Columns.size())
			throw database_error("07009", "[SQLite] column index out of range", 0);
		return step->Data.Columns[static_cast<size_t>(column)];
	}

	bool next() override
	{
		const Step* step = current_rows();
		if (step == nullptr)
			ThrowInvalidCursorState();
		if (_row + 1 < static_cast<int64_t>(step->Data.Rows.size()))
		{
			++_row;
			return true;
		}
		_row = static_cast<int64_t>(step->Data.Rows.size());
		return false;
	}

	bool next_result() override
	{
		if (_index + 1 < _steps.size())
		{
			++_index;
			_row = -1;
			if (_steps[_index].Kind == Step::Type::Error)
				throw_step(_steps[_index]);
			return true;
		}
		_index = _steps.size();
		apply_outputs();
		return false;
	}

	bool at_end() const override
	{
		const Step* step = current_rows();
		return step == nullptr || _row >= static_cast<int64_t>(step->Data.Rows.size());
	}

	bool first() override
	{
		const Step* step = current_rows();
		if (step == nullptr)
			ThrowInvalidCursorState();
		_row = 0;
		return !step->Data.Rows.empty();
	}

	const Value& value(short column) const override
	{
		const Step* step = current_rows();
		if (step == nullptr || _row < 0 || _row >= static_cast<int64_t>(step->Data.Rows.size()))
			ThrowInvalidCursorState();
		const auto& row = step->Data.Rows[static_cast<size_t>(_row)];
		if (column < 0 || static_cast<size_t>(column) >= row.size())
			throw database_error("07009", "[SQLite] column index out of range", 0);
		return row[static_cast<size_t>(column)];
	}

private:
	const Step* current_rows() const
	{
		if (_index >= _steps.size() || _steps[_index].Kind != Step::Type::Rows)
			return nullptr;
		return &_steps[_index];
	}

	[[noreturn]] static void throw_step(const Step& step)
	{
		throw database_error(step.SqlState, step.Message, step.Native);
	}

	void apply_outputs()
	{
		if (_applied)
			return;
		_applied = true;
		for (const pending_output& out : _outputs)
		{
			if (out.Ptr != nullptr)
				WriteScalar(out.Ptr, out.ScalarKind, out.Data);
		}
	}

	std::shared_ptr<statement_impl> _stmt;
	std::vector<Step> _steps;
	std::vector<pending_output> _outputs;
	size_t _index = 0;
	int64_t _row  = -1;
	bool _applied = false;
};

} // namespace detail

// --- connection ---------------------------------------------------------------------------

connection::connection() : _impl(std::make_shared<detail::connection_impl>())
{
}

connection::connection(const string& connectionString, long timeout) : connection()
{
	connect(connectionString, timeout);
}

connection::connection(const string& dsn, const string& user, const string& pass, long timeout) :
	connection()
{
	connect(dsn, user, pass, timeout);
}

connection::~connection() noexcept = default;

void connection::connect(const string& connectionString, long /*timeout*/)
{
	_impl->connect(detail::ExtractDsn(connectionString));
}

void connection::connect(
	const string& dsn, const string& /*user*/, const string& /*pass*/, long /*timeout*/)
{
	_impl->connect(dsn);
}

bool connection::connected() const
{
	return _impl->connected();
}

void connection::disconnect()
{
	_impl->disconnect();
}

const string& connection::database_path() const
{
	return _impl->path();
}

// --- statement ----------------------------------------------------------------------------

statement::statement() : _impl(std::make_shared<detail::statement_impl>())
{
}

statement::statement(class connection& conn) : statement()
{
	open(conn);
}

statement::statement(class connection& conn, const string& query, long timeout) : statement()
{
	prepare(conn, query, timeout);
}

statement::~statement() noexcept = default;

void statement::open(class connection& conn)
{
	_impl->finalize();
	_impl->Conn   = conn._impl;
	_impl->Opened = true;
}

bool statement::open() const
{
	return _impl->Opened;
}

void statement::close()
{
	_impl->finalize();
	_impl->Opened = false;
	_impl->Params.clear();
}

void statement::prepare(class connection& conn, const string& query, long timeout)
{
	open(conn);
	prepare(query, timeout);
}

void statement::prepare(const string& query, long /*timeout*/)
{
	if (_impl->Conn == nullptr)
		throw database_error("HY010", "[SQLite] prepare() without a connection", 0);
	_impl->set_query(query);
}

result statement::execute(long /*batchOperations*/, long /*timeout*/)
{
	detail::statement_impl& s = *_impl;
	if (s.Conn == nullptr)
		throw database_error("HY010", "[SQLite] execute() without a connection", 0);
	sqlite3* db = s.Conn->db();

	if (s.IsProcedure)
	{
		db::sqlite::ProcedureFn fn = db::sqlite::FindProcedure(s.ProcedureName);
		if (fn == nullptr)
			throw database_error("42000",
				"[SQLite] Could not find stored procedure '" + s.ProcedureName + "'.", 2812);

		// Parameter markers map onto procedure parameters in order; with "? = CALL" the
		// first marker is the return value.
		size_t first = s.HasReturn ? 1 : 0;
		std::vector<Value> args;
		for (size_t i = first; i < s.Params.size(); i++)
		{
			const detail::bound_param& p = s.Params[i];
			// Output-only parameters arrive in the procedure as NULL.
			args.push_back(p.Direction == PARAM_OUT ? Value::Null() : p.Read());
		}

		db::sqlite::ProcContext ctx(db, std::move(args));
		try
		{
			fn(ctx);
		}
		catch (...)
		{
			ctx.RollbackTran();
			throw;
		}
		ctx.RollbackTran(); // T-SQL leaves an open transaction to the caller; nothing here relies on it

		std::vector<detail::sequence_result::pending_output> outputs;
		auto addOutput = [&](size_t marker, Value data)
		{
			if (marker >= s.Params.size())
				return;
			const detail::bound_param& p = s.Params[marker];
			// nanodbc takes output buffers as const T* and writes through them; so do we.
			if (p.Kind == detail::bound_param::kind::scalar && p.Direction != PARAM_IN)
				outputs.push_back(
					{ const_cast<void*>(p.Ptr), // NOLINT(cppcoreguidelines-pro-type-const-cast)
						p.ScalarKind, std::move(data) });
		};
		for (const auto& [index, data] : ctx.Outputs())
			addOutput(index + first, data);
		if (s.HasReturn)
			addOutput(0, Value::Integer(ctx.ReturnValue().value_or(0)));

		return result(std::make_shared<detail::sequence_result>(
			_impl, std::move(ctx.Steps()), std::move(outputs)));
	}

	if (s.Stmt == nullptr)
	{
		int rc = sqlite3_prepare_v2(
			db, s.Sql.c_str(), static_cast<int>(s.Sql.size()), &s.Stmt, nullptr);
		if (rc != SQLITE_OK)
			db::sqlite::ThrowSqliteError(db, rc, "prepare '" + s.Sql + "'");
	}
	else
	{
		sqlite3_reset(s.Stmt);
	}

	sqlite3_clear_bindings(s.Stmt);
	for (size_t i = 0; i < s.Params.size(); i++)
	{
		Value v = s.Params[i].Read();
		int idx = static_cast<int>(i) + 1;
		switch (v.Kind)
		{
			case Value::Type::Null:
				sqlite3_bind_null(s.Stmt, idx);
				break;
			case Value::Type::Integer:
				sqlite3_bind_int64(s.Stmt, idx, v.Int);
				break;
			case Value::Type::Real:
				sqlite3_bind_double(s.Stmt, idx, v.Real);
				break;
			case Value::Type::Text:
				sqlite3_bind_text(
					s.Stmt, idx, v.Bytes.data(), static_cast<int>(v.Bytes.size()), Transient);
				break;
			case Value::Type::Blob:
				sqlite3_bind_blob(
					s.Stmt, idx, v.Bytes.data(), static_cast<int>(v.Bytes.size()), Transient);
				break;
		}
	}

	if (sqlite3_column_count(s.Stmt) > 0)
		return result(std::make_shared<detail::live_result>(_impl));

	// INSERT/UPDATE/DELETE: run now, report the row count.
	int rc = sqlite3_step(s.Stmt);
	while (rc == SQLITE_ROW)
		rc = sqlite3_step(s.Stmt);
	if (rc != SQLITE_DONE)
	{
		sqlite3_reset(s.Stmt);
		db::sqlite::ThrowSqliteError(db, rc, "execute");
	}

	Step step;
	step.Kind  = Step::Type::RowCount;
	step.Count = sqlite3_changes64(db);
	return result(
		std::make_shared<detail::sequence_result>(_impl, std::vector<Step> { std::move(step) },
			std::vector<detail::sequence_result::pending_output> {}));
}

void statement::just_execute(long batchOperations, long timeout)
{
	execute(batchOperations, timeout);
}

void statement::reset_parameters() noexcept
{
	_impl->Params.clear();
}

short statement::parameters() const
{
	if (_impl->Stmt != nullptr)
		return static_cast<short>(sqlite3_bind_parameter_count(_impl->Stmt));
	return static_cast<short>(_impl->Params.size());
}

void statement::bind_scalar(
	short paramIndex, const void* value, scalar_kind kind, param_direction direction)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind = value != nullptr ? detail::bound_param::kind::scalar : detail::bound_param::kind::null;
	p.Direction  = direction;
	p.Ptr        = value;
	p.ScalarKind = static_cast<int>(kind);
}

void statement::bind(short paramIndex, const char* value, param_direction direction)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind                 = value != nullptr ? detail::bound_param::kind::cstring
											  : detail::bound_param::kind::null;
	p.Direction            = direction;
	p.Ptr                  = value;
}

void statement::bind(short paramIndex, const std::string& value, param_direction direction)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind                 = detail::bound_param::kind::owned_text;
	p.Direction            = direction;
	p.Owned                = value;
}

void statement::bind_binary(
	short paramIndex, const std::vector<uint8_t>& value, param_direction direction)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind                 = detail::bound_param::kind::owned_binary;
	p.Direction            = direction;
	p.Owned.assign(reinterpret_cast<const char*>(value.data()), value.size());
}

void statement::bind_binary(
	short paramIndex, const uint8_t* value, std::size_t valueSize, param_direction direction)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind                 = detail::bound_param::kind::binary_ptr;
	p.Direction            = direction;
	p.Ptr                  = value;
	p.Size                 = valueSize;
}

void statement::bind_null(short paramIndex, std::size_t /*batchSize*/)
{
	detail::bound_param& p = _impl->param(paramIndex);
	p.Kind                 = detail::bound_param::kind::null;
	p.Direction            = PARAM_IN;
}

// --- result -------------------------------------------------------------------------------

result::result() = default;

result::result(std::shared_ptr<detail::result_impl> impl) : _impl(std::move(impl))
{
}

result::~result() noexcept            = default;

result::result(const result& rhs)     = default;

result::result(result&& rhs) noexcept = default;

result& result::operator=(result rhs)
{
	_impl = std::move(rhs._impl);
	return *this;
}

namespace
{
detail::result_impl& Impl(const std::shared_ptr<detail::result_impl>& impl)
{
	if (impl == nullptr)
		throw database_error("HY010", "[SQLite] empty result", 0);
	return *impl;
}
} // namespace

long result::affected_rows() const
{
	return Impl(_impl).affected_rows();
}

short result::columns() const
{
	return Impl(_impl).columns();
}

string result::column_name(short column) const
{
	return Impl(_impl).column_name(column);
}

int result::column_datatype(short column) const
{
	return Impl(_impl).datatype(column);
}

short result::column(const string& columnName) const
{
	return Impl(_impl).column_index(columnName);
}

bool result::next()
{
	return Impl(_impl).next();
}

bool result::next_result()
{
	return Impl(_impl).next_result();
}

bool result::first()
{
	return Impl(_impl).first();
}

bool result::end() const noexcept
{
	return _impl == nullptr || _impl->at_end();
}

bool result::is_null(short column) const
{
	return Impl(_impl).value(column).IsNull();
}

bool result::is_null(const string& columnName) const
{
	return is_null(column(columnName));
}

result::operator bool() const
{
	return _impl != nullptr;
}

int64_t result::get_int64(short column) const
{
	const Value& v = Impl(_impl).value(column);
	switch (v.Kind)
	{
		case Value::Type::Integer:
			return v.Int;
		case Value::Type::Real:
			return static_cast<int64_t>(v.Real);
		case Value::Type::Text:
		case Value::Type::Blob:
			return std::strtoll(v.Bytes.c_str(), nullptr, 10);
		default:
			return 0;
	}
}

double result::get_double(short column) const
{
	const Value& v = Impl(_impl).value(column);
	switch (v.Kind)
	{
		case Value::Type::Integer:
			return static_cast<double>(v.Int);
		case Value::Type::Real:
			return v.Real;
		case Value::Type::Text:
		case Value::Type::Blob:
			return std::strtod(v.Bytes.c_str(), nullptr);
		default:
			return 0.0;
	}
}

std::string result::get_string(short column) const
{
	const Value& v = Impl(_impl).value(column);
	switch (v.Kind)
	{
		case Value::Type::Integer:
			return std::to_string(v.Int);
		case Value::Type::Real:
			return std::format("{:.17g}", v.Real);
		case Value::Type::Text:
			// nanodbc reads bound character columns as C strings, so they end at the first NUL;
			// long-data (text) columns are read with lengths and keep every byte.
			if (!Impl(_impl).is_long(column))
				return v.Bytes.substr(0, v.Bytes.find('\0'));
			return v.Bytes;
		case Value::Type::Blob:
			return v.Bytes;
		default:
			return {};
	}
}

std::vector<uint8_t> result::get_bytes(short column) const
{
	const Value& v = Impl(_impl).value(column);
	if (v.Kind == Value::Type::Text || v.Kind == Value::Type::Blob)
		return std::vector<uint8_t>(v.Bytes.begin(), v.Bytes.end());
	if (v.Kind == Value::Type::Null)
		return {};
	std::string s = get_string(column);
	return std::vector<uint8_t>(s.begin(), s.end());
}

timestamp result::get_timestamp(short column) const
{
	const Value& v = Impl(_impl).value(column);
	timestamp ts {};
	if (v.Kind != Value::Type::Text)
		return ts;
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, ms = 0;
	if (!db::sqlite::ParseDateTime(v.Bytes, year, month, day, hour, minute, second, ms))
		return ts;
	ts.year  = static_cast<int16_t>(year);
	ts.month = static_cast<int16_t>(month);
	ts.day   = static_cast<int16_t>(day);
	ts.hour  = static_cast<int16_t>(hour);
	ts.min   = static_cast<int16_t>(minute);
	ts.sec   = static_cast<int16_t>(second);
	ts.fract = ms * 1000000;
	return ts;
}

date result::get_date(short column) const
{
	timestamp ts = get_timestamp(column);
	return date { ts.year, ts.month, ts.day };
}

} // namespace nanodbc
