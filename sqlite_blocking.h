#ifndef THIRD_PARTY_SQLITE_SQLITE_BLOCKING_H_
#define THIRD_PARTY_SQLITE_SQLITE_BLOCKING_H_

#include "sqlite3.h"

/*
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
** This function is a wrapper around the SQLite function sqlite3_step().
** It functions in the same way as step(), except that if a required
** shared-cache lock cannot be obtained, this function may block waiting for
** the lock to become available. In this scenario the normal API step()
** function always returns SQLITE_LOCKED.
**
** If this function returns SQLITE_LOCKED, the caller should rollback
** the current transaction (if any) and try again later. Otherwise, the
** system may become deadlocked.
*/
int sqlite3_blocking_step(sqlite3_stmt *pStmt);

/*
** This function is a wrapper around the SQLite function sqlite3_prepare_v2().
** It functions in the same way as prepare_v2(), except that if a required
** shared-cache lock cannot be obtained, this function may block waiting for
** the lock to become available. In this scenario the normal API prepare_v2()
** function always returns SQLITE_LOCKED.
**
** If this function returns SQLITE_LOCKED, the caller should rollback
** the current transaction (if any) and try again later. Otherwise, the
** system may become deadlocked.
*/
int sqlite3_blocking_prepare_v2(
    sqlite3 *db,           /* Database handle. */
    const char *zSql,      /* UTF-8 encoded SQL statement. */
    int nSql,              /* Length of zSql in bytes. */
    sqlite3_stmt **ppStmt, /* OUT: A pointer to the prepared statement */
    const char **pz        /* OUT: End of parsed string */
);

/*
** This function is a wrapper around the SQLite function sqlite3_exec().
** It functions in the same way as exec(), except that if a required
** shared-cache lock cannot be obtained, this function may block waiting for
** the lock to become available. In this scenario the normal API exec()
** function always returns SQLITE_LOCKED.
**
** If this function returns SQLITE_LOCKED, the caller should rollback
** the current transaction (if any) and try again later. Otherwise, the
** system may become deadlocked.
*/
int sqlite3_blocking_exec(sqlite3 *db, const char *sql,
                          int (*callback)(void *, int, char **, char **),
                          void *arg, char **errmsg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // THIRD_PARTY_SQLITE_SQLITE_BLOCKING_H_
