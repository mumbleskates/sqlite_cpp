#include "sqlite_blocking.h"

#include <cassert>
#include <condition_variable>
#include <mutex>

/*
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/

// Adapted from: https://www.sqlite.org/unlock_notify.html

namespace {

/*
** A pointer to an instance of this structure is passed as the user-context
** pointer when registering for an unlock-notify callback.
*/
struct UnlockNotification {
  std::mutex mutex;             /* Mutex to protect structure */
  std::condition_variable cond; /* Condition variable to wait on */
  bool fired = false;           /* True after unlock event has occurred */
};

/*
** This function is an unlock-notify callback registered with SQLite.
*/
void unlock_notify_cb(void **apArg, int nArg) {
  for (int i = 0; i < nArg; i++) {
    auto *p = reinterpret_cast<UnlockNotification *>(apArg[i]);
    auto _lock = std::lock_guard(p->mutex);
    p->fired = true;
    p->cond.notify_all();
  }
}

/*
** This function assumes that an SQLite API call (either sqlite3_prepare_v2()
** or sqlite3_step()) has just returned SQLITE_LOCKED. The argument is the
** associated database connection.
**
** This function calls sqlite3_unlock_notify() to register for an
** unlock-notify callback, then blocks until that callback is delivered
** and returns SQLITE_OK. The caller should then retry the failed operation.
**
** Or, if sqlite3_unlock_notify() indicates that to block would deadlock
** the system, then this function returns SQLITE_LOCKED immediately. In
** this case the caller should not retry the operation and should roll
** back the current transaction (if any).
*/
int wait_for_unlock_notify(sqlite3 *db) {
  UnlockNotification un;

  /* Register for an unlock-notify callback. */
  int rc = sqlite3_unlock_notify(db, unlock_notify_cb,
                                 reinterpret_cast<void *>(&un));
  assert(rc == SQLITE_LOCKED || rc == SQLITE_OK);

  /* The call to sqlite3_unlock_notify() always returns either SQLITE_LOCKED
  ** or SQLITE_OK.
  **
  ** If SQLITE_LOCKED was returned, then the system is deadlocked. In this
  ** case this function needs to return SQLITE_LOCKED to the caller so
  ** that the current transaction can be rolled back. Otherwise, block
  ** until the unlock-notify callback is invoked, then return SQLITE_OK.
  */
  if (rc == SQLITE_OK) {
    auto _lock = std::unique_lock(un.mutex);
    while (!un.fired) {
      un.cond.wait(_lock);
    }
  }

  return rc;
}

}  // namespace

int sqlite3_blocking_step(sqlite3_stmt *pStmt) {
  int rc;
  while (SQLITE_LOCKED == (rc = sqlite3_step(pStmt))) {
    rc = wait_for_unlock_notify(sqlite3_db_handle(pStmt));
    if (rc != SQLITE_OK) break;
    sqlite3_reset(pStmt);
  }
  return rc;
}

int sqlite3_blocking_prepare_v2(
    sqlite3 *db,           /* Database handle. */
    const char *zSql,      /* UTF-8 encoded SQL statement. */
    int nSql,              /* Length of zSql in bytes. */
    sqlite3_stmt **ppStmt, /* OUT: A pointer to the prepared statement */
    const char **pz        /* OUT: End of parsed string */
) {
  int rc;
  while (SQLITE_LOCKED ==
         (rc = sqlite3_prepare_v2(db, zSql, nSql, ppStmt, pz))) {
    rc = wait_for_unlock_notify(db);
    if (rc != SQLITE_OK) break;
  }
  return rc;
}

int sqlite3_blocking_exec(sqlite3 *db, const char *sql,
                          int (*callback)(void *, int, char **, char **),
                          void *arg, char **errmsg) {
  int rc;
  while (SQLITE_LOCKED == (rc = sqlite3_exec(db, sql, callback, arg, errmsg))) {
    rc = wait_for_unlock_notify(db);
    if (rc != SQLITE_OK) break;
  }
  return rc;
}
