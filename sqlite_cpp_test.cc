#include "sqlite_cpp.h"

/*
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/

#undef NDEBUG  // always keep asserts

#include <algorithm>
#include <cassert>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "sqlite3.h"

int main(int argc, char* argv[]) {
  sqlite3* db;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    std::cout << sqlite3_errmsg(db) << std::endl;
    return sqlite3_errcode(db);
  }
  {
    sqlite::Statement stmt(db, R"sql(
      -- Taken from the WITH clause documentation
      WITH RECURSIVE
      xaxis(x) AS (
        VALUES(-2.0) UNION ALL SELECT x+0.025 FROM xaxis WHERE x<1.2
      ),
      yaxis(y) AS (
        VALUES(-1.0) UNION ALL SELECT y+0.05 FROM yaxis WHERE y<1.0
      ),
      m(iter, cx, cy, x, y) AS (
        SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis
        UNION ALL
        SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m
         WHERE (x*x + y*y) < 4.0 AND iter<56
      ),
      m2(iter, cx, cy) AS (
        SELECT max(iter), cx, cy FROM m GROUP BY cx, cy
      ),
      a(t) AS (
        SELECT group_concat( substr(' .-~+*=?#', 1+min(iter/7,8), 1), '')
        FROM m2 GROUP BY cy
      )
      SELECT group_concat(rtrim(t),x'0a') FROM a;
    )sql");
    assert(stmt.ok());
    auto row = stmt.GetRow<std::string>();
    assert(row.has_value());
    std::cout << std::get<0>(*row) << std::endl;
  }

  assert(sqlite::Exec(db, R"sql(
    CREATE TABLE a (
      x INTEGER PRIMARY KEY,
      y INTEGER,
      z TEXT
    );
  )sql"));

  {
    sqlite::Statement stmt(db, R"sql(
      INSERT INTO a(x, y, z) VALUES (?, ?, ?);
    )sql");
    assert(stmt.ok());
    std::string thd = "300";
    stmt.Bind(100, 200, thd);
    assert(stmt.Run());
    std::vector<
        std::tuple<int, std::optional<int>, std::optional<sqlite::Text>>>
        things = {
            {1, 4, "asdf"},
            {2, 4, "wabl"},
            {3, std::nullopt, "test"},
            {4, -1, std::nullopt},
            {55, 3, "stuff goes here"},
            {6, 0, ""},
            {7, 4, "here's another one"},
        };
    std::copy(things.begin(), things.end(), stmt.Sink());
  }
  {
    std::stringstream results;
    sqlite::Statement stmt(db, R"sql(SELECT * FROM a ORDER BY x;)sql");
    for (const auto& [x, yy, zz] :
         stmt.Rows<int, std::optional<int>, std::optional<sqlite::Text>>()) {
      std::string y = yy.has_value() ? std::to_string(*yy) : "null";
      std::string z = zz.has_value() ? "'" + *zz + "'" : "null";
      results << x << ", " << y << ", " << z << std::endl;
    }
    assert(stmt.done());
    std::cout << results.str() << std::endl;
    assert(results.str() ==
           "1, 4, 'asdf'\n"
           "2, 4, 'wabl'\n"
           "3, null, 'test'\n"
           "4, -1, null\n"
           "6, 0, ''\n"
           "7, 4, 'here's another one'\n"
           "55, 3, 'stuff goes here'\n"
           "100, 200, '300'\n");
  }

  // Test Exec and ExecRC with some goofy scripts
  assert(sqlite::Exec(db, ";;begin;;;rollback; select 1        ;   "));
  assert(sqlite::ExecRC(db, "   select 1; asdf") == SQLITE_ERROR);

  {
    // Garbage after the first statement in the sql text provided to the
    // Statement constructor is still looked at.
    sqlite::Statement a(db, "select 1; this part is invalid sql");
    assert(!a.ok());
    a.Reset();
    // After resetting, the Statement is ok
    assert(a.ok());
    // ...and the first statement still compiled and is usable.
    auto row = a.GetRow<int>();
    assert(row.has_value());
    assert(std::get<0>(*row) == 1);
    assert(!a.GetRow<int>().has_value());
    assert(a.done());
    // Two statements in a row is also not ok.
    a = sqlite::Statement(db, "select 2; select 3;");
    assert(!a.ok());
    // ...unless we say we don't care about the rest of the sql string.
    a = sqlite::Statement(db, "select 4; select 5;", false);
    assert(a.ok());
    a = sqlite::Statement(db, "select 4; literally& anything * here!", false);
    assert(a.ok());
  }

  {
    // Ensure that statements are finalized correctly.
    sqlite::Statement a(db, "select 1;");
    sqlite::Statement b(db, "select 2;");
    a = std::move(b);
    sqlite::Statement c(db, "select 3;");
    sqlite::Statement d(std::move(c));
    // Self-move is normally a bug, but should neither double-free nor leak.
    d = std::move(d);
  }

  // We use sqlite3_close() instead of sqlite3_close_v2() so that we can
  // demonstrate that every prepared statement has been cleaned up upon
  // destruction.
  assert(sqlite3_close(db) == SQLITE_OK);

  std::cout << "Ok!" << std::endl;
  return 0;
}
