/*
 * Host-side regression tests for the SQLite Arduino library.
 *
 * These tests compile the SQLite amalgamation on the host (gcc) and exercise
 * the public C API that the Arduino sketches use. They need no hardware and
 * run in CI on every push.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "sqlite3.h"

static int g_run = 0;
static int g_failed = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      fprintf(stderr, "  CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
              #cond);                                                      \
      return 1;                                                            \
    }                                                                      \
  } while (0)

#define RUN(fn)                                  \
  do {                                           \
    g_run++;                                     \
    printf("[ RUN  ] %s\n", #fn);                \
    if (fn() == 0) {                             \
      printf("[  OK  ] %s\n", #fn);              \
    } else {                                     \
      printf("[FAILED] %s\n", #fn);              \
      g_failed++;                                \
    }                                            \
  } while (0)

/* Execute a statement that returns no rows; returns SQLite result code. */
static int exec(sqlite3 *db, const char *sql) {
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "  exec error (%d): %s\n", rc, err ? err : "?");
  }
  sqlite3_free(err);
  return rc;
}

/* Run a query and return the first integer column of the first row. */
static sqlite3_int64 query_int(sqlite3 *db, const char *sql, int *ok) {
  sqlite3_stmt *st = NULL;
  sqlite3_int64 v = 0;
  *ok = 0;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    v = sqlite3_column_int64(st, 0);
    *ok = 1;
  }
  sqlite3_finalize(st);
  return v;
}

/* ------------------------------------------------------------------------- */

static int test_version(void) {
  CHECK(sqlite3_libversion_number() > 3000000);
  CHECK(strlen(sqlite3_libversion()) > 0);
  return 0;
}

static int test_open_close_memory(void) {
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(db != NULL);
  CHECK(sqlite3_close(db) == SQLITE_OK);
  return 0;
}

static int test_create_insert_select(void) {
  sqlite3 *db = NULL;
  int ok = 0;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, val REAL);")
        == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO t(name,val) VALUES('alpha', 1.5),('beta', 2.5);")
        == SQLITE_OK);
  CHECK(query_int(db, "SELECT COUNT(*) FROM t;", &ok) == 2 && ok);
  CHECK(query_int(db, "SELECT SUM(val) FROM t WHERE 1;", &ok) == 4 && ok);
  sqlite3_close(db);
  return 0;
}

static int test_prepared_statement_binding(void) {
  sqlite3 *db = NULL;
  sqlite3_stmt *ins = NULL, *sel = NULL;
  int ok = 0;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE p(id INTEGER, label TEXT, payload BLOB);")
        == SQLITE_OK);

  CHECK(sqlite3_prepare_v2(db, "INSERT INTO p VALUES(?1, ?2, ?3);", -1, &ins,
                           NULL) == SQLITE_OK);
  for (int i = 1; i <= 5; i++) {
    unsigned char blob[16];
    for (int j = 0; j < 16; j++) blob[j] = (unsigned char)(i * j);
    sqlite3_reset(ins);
    CHECK(sqlite3_bind_int(ins, 1, i) == SQLITE_OK);
    CHECK(sqlite3_bind_text(ins, 2, "row", -1, SQLITE_TRANSIENT) == SQLITE_OK);
    CHECK(sqlite3_bind_blob(ins, 3, blob, sizeof(blob), SQLITE_TRANSIENT)
          == SQLITE_OK);
    CHECK(sqlite3_step(ins) == SQLITE_DONE);
  }
  sqlite3_finalize(ins);

  CHECK(sqlite3_prepare_v2(db, "SELECT payload FROM p WHERE id = ?1;", -1,
                           &sel, NULL) == SQLITE_OK);
  CHECK(sqlite3_bind_int(sel, 1, 3) == SQLITE_OK);
  CHECK(sqlite3_step(sel) == SQLITE_ROW);
  CHECK(sqlite3_column_bytes(sel, 0) == 16);
  const unsigned char *got = sqlite3_column_blob(sel, 0);
  CHECK(got != NULL && got[2] == (unsigned char)(3 * 2));
  sqlite3_finalize(sel);

  CHECK(query_int(db, "SELECT COUNT(*) FROM p;", &ok) == 5 && ok);
  sqlite3_close(db);
  return 0;
}

static int test_transaction_rollback(void) {
  sqlite3 *db = NULL;
  int ok = 0;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE r(x INTEGER);") == SQLITE_OK);
  CHECK(exec(db, "BEGIN;") == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO r VALUES(1),(2),(3);") == SQLITE_OK);
  CHECK(exec(db, "ROLLBACK;") == SQLITE_OK);
  CHECK(query_int(db, "SELECT COUNT(*) FROM r;", &ok) == 0 && ok);
  CHECK(exec(db, "BEGIN;") == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO r VALUES(7);") == SQLITE_OK);
  CHECK(exec(db, "COMMIT;") == SQLITE_OK);
  CHECK(query_int(db, "SELECT x FROM r;", &ok) == 7 && ok);
  sqlite3_close(db);
  return 0;
}

static int test_error_reporting(void) {
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "SELECT * FROM does_not_exist;") != SQLITE_OK);
  CHECK(strstr(sqlite3_errmsg(db), "no such table") != NULL);
  sqlite3_close(db);
  return 0;
}

static int test_constraints(void) {
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE u(k TEXT PRIMARY KEY);") == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO u VALUES('a');") == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO u VALUES('a');") == SQLITE_CONSTRAINT);
  sqlite3_close(db);
  return 0;
}

static int test_file_database_persistence(void) {
  const char *path = "sqlite_host_test.db";
  sqlite3 *db = NULL;
  int ok = 0;
  unlink(path);
  CHECK(sqlite3_open(path, &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE f(v INTEGER); INSERT INTO f VALUES(42);")
        == SQLITE_OK);
  sqlite3_close(db);

  CHECK(sqlite3_open(path, &db) == SQLITE_OK);
  CHECK(query_int(db, "SELECT v FROM f;", &ok) == 42 && ok);
  sqlite3_close(db);
  unlink(path);
  return 0;
}

static int test_bulk_insert_is_consistent(void) {
  sqlite3 *db = NULL;
  sqlite3_stmt *ins = NULL;
  int ok = 0;
  const int n = 2000;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE b(i INTEGER);") == SQLITE_OK);
  CHECK(exec(db, "BEGIN;") == SQLITE_OK);
  CHECK(sqlite3_prepare_v2(db, "INSERT INTO b VALUES(?1);", -1, &ins, NULL)
        == SQLITE_OK);
  for (int i = 0; i < n; i++) {
    sqlite3_reset(ins);
    sqlite3_bind_int(ins, 1, i);
    CHECK(sqlite3_step(ins) == SQLITE_DONE);
  }
  sqlite3_finalize(ins);
  CHECK(exec(db, "COMMIT;") == SQLITE_OK);
  CHECK(query_int(db, "SELECT COUNT(*) FROM b;", &ok) == n && ok);
  CHECK(query_int(db, "SELECT SUM(i) FROM b;", &ok)
        == (sqlite3_int64)n * (n - 1) / 2);
  sqlite3_close(db);
  return 0;
}

/* ------------------------------------------------------------------------- */

int main(void) {
  RUN(test_version);
  RUN(test_open_close_memory);
  RUN(test_create_insert_select);
  RUN(test_prepared_statement_binding);
  RUN(test_transaction_rollback);
  RUN(test_error_reporting);
  RUN(test_constraints);
  RUN(test_file_database_persistence);
  RUN(test_bulk_insert_is_consistent);

  printf("\n%d tests, %d failed\n", g_run, g_failed);
  return g_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
