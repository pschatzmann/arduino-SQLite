/*
 * Host tests for the C++ wrapper (src/SQLite.h), SQLiteCpp-style API.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "SQLite.h"

static int g_failed = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      fprintf(stderr, "  CHECK failed at %s:%d: %s\n", __FILE__,         \
              __LINE__, #cond);                                          \
      g_failed++;                                                        \
      return;                                                            \
    }                                                                    \
  } while (0)

static void test_open_and_exec(void) {
  SQLite::Database db(":memory:");
  CHECK(db.isOpen());
  CHECK(db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, v REAL);") == SQLITE_OK);
  CHECK(db.tableExists("t"));
  CHECK(!db.tableExists("missing"));
  CHECK(db.exec("INSERT INTO t(name, v) VALUES('a', 1.5), ('b', 2.5);") == SQLITE_OK);
  CHECK(db.getChanges() == 2);
  CHECK(db.getLastInsertRowid() == 2);
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 2);
  CHECK(db.execAndGetInt("SELECT id FROM t WHERE 0;", 42) == 42);  /* default, not error */
  CHECK(!db.hasError());
}

static void test_statement_bind_and_columns(void) {
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE t(i INTEGER, l INTEGER, d REAL, s TEXT, b BLOB, n);");

  SQLite::Statement ins(db, "INSERT INTO t VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
  CHECK(ins.isValid());
  CHECK(ins.getBindParameterCount() == 6);
  CHECK(ins.getIndex("?3") == 3);
  const unsigned char blob[4] = {1, 2, 3, 4};
  ins.bind(1, 7);
  ins.bind(2, 1234567890123LL);
  ins.bind(3, 3.25);
  ins.bind(4, "hello");
  ins.bind(5, blob, 4);
  ins.bindNull(6);
  CHECK(ins.exec() == 1);  /* rows changed */
  ins.reset();
  ins.clearBindings();

  SQLite::Statement q(db, "SELECT i, l, d, s, b, n FROM t;");
  CHECK(q.getColumnCount() == 6);
  CHECK(strcmp(q.getColumnName(3), "s") == 0);
  CHECK(q.executeStep());
  CHECK(q.getColumn(0).getInt() == 7);
  CHECK(q.getColumn(1).getInt64() == 1234567890123LL);
  CHECK(q.getColumn(2).getDouble() == 3.25);
  CHECK(strcmp(q.getColumn(3).getText(), "hello") == 0);
  CHECK(q.getColumn(4).getBytes() == 4);
  CHECK(((const unsigned char *)q.getColumn(4).getBlob())[3] == 4);
  CHECK(q.getColumn(5).isNull());
  CHECK(q.isColumnNull(5));
  CHECK(!q.executeStep());  /* no more rows */
  CHECK(!q.hasError());
}

static void test_exec_on_row_returning_statement_fails(void) {
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE t(x); INSERT INTO t VALUES(1);");
  SQLite::Statement q(db, "SELECT x FROM t;");
  CHECK(q.exec() == -1);
  CHECK(q.hasError());
}

static void test_transaction_commit_and_rollback(void) {
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE t(x);");
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(1),(2);");
    CHECK(tx.commit());
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 2);
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(3);");
    /* no commit: destructor rolls back */
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 2);
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(4);");
    CHECK(tx.rollback());
    CHECK(!tx.commit());  /* already finished */
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 2);
}

static void test_error_reporting(void) {
  SQLite::Database db(":memory:");
  CHECK(db.exec("SELECT * FROM nope;") != SQLITE_OK);
  CHECK(db.hasError());
  CHECK(strstr(db.getErrorMsg(), "no such table") != nullptr);
  SQLite::Statement bad(db, "SELEC broken");
  CHECK(!bad.isValid());
  CHECK(bad.hasError());
  CHECK(!bad.executeStep());
}

static void test_file_database_and_read_only(void) {
  const char *path = "wrapper_host_test.db";
  unlink(path);
  {
    SQLite::Database db(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    CHECK(db.isOpen());
    db.exec("CREATE TABLE f(v TEXT); INSERT INTO f VALUES('persisted');");
  }
  {
    SQLite::Database ro(path, SQLite::OPEN_READONLY);
    CHECK(ro.isOpen());
    SQLite::Statement q(ro, "SELECT v FROM f;");
    CHECK(q.executeStep());
    CHECK(strcmp(q.getColumn(0).getText(), "persisted") == 0);
    CHECK(ro.exec("INSERT INTO f VALUES('nope');") != SQLITE_OK);  /* read-only */
  }
  {
    /* Read-only open of a missing file must fail and must not create it. */
    SQLite::Database missing("wrapper_missing.db", SQLite::OPEN_READONLY);
    CHECK(!missing.isOpen());
    CHECK(access("wrapper_missing.db", F_OK) != 0);
  }
  unlink(path);
}

int main(void) {
  printf("SQLite C++ wrapper (%s)\n", SQLite::Database::libVersion());
  test_open_and_exec();
  test_statement_bind_and_columns();
  test_exec_on_row_returning_statement_fails();
  test_transaction_commit_and_rollback();
  test_error_reporting();
  test_file_database_and_read_only();
  printf("%s\n", g_failed ? "C++ wrapper: FAILED" : "C++ wrapper: all passed");
  return g_failed ? 1 : 0;
}
