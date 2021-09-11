#ifndef THIRD_PARTY_SQLITE_SQLITE_CPP_H_
#define THIRD_PARTY_SQLITE_SQLITE_CPP_H_

/*
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "sqlite3.h"
#include "sqlite_blocking.h"

namespace sqlite {

using int64 = sqlite3_int64;

// Replace these to change the implementation used
// (for example, absl::string_view)
using string = std::string;
using string_view = std::string_view;

// Subclasses of string and string_view to represent a string
// containing unicode data. Text can be implicitly cast to a string but
// must be explicitly cast the other way around.
class TextView : public string_view {
 public:
  using string_view::operator=;
  TextView() noexcept = default;
  TextView(const TextView& copy_from) noexcept = default;
  TextView(const char* data, size_t size) noexcept
      : string_view(data, size) {}
  TextView(const char* data) : string_view(data) {}
  explicit TextView(const string_view& copy_from)
      : string_view(copy_from) {}
};

class Text : public string {
 public:
  using string::string;
  using string::operator=;
  Text(const Text&) = default;
  Text(Text&&) noexcept = default;
  explicit Text(const string& copy_from) : string(copy_from) {}
  explicit Text(string&& move_from) noexcept
      : string(std::forward<string>(move_from)) {}
  // implicit conversion to TextView to match string
  operator TextView() const noexcept { return TextView(data(), size()); }
  // conversion to string_view must be explicit to avoid function ambiguity
  explicit operator string_view() const noexcept {
    return string_view(data(), size());
  }
};

// Only thrown by output iterators.
struct SqliteException : public std::runtime_error {
  using runtime_error::runtime_error;
};

namespace detail {

// Non-null empty-string surrogate.
constexpr static char kNothing = 0;

// Template archetype for reading columns. This must be in a struct because, as
// they only vary on return type, they cannot be deduced except by partial
// specialization. (This lets us use optional<> for nullable easily.)
template <typename Col>
struct ColumnReader;

template <>
struct ColumnReader<int64> {
  static int64 Read(sqlite3_stmt* stmt, int position) {
    return sqlite3_column_int64(stmt, position);
  }
};

template <>
struct ColumnReader<long> {
  static long Read(sqlite3_stmt* stmt, int position) {
    return sqlite3_column_int64(stmt, position);
  }
};

template <>
struct ColumnReader<int> {
  static int Read(sqlite3_stmt* stmt, int position) {
    return sqlite3_column_int(stmt, position);
  }
};

template <>
struct ColumnReader<double> {
  static double Read(sqlite3_stmt* stmt, int position) {
    return sqlite3_column_double(stmt, position);
  }
};

template <>
struct ColumnReader<string> {
  static string Read(sqlite3_stmt* stmt, int position) {
    return string(
        reinterpret_cast<const char*>(sqlite3_column_blob(stmt, position)),
        sqlite3_column_bytes(stmt, position));
  }
};

template <>
struct ColumnReader<string_view> {
  static string_view Read(sqlite3_stmt* stmt, int position) {
    return string_view(
        reinterpret_cast<const char*>(sqlite3_column_blob(stmt, position)),
        sqlite3_column_bytes(stmt, position));
  }
};

template <>
struct ColumnReader<Text> {
  static Text Read(sqlite3_stmt* stmt, int position) {
    return Text(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, position)),
        sqlite3_column_bytes(stmt, position));
  }
};

template <>
struct ColumnReader<TextView> {
  static TextView Read(sqlite3_stmt* stmt, int position) {
    return TextView(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, position)),
        sqlite3_column_bytes(stmt, position));
  }
};

template <typename Nullable>
struct ColumnReader<std::optional<Nullable>> {
  static std::optional<Nullable> Read(sqlite3_stmt* stmt, int position) {
    if (sqlite3_column_type(stmt, position) == SQLITE_NULL) {
      return std::nullopt;
    } else {
      return ColumnReader<Nullable>::Read(stmt, position);
    }
  }
};

// Base case and variadic extension for reading entire rows.
template <int Pos, typename Col, typename... Rest>
struct RowReader {
  static std::tuple<Col> Read(sqlite3_stmt* stmt) {
    return {ColumnReader<Col>::Read(stmt, Pos)};
  }
};

template <int Pos, typename Col, typename More, typename... Rest>
struct RowReader<Pos, Col, More, Rest...> {
  static std::tuple<Col, More, Rest...> Read(sqlite3_stmt* stmt) {
    return std::tuple_cat(std::tuple<Col>(ColumnReader<Col>::Read(stmt, Pos)),
                          RowReader<Pos + 1, More, Rest...>::Read(stmt));
  }
};

// Wrapper function to bake in the starting index.
template <typename... Cols>
std::tuple<Cols...> ReadRow(sqlite3_stmt* stmt) {
  // Result columns are zero-indexed!
  return RowReader<0, Cols...>::Read(stmt);
}

// Templates for binding values to columns. CopyOnBind, where relevant,
// determines whether sqlite is instructed to eagerly copy byte buffers as soon
// as the call occurs (true) or whether it will only read the buffers when the
// statement is evaluated (false).
template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, int64 param) {
  return sqlite3_bind_int64(stmt, position, param);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, long param) {
  return sqlite3_bind_int64(stmt, position, param);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, int param) {
  return sqlite3_bind_int(stmt, position, param);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, double param) {
  return sqlite3_bind_double(stmt, position, param);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, string_view param) {
  if (param.empty()) {
    return sqlite3_bind_zeroblob(stmt, position, 0);
  }
  sqlite3_destructor_type copy_mode;
  if constexpr (CopyOnBind) {
    copy_mode = SQLITE_TRANSIENT;
  } else {
    copy_mode = SQLITE_STATIC;
  }
  return sqlite3_bind_blob64(stmt, position, param.data(), param.size(),
                             copy_mode);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, TextView param) {
  sqlite3_destructor_type copy_mode;
  if constexpr (CopyOnBind) {
    copy_mode = SQLITE_TRANSIENT;
  } else {
    copy_mode = SQLITE_STATIC;
  }
  // Force a non-null pointer to avoid binding SQL NULL. For BLOB types we use
  // bind_zeroblob but there is no good equivalent to this that produces a TEXT
  // value.
  return sqlite3_bind_text64(stmt, position,
                             (param.empty() ? &kNothing : param.data()),
                             param.size(), copy_mode, SQLITE_UTF8);
}

template <bool CopyOnBind>
int BindParam(sqlite3_stmt* stmt, int position, std::nullopt_t) {
  return sqlite3_bind_null(stmt, position);
}

template <bool CopyOnBind, typename Nullable>
int BindParam(sqlite3_stmt* stmt, int position,
              const std::optional<Nullable>& param) {
  if (!param.has_value()) return sqlite3_bind_null(stmt, position);
  return BindParam<CopyOnBind>(stmt, position, *param);
}

// Binds params from a tuple or other std::get-decomposable argument.
template <bool CopyOnBind, int Pos = 0, typename Tuple>
int DoBindTupleParams(sqlite3_stmt* stmt, const Tuple& params) {
  // Params are one-indexed! so we add 1 to Pos
  int rc = BindParam<CopyOnBind>(stmt, Pos + 1, std::get<Pos>(params));
  if (rc != SQLITE_OK) return rc;
  if constexpr (Pos + 1 < std::tuple_size<Tuple>::value) {
    return DoBindTupleParams<CopyOnBind, Pos + 1, Tuple>(stmt, params);
  } else {
    return SQLITE_OK;
  }
}

template <typename Tuple>
int BindTupleParams(sqlite3_stmt* stmt, const Tuple& params) {
  return DoBindTupleParams<false>(stmt, params);
}

template <typename Tuple>
int BindTupleParamsCopy(sqlite3_stmt* stmt, const Tuple& params) {
  return DoBindTupleParams<true>(stmt, params);
}

inline int ColIndex(sqlite3_stmt* stmt, const char* name) {
  return sqlite3_bind_parameter_index(stmt, name);
}
inline int ColIndex(sqlite3_stmt* stmt, const string& name) {
  return sqlite3_bind_parameter_index(stmt, name.data());
}
inline int ColIndex(sqlite3_stmt* stmt, int index) { return index; }

}  // namespace detail

// Execute a script that may contain multiple statements, ignoring any result
// rows. Returns the sqlite return code (rc).
int ExecRC(sqlite3* db, string_view script);

// Execute a script that may contain multiple statements, ignoring any result
// rows. Returns true if there is no error code.
inline bool Exec(sqlite3* db, string_view script) {
  return ExecRC(db, script) == SQLITE_OK;
}

class Statement {
 private:
  template <typename... Cols>
  friend class RowIterator;
  template <typename... Cols>
  friend class Rowset;
  template <typename... Cols>
  class Rowset;
  class SinkIterator;
  class SinkCopyIterator;

 public:
  Statement(sqlite3* db, string_view sql, bool must_compile_all = true);
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& move_from) noexcept;
  Statement& operator=(Statement&& move_from) noexcept;
  ~Statement();

  inline void Reset() {
    sqlite3_reset(stmt_);
    rc_ = stmt_ == nullptr ? SQLITE_ERROR : SQLITE_OK;
  }

  // Binds values to the statement, returning false if an error occurs. String
  // values are copied.
  template <typename... Params>
  bool BindCopy(const Params&... params) {
    Reset();
    return (rc_ = detail::BindTupleParamsCopy(stmt_, std::tie(params...))) ==
           SQLITE_OK;
  }

  // Binds values to the statement, returning false if an error occurs. String
  // values are copied.
  template <typename TupleParams>
  bool BindTupleCopy(const TupleParams& params) {
    Reset();
    return (rc_ = detail::BindTupleParamsCopy(stmt_, params)) == SQLITE_OK;
  }

  // Binds values to the statement, returning false if an error occurs.
  template <typename... Params>
  bool Bind(const Params&... params) {
    Reset();
    return (rc_ = detail::BindTupleParams(stmt_, std::tie(params...))) ==
           SQLITE_OK;
  }

  // Binds values to the statement, returning false if an error occurs.
  template <typename TupleParams>
  bool BindTuple(const TupleParams& params) {
    Reset();
    return (rc_ = detail::BindTupleParams(stmt_, params)) == SQLITE_OK;
  }

  // Bind a specific named or numbered parameter to the statement. The name can
  // be a string, a "C string literal", or an int giving the column number.
  template <typename Name, typename Value>
  bool SetCopy(Name name, Value value) {
    Reset();
    return (rc_ = detail::BindParam<true>(stmt_, detail::ColIndex(stmt_, name),
                                          value)) == SQLITE_OK;
  }

  // Bind a specific named or numbered parameter to the statement. The name can
  // be a string, a "C string literal", or an int giving the column number.
  template <typename Name, typename Value>
  bool Set(Name name, Value value) {
    Reset();
    return (rc_ = detail::BindParam<false>(stmt_, detail::ColIndex(stmt_, name),
                                           value)) == SQLITE_OK;
  }

  // Resets all the bindings of the statement to null.
  void ClearBinds();

  // Advances the statement to read a row if present. If there are no more rows
  // or there is an error, returns nullopt.
  template <typename... Cols>
  std::optional<std::tuple<Cols...>> GetRow() {
    rc_ = sqlite3_blocking_step(stmt_);
    if (rc_ == SQLITE_ROW) {
      return detail::ReadRow<Cols...>(stmt_);
    }
    return std::nullopt;
  }

  // Resets and runs a statement expecting no returned rows. Returns false if an
  // error occurs or a row was returned.
  bool Run();

  // Returns an iterable that yields all the rows from this query.
  // When the iterable stops yielding rows the status of the Statement should be
  // checked to see if it is done; if it is not, an error occurred.
  //
  // The iterable can be iterated again from the beginning, which will re-run
  // the query. It is reset when the begin() iterator is fetched, and not really
  // designed to be used much outside range-based loops. Both the range and the
  // begin iterator it produces are invalidated when the Statement is moved or
  // destroyed.
  //
  // Example:
  //
  // Statement stmt(db, "SELECT name, age FROM users WHERE alive;");
  // for (const auto& [name, age] : stmt.Rows<string, int>()) {
  //   cout << name << " is alive and " << age << " years old" << endl;
  // }
  // if (!stmt.done()) cerr << "oh no! " << stmt.errstr() << endl;
  template <typename... Cols>
  Rowset<Cols...> Rows() {
    return Rowset<Cols...>(this);
  }

  // Returns an output iterator for any kind of tuple that can be bound to this
  // statement. A SqliteException will be thrown if the statement fails to run
  // or returns a row.
  inline SinkIterator Sink() { return SinkIterator(this); }

  inline bool ok() const { return rc_ == SQLITE_OK; }
  inline bool done() const { return rc_ == SQLITE_DONE; }
  inline int rc() const { return rc_; }
  inline string_view errstr() const { return sqlite3_errstr(rc_); }

 private:
  // Iterator for rows produced by a Statement. Invalidated when the Statement
  // is moved or destroyed.
  template <typename... Cols>
  class RowIterator {
   public:
    using value_type = std::tuple<Cols...>;

    RowIterator() : s_(nullptr) {}
    explicit RowIterator(Statement* s) : s_(s) {}

    RowIterator& operator++() {
      s_->rc_ = sqlite3_blocking_step(s_->stmt_);
      return *this;
    }
    value_type operator*() { return detail::ReadRow<Cols...>(s_->stmt_); }
    bool operator!=(const RowIterator& other) const {
      return (s_ != other.s_) && !(stopped() && other.stopped());
    }

   private:
    bool stopped() const { return s_ == nullptr || s_->rc() != SQLITE_ROW; }

    Statement* s_;
  };

  // Range for rows produced by a Statement. Rerunnable when exhausted.
  // Invalidated when the Statement is moved or destroyed.
  template <typename... Cols>
  class Rowset {
   public:
    explicit Rowset(Statement* s) : s_(s) {}

    RowIterator<Cols...> begin() {
      s_->Reset();
      RowIterator<Cols...> it(s_);
      ++it;
      return it;
    }
    RowIterator<Cols...> end() { return {}; }

   private:
    Statement* s_;
  };

  // Converts assignments to this object into BindTuple calls on the wrapped
  // statement.
  class AssignBinder {
   public:
    explicit AssignBinder(Statement* s) : s_(s) {}

    template <typename TupleCols>
    AssignBinder& operator=(const TupleCols& row) {
      s_->ClearBinds();
      s_->BindTuple(row);
      if (!s_->Run()) {
        throw SqliteException(sqlite3_errstr(s_->rc()));
      }
      return *this;
    }

   private:
    Statement* s_;
  };

  // Output iterator for any kind of row tuple. Invalidated when the Statement
  // is moved or destroyed.
  class SinkIterator {
   public:
    explicit SinkIterator(Statement* s) : s_(s) {}

    SinkIterator& operator++() { return *this; }
    AssignBinder operator*() { return AssignBinder(s_); }

   private:
    Statement* s_;
  };

  sqlite3_stmt* stmt_ = nullptr;
  int rc_ = 0;
};

}  // namespace sqlite

#endif  // THIRD_PARTY_SQLITE_SQLITE_CPP_H_
