#include "sqlite_cpp.h"

/*
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/

namespace sqlite {

int ExecRC(sqlite3* db, std::string_view script) {
  // We compile and execute the script in long-form so that we can accept
  // string_views, which may not be nul-terminated; sqlite3_exec only accepts
  // nul-terminated strings.
  const char* current = script.begin();
  int rc = SQLITE_OK;
  while (current < script.end()) {
    // Compile the next available statement
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_blocking_prepare_v2(
        db, current, static_cast<int>(script.end() - current), &stmt, &current);
    if (rc == SQLITE_OK) {
      if (!stmt) continue;  // No statement was compiled, skip.
      // Run the statement ignoring all rows until it is done.
      while ((rc = sqlite3_blocking_step(stmt)) == SQLITE_ROW) {
        // Ignore rows.
      }
    }
    sqlite3_finalize(stmt);  // Always finalize stmts before returning.
    if (rc != SQLITE_OK && rc != SQLITE_DONE) return rc;
  }
  return SQLITE_OK;
}

Statement::Statement(sqlite3* db, std::string_view sql) {
  rc_ =
      sqlite3_blocking_prepare_v2(db, sql.data(), sql.size(), &stmt_, nullptr);
}

Statement::Statement(Statement&& move_from) noexcept : stmt_(move_from.stmt_) {
  move_from.stmt_ = nullptr;
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement& Statement::operator=(Statement&& move_from) noexcept {
  sqlite3_finalize(stmt_);
  stmt_ = move_from.stmt_;
  move_from.stmt_ = nullptr;
  return *this;
}

void Statement::ClearBinds() { sqlite3_clear_bindings(stmt_); }

bool Statement::Run() {
  sqlite3_reset(stmt_);
  return (rc_ = sqlite3_blocking_step(stmt_)) == SQLITE_DONE;
}

}  // namespace sqlite
