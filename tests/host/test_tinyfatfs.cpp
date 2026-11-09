/*
 * Host test: SQLite running on a TinyFATFS volume (RAM disk driver).
 *
 * Covers the SQLiteFileSystem backend API end to end: create, write, close,
 * reopen and read back through the VFS, plus journal files and the switch back
 * to the stdio backend.
 */
#include <stdio.h>
#include <string.h>

// TinyFATFS emits a desktop main() that calls setup()/loop(); this test has
// its own main(), so suppress it.
#define FATFS_NO_MAIN
#include "fatfs.h"            // TinyFATFS
#include "SQLiteTinyFATFS.h"  // backend under test
#include "SQLiteFS.h"
#include "sqlite3.h"

static int g_failed = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "  CHECK failed at %s:%d: %s\n", __FILE__,       \
              __LINE__, #cond);                                        \
      g_failed++;                                                      \
      return;                                                          \
    }                                                                  \
  } while (0)

static long long queryInt(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  long long v = -1;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return -1;
  if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
  sqlite3_finalize(st);
  return v;
}

static void test_persistence_on_fat_volume(void) {
  static RamIO drv{400, 512};  // 200 KiB RAM disk
  CHECK(SD.begin(drv));

  SQLiteTinyFATFS backend(SD.getFatFs());
  SQLiteVFS::setFileSystem(&backend);
  CHECK(SQLiteVFS::fileSystem() == &backend);

  sqlite3* db = nullptr;
  CHECK(sqlite3_open("/app.db", &db) == SQLITE_OK);
  {
    int rc = sqlite3_exec(db, "CREATE TABLE r(id INTEGER PRIMARY KEY, v TEXT);",
                          nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) fprintf(stderr, "  create rc=%d msg=%s\n", rc, sqlite3_errmsg(db));
    CHECK(rc == SQLITE_OK);
  }
  CHECK(sqlite3_exec(db,
                     "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 "
                     "FROM c WHERE x < 200) INSERT INTO r(v) SELECT 'row' || x FROM c;",
                     nullptr, nullptr, nullptr) == SQLITE_OK);
  CHECK(queryInt(db, "SELECT COUNT(*) FROM r;") == 200);
  CHECK(sqlite3_close(db) == SQLITE_OK);

  // The database must exist on the FAT volume and the journal must be gone.
  CHECK(SD.exists("/app.db"));
  CHECK(!SD.exists("/app.db-journal"));

  // Reopen through a new connection: data must have been written to the volume.
  CHECK(sqlite3_open("/app.db", &db) == SQLITE_OK);
  CHECK(queryInt(db, "SELECT COUNT(*) FROM r;") == 200);
  CHECK(queryInt(db, "SELECT COUNT(*) FROM r WHERE v = 'row77';") == 1);
  CHECK(sqlite3_close(db) == SQLITE_OK);

  // Rolled-back changes must not reach the volume.
  CHECK(sqlite3_open("/app.db", &db) == SQLITE_OK);
  CHECK(sqlite3_exec(db, "BEGIN; DELETE FROM r; ROLLBACK;", nullptr, nullptr,
                     nullptr) == SQLITE_OK);
  CHECK(queryInt(db, "SELECT COUNT(*) FROM r;") == 200);
  CHECK(sqlite3_close(db) == SQLITE_OK);

  // Restore the default backend for the remaining tests.
  SQLiteVFS::setFileSystem(nullptr);
  CHECK(SQLiteVFS::fileSystem() != &backend);
}

static void test_open_missing_file_fails(void) {
  SQLiteTinyFATFS backend(SD.getFatFs());
  SQLiteVFS::setFileSystem(&backend);
  sqlite3* db = nullptr;
  // Read-only open of a missing file must fail, not create it.
  int rc = sqlite3_open_v2("/missing.db", &db, SQLITE_OPEN_READONLY, nullptr);
  CHECK(rc != SQLITE_OK);
  CHECK(!SD.exists("/missing.db"));
  sqlite3_close(db);
  SQLiteVFS::setFileSystem(nullptr);
}

int main(void) {
  printf("[ RUN  ] test_persistence_on_fat_volume\n");
  test_persistence_on_fat_volume();
  printf(g_failed ? "[FAILED] test_persistence_on_fat_volume\n"
                  : "[  OK  ] test_persistence_on_fat_volume\n");
  int before = g_failed;

  printf("[ RUN  ] test_open_missing_file_fails\n");
  test_open_missing_file_fails();
  printf(g_failed > before ? "[FAILED] test_open_missing_file_fails\n"
                           : "[  OK  ] test_open_missing_file_fails\n");

  printf("\nTinyFATFS backend: %s\n", g_failed ? "FAILED" : "all passed");
  return g_failed ? 1 : 0;
}
