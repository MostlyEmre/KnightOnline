#ifndef DBLIBRARY_SQLITE_NANODBC_NANODBC_H
#define DBLIBRARY_SQLITE_NANODBC_NANODBC_H

#pragma once

// SQLite database backend, presented through the subset of the nanodbc API that
// db-library, the generated db-models and the servers use. Selected at configure time
// with -DOPENKO_DB_BACKEND=SQLITE; it then provides the `nanodbc` target in place of
// the ODBC library, so none of those callers change.
//
// Behaviour follows what the SQL Server ODBC driver does through real nanodbc
// (built with NANODBC_DISABLE_NULL_ACCESS_ERROR and NANODBC_USE_UINT8_FOR_TINYINT):
// - reading NULL through get()/get_ref() yields T{} instead of throwing;
// - output parameters and return values are written once the result sets are drained;
// - fetching from a statement that produced no result set throws SQLSTATE 24000.
// Stored procedure calls ({CALL NAME(?,...)} / {? = CALL NAME(?)}) run C++
// reimplementations of the T-SQL procedures (see Procedures.h).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Narrow strings only, as nanodbc builds without NANODBC_ENABLE_UNICODE. Kept as a macro
// because the generated code uses nanodbc's macro.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define NANODBC_TEXT(s) s

namespace nanodbc
{

using string = std::string;

struct date
{
	std::int16_t year;
	std::int16_t month;
	std::int16_t day;
};

struct time
{
	std::int16_t hour;
	std::int16_t min;
	std::int16_t sec;
};

struct timestamp
{
	std::int16_t year;
	std::int16_t month;
	std::int16_t day;
	std::int16_t hour;
	std::int16_t min;
	std::int16_t sec;
	std::int32_t fract; ///< nanoseconds, as SQL_TIMESTAMP_STRUCT
};

class database_error : public std::runtime_error
{
public:
	/// \brief Same shape as nanodbc's; the handle arguments are ignored.
	database_error(void* handle, short handle_type, const std::string& info = "");

	/// \brief Raised by the SQLite backend itself.
	database_error(std::string sqlState, const std::string& message, long native = 0);

	const char* what() const noexcept override;
	long native() const noexcept;
	const std::string& state() const noexcept;

private:
	long _native = 0;
	std::string _state;
	std::string _message;
};

/// \brief Same as nanodbc's; real nanodbc raises it for conversions its C type can't make
/// (e.g. a tinyint read as a string). The SQLite backend converts freely and never throws it.
class type_incompatible_error : public std::runtime_error
{
public:
	type_incompatible_error() : std::runtime_error("type incompatible")
	{
	}
};

class statement;
class result;

namespace detail
{
class connection_impl;
class statement_impl;
class result_impl;
} // namespace detail

class connection
{
public:
	connection();
	explicit connection(const string& connectionString, long timeout = 0);
	connection(const string& dsn, const string& user, const string& pass, long timeout = 0);
	~connection() noexcept;

	connection(const connection&)            = default;
	connection& operator=(const connection&) = default;

	void connect(const string& connectionString, long timeout = 0);
	void connect(const string& dsn, const string& user, const string& pass, long timeout = 0);
	bool connected() const;
	void disconnect();

	/// \brief Path of the open database file (empty if not connected).
	const string& database_path() const;

private:
	friend class statement;
	std::shared_ptr<detail::connection_impl> _impl;
};

class statement
{
public:
	enum param_direction : uint8_t
	{
		PARAM_IN,
		PARAM_OUT,
		PARAM_INOUT,
		PARAM_RETURN
	};

	statement();
	explicit statement(class connection& conn);
	statement(class connection& conn, const string& query, long timeout = 0);
	~statement() noexcept;

	statement(const statement&)            = default;
	statement& operator=(const statement&) = default;

	void open(class connection& conn);
	bool open() const;
	void close();
	void prepare(class connection& conn, const string& query, long timeout = 0);
	void prepare(const string& query, long timeout = 0);

	result execute(long batchOperations = 1, long timeout = 0);
	void just_execute(long batchOperations = 1, long timeout = 0);

	void reset_parameters() noexcept;
	short parameters() const;

	/// \brief Binds a single value. Integral and floating-point types are read when the
	/// statement executes and, for output directions, written back after it is drained.
	template <class T>
	void bind(short paramIndex, const T* value, param_direction direction = PARAM_IN);

	void bind(short paramIndex, const char* value, param_direction direction = PARAM_IN);
	void bind(short paramIndex, const std::string& value, param_direction direction = PARAM_IN);
	void bind_binary(
		short paramIndex, const std::vector<uint8_t>& value, param_direction direction = PARAM_IN);
	void bind_binary(short paramIndex, const uint8_t* value, std::size_t valueSize,
		param_direction direction = PARAM_IN);
	void bind_null(short paramIndex, std::size_t batchSize = 1);

private:
	enum class scalar_kind : uint8_t
	{
		i8,
		u8,
		i16,
		u16,
		i32,
		u32,
		i64,
		u64,
		f32,
		f64
	};

	void bind_scalar(
		short paramIndex, const void* value, scalar_kind kind, param_direction direction);

	template <class T>
	static constexpr scalar_kind kind_of();

	std::shared_ptr<detail::statement_impl> _impl;
};

class result
{
public:
	result();
	~result() noexcept;
	result(const result& rhs);
	result(result&& rhs) noexcept;
	result& operator=(result rhs);

	long affected_rows() const;
	short columns() const;
	string column_name(short column) const;

	/// \brief The column's ODBC SQL type code (SQL_TINYINT, SQL_VARCHAR, ...), from the declared
	/// column type, or from the value for computed columns.
	int column_datatype(short column) const;
	short column(const string& columnName) const;

	bool next();
	bool next_result();
	bool first();
	bool end() const noexcept;

	bool is_null(short column) const;
	bool is_null(const string& columnName) const;

	template <class T>
	T get(short column) const
	{
		T value {};
		get_ref<T>(column, value);
		return value;
	}

	template <class T>
	T get(short column, const T& fallback) const
	{
		if (is_null(column))
			return fallback;
		return get<T>(column);
	}

	template <class T>
	T get(const string& columnName) const
	{
		return get<T>(column(columnName));
	}

	template <class T>
	void get_ref(short column, T& out) const;

	template <class T>
	void get_ref(const string& columnName, T& out) const
	{
		get_ref<T>(column(columnName), out);
	}

	explicit operator bool() const;

private:
	friend class statement;
	explicit result(std::shared_ptr<detail::result_impl> impl);

	// Typed accessors the get_ref() template dispatches to.
	int64_t get_int64(short column) const;
	double get_double(short column) const;
	std::string get_string(short column) const;
	std::vector<uint8_t> get_bytes(short column) const;
	timestamp get_timestamp(short column) const;
	date get_date(short column) const;

	std::shared_ptr<detail::result_impl> _impl;
};

namespace detail
{
template <class T>
struct is_optional : std::false_type
{
};

template <class T>
struct is_optional<std::optional<T>> : std::true_type
{
};
} // namespace detail

template <class T>
constexpr statement::scalar_kind statement::kind_of()
{
	if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, char>)
		return scalar_kind::i8;
	else if constexpr (std::is_same_v<T, uint8_t>)
		return scalar_kind::u8;
	else if constexpr (std::is_same_v<T, int16_t>)
		return scalar_kind::i16;
	else if constexpr (std::is_same_v<T, uint16_t>)
		return scalar_kind::u16;
	else if constexpr (std::is_same_v<T, int32_t>)
		return scalar_kind::i32;
	else if constexpr (std::is_same_v<T, uint32_t>)
		return scalar_kind::u32;
	else if constexpr (std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) == 8)
		return scalar_kind::i64;
	else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) == 8)
		return scalar_kind::u64;
	else if constexpr (std::is_same_v<T, float>)
		return scalar_kind::f32;
	else if constexpr (std::is_same_v<T, double>)
		return scalar_kind::f64;
	else
		static_assert(sizeof(T) == 0, "nanodbc (SQLite backend): unsupported bind type");
}

template <class T>
void statement::bind(short paramIndex, const T* value, param_direction direction)
{
	bind_scalar(paramIndex, value, kind_of<std::remove_cv_t<T>>(), direction);
}

template <class T>
void result::get_ref(short column, T& out) const
{
	if constexpr (detail::is_optional<T>::value)
	{
		using V = typename T::value_type;
		if (is_null(column))
		{
			out.reset();
			return;
		}
		V value {};
		get_ref<V>(column, value);
		out = std::move(value);
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		out = get_string(column);
	}
	else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
	{
		out = get_bytes(column);
	}
	else if constexpr (std::is_same_v<T, timestamp>)
	{
		out = get_timestamp(column);
	}
	else if constexpr (std::is_same_v<T, date>)
	{
		out = get_date(column);
	}
	else if constexpr (std::is_floating_point_v<T>)
	{
		out = static_cast<T>(get_double(column));
	}
	else if constexpr (std::is_integral_v<T>)
	{
		// Matches nanodbc: integers are fetched as 64-bit and narrowed with a cast.
		out = static_cast<T>(get_int64(column));
	}
	else
	{
		static_assert(sizeof(T) == 0, "nanodbc (SQLite backend): unsupported result type");
	}
}

} // namespace nanodbc

#endif // DBLIBRARY_SQLITE_NANODBC_NANODBC_H
