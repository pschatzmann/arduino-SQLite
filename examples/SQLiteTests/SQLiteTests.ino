/*
 * Hardware test suite for the SQLite Arduino library (ESP32).
 *
 * Runs every test once, prints one PASS/FAIL line per check to the serial
 * monitor at 115200 baud, and ends with a summary line:
 *     RESULT: PASS <n>/<n>      or      RESULT: FAIL <failed>/<total>
 * The LittleFS tests format the "spiffs" partition if it cannot be mounted.
 */
#include <math.h>
#include <LittleFS.h>
#include <SQLite.h>
#include <SQLiteMemory.h>

static int g_total = 0;
static int g_failed = 0;
static const char *g_test = "";

#define CHECK(cond)                                                              \
  do {                                                                           \
    g_total++;                                                                   \
    if (!(cond)) {                                                               \
      g_failed++;                                                                \
      Serial.printf("  FAIL %s: line %d: %s\n", g_test, __LINE__, #cond);        \
    }                                                                            \
  } while (0)

static void banner(const char *name) {
  g_test = name;
  Serial.printf("[ RUN  ] %s\n", name);
}

static void done(const char *name, int failedBefore) {
  Serial.printf("[%s] %s  (free heap %u, min %u)\n",
                g_failed > failedBefore ? "FAIL" : " OK ", name,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

// ---------------------------------------------------------------- SQL basics

static void testSqlBasics() {
  banner("sql_basics");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  CHECK(db.isOpen());
  CHECK(db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, v REAL);") == SQLITE_OK);
  CHECK(db.tableExists("t"));
  CHECK(!db.tableExists("nope"));
  CHECK(db.exec("INSERT INTO t(name, v) VALUES('a', 1.5), ('b', 2.5), ('c', NULL);") == SQLITE_OK);
  CHECK(db.getChanges() == 3);
  CHECK(db.getLastInsertRowid() == 3);
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 3);
  CHECK(db.execAndGetInt("SELECT COUNT(v) FROM t;") == 2);
  CHECK(db.execAndGetInt("SELECT id FROM t WHERE 0;", 77) == 77);
  CHECK(!db.hasError());
  done("sql_basics", f0);
}

static void testStatementBinding() {
  banner("statement_binding");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE b(i INTEGER, l INTEGER, d REAL, s TEXT, x BLOB, n);");
  const uint8_t blob[5] = {0, 1, 2, 254, 255};
  {
    SQLite::Statement ins(db, "INSERT INTO b VALUES(?1,?2,?3,?4,?5,?6)");
    CHECK(ins.isValid());
    CHECK(ins.getBindParameterCount() == 6);
    CHECK(ins.getIndex("?4") == 4);
    ins.bind(1, -42);
    ins.bind(2, 9007199254740993LL);
    ins.bind(3, 3.14159);
    ins.bind(4, "hello world");
    ins.bind(5, blob, sizeof(blob));
    ins.bindNull(6);
    CHECK(ins.exec() == 1);
  }
  SQLite::Statement q(db, "SELECT i, l, d, s, x, n FROM b;");
  CHECK(q.getColumnCount() == 6);
  CHECK(strcmp(q.getColumnName(3), "s") == 0);
  CHECK(q.executeStep());
  CHECK(q.getColumn(0).getInt() == -42);
  CHECK(q.getColumn(1).getInt64() == 9007199254740993LL);
  CHECK(fabs(q.getColumn(2).getDouble() - 3.14159) < 1e-9);
  CHECK(strcmp(q.getColumn(3).getText(), "hello world") == 0);
  CHECK(q.getColumn(4).getBytes() == 5);
  CHECK(memcmp(q.getColumn(4).getBlob(), blob, 5) == 0);
  CHECK(q.getColumn(5).isNull());
  CHECK(q.getColumn(0).getType() == SQLite::INTEGER);
  CHECK(q.getColumn(3).getType() == SQLite::TEXT);
  CHECK(q.getColumn(4).getType() == SQLite::BLOB);
  CHECK(!q.executeStep());
  done("statement_binding", f0);
}

static void testReuseAndReset() {
  banner("statement_reuse");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE r(x INTEGER);");
  SQLite::Statement ins(db, "INSERT INTO r VALUES(?)");
  for (int i = 1; i <= 100; i++) {
    ins.bind(1, i);
    CHECK(ins.exec() == 1);
    ins.reset();
    ins.clearBindings();
  }
  CHECK(db.execAndGetInt("SELECT SUM(x) FROM r;") == 5050);
  SQLite::Statement sum(db, "SELECT SUM(x) FROM r;");
  CHECK(sum.executeStep() && sum.getColumn(0).getInt64() == 5050);
  sum.reset();
  CHECK(sum.executeStep());  // reset makes the statement run again
  done("statement_reuse", f0);
}

static void testTransactions() {
  banner("transactions");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE t(x);");
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(1),(2),(3);");
    CHECK(tx.commit());
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 3);
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(4);");
    // no commit: rolled back by the destructor
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 3);
  {
    SQLite::Transaction tx(db);
    db.exec("INSERT INTO t VALUES(5);");
    CHECK(tx.rollback());
    CHECK(!tx.commit());
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 3);
  done("transactions", f0);
}

static void testErrors() {
  banner("errors");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  CHECK(db.exec("SELECT * FROM missing_table;") != SQLITE_OK);
  CHECK(db.hasError());
  CHECK(strstr(db.getErrorMsg(), "no such table") != nullptr);
  db.exec("CREATE TABLE u(k TEXT PRIMARY KEY);");
  db.exec("INSERT INTO u VALUES('a');");
  CHECK(db.exec("INSERT INTO u VALUES('a');") == SQLITE_CONSTRAINT);
  SQLite::Statement bad(db, "SELEC nonsense");
  CHECK(!bad.isValid());
  CHECK(bad.hasError());
  CHECK(!bad.executeStep());
  SQLite::Statement sel(db, "SELECT k FROM u;");
  CHECK(sel.exec() == -1);  // rows returned: exec() is the wrong call
  CHECK(sel.hasError());
  // the connection is still usable after errors
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM u;") == 1);
  done("errors", f0);
}

static void testMathJsonDate() {
  banner("math_json_date");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  CHECK(db.execAndGetInt("SELECT CAST(sqrt(144) AS INTEGER);") == 12);
  CHECK(db.execAndGetInt("SELECT CAST(sin(0)*1000 AS INTEGER);") == 0);
  CHECK(db.execAndGetInt("SELECT CAST(ROUND(ln(exp(3))*100) AS INTEGER);") == 300);  // ln = natural log; log = base 10
  CHECK(db.execAndGetInt("SELECT json_array_length('[1,2,3,4]');") == 4);
  CHECK(db.execAndGetInt("SELECT json_extract('{\"a\":{\"b\":42}}', '$.a.b');") == 42);
  CHECK(db.execAndGetInt("SELECT CAST(strftime('%s','2026-01-01 00:00:00') AS INTEGER);") == 1767225600);
  CHECK(db.execAndGetInt("SELECT CAST(strftime('%Y', '2026-11-10') AS INTEGER);") == 2026);
  done("math_json_date", f0);
}

static void testBulkInMemory() {
  banner("bulk_memory");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE big(i INTEGER PRIMARY KEY, v INTEGER, s TEXT);");
  const int n = 3000;
  {
    SQLite::Transaction tx(db);
    SQLite::Statement ins(db, "INSERT INTO big VALUES(?,?,?)");
    char buf[32];
    for (int i = 1; i <= n; i++) {
      snprintf(buf, sizeof(buf), "row-%05d", i);
      ins.bind(1, i);
      ins.bind(2, (long long)i * i);
      ins.bind(3, buf);
      CHECK(ins.exec() == 1);
      ins.reset();
      ins.clearBindings();
    }
    CHECK(tx.commit());
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM big;") == n);
  CHECK(db.execAndGetInt("SELECT SUM(i) FROM big;") == (long long)n * (n + 1) / 2);
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM big WHERE s = 'row-01500';") == 1);
  CHECK(db.execAndGetInt("SELECT v FROM big WHERE i = 2999;") == 2999LL * 2999);
  // aggregates, ordering and grouping over the full table
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM (SELECT i % 10 AS g FROM big GROUP BY g);") == 10);
  CHECK(db.execAndGetInt("SELECT i FROM big ORDER BY v DESC LIMIT 1;") == n);
  done("bulk_memory", f0);
}

static void testIndexesAndJoins() {
  banner("indexes_joins");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE c(id INTEGER PRIMARY KEY, name TEXT UNIQUE);"
          "CREATE TABLE o(id INTEGER PRIMARY KEY, cid INTEGER REFERENCES c(id), amount REAL);"
          "CREATE INDEX o_cid ON o(cid);");
  db.exec("INSERT INTO c(name) VALUES('alice'),('bob'),('carol');");
  {
    SQLite::Transaction tx(db);
    for (int i = 0; i < 300; i++) {
      char sql[96];
      snprintf(sql, sizeof(sql), "INSERT INTO o(cid, amount) VALUES(%d, %d.5);", (i % 3) + 1, i);
      CHECK(db.exec(sql) == SQLITE_OK);
    }
    CHECK(tx.commit());
  }
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM o WHERE cid = 2;") == 100);
  CHECK(db.execAndGetInt("SELECT COUNT(*) FROM o JOIN c ON c.id = o.cid WHERE c.name = 'carol';") == 100);
  SQLite::Statement q(db,
      "SELECT c.name, COUNT(*), SUM(o.amount) FROM c JOIN o ON o.cid = c.id "
      "GROUP BY c.name ORDER BY c.name;");
  int rows = 0;
  while (q.executeStep()) {
    rows++;
    CHECK(q.getColumn(1).getInt() == 100);
  }
  CHECK(rows == 3);
  // constraint: foreign keys are enabled by default in this build
  CHECK(db.exec("INSERT INTO o(cid, amount) VALUES(99, 1.0);") == SQLITE_CONSTRAINT);
  done("indexes_joins", f0);
}

static void testLargeValues() {
  banner("large_values");
  int f0 = g_failed;
  SQLite::Database db(":memory:");
  db.exec("CREATE TABLE l(id INTEGER, data BLOB, txt TEXT);");
  static uint8_t data[4096];
  for (int i = 0; i < (int)sizeof(data); i++) data[i] = (uint8_t)(i * 31 + 7);
  static char text[2048];
  for (int i = 0; i < (int)sizeof(text) - 1; i++) text[i] = 'a' + (i % 26);
  text[sizeof(text) - 1] = '\0';
  {
    SQLite::Statement ins(db, "INSERT INTO l VALUES(1, ?, ?)");
    ins.bind(1, data, sizeof(data));
    ins.bind(2, text);
    CHECK(ins.exec() == 1);
  }
  SQLite::Statement q(db, "SELECT data, txt FROM l WHERE id = 1;");
  CHECK(q.executeStep());
  CHECK(q.getColumn(0).getBytes() == (int)sizeof(data));
  CHECK(memcmp(q.getColumn(0).getBlob(), data, sizeof(data)) == 0);
  CHECK(strcmp(q.getColumn(1).getText(), text) == 0);
  // a statement longer than SQLITE_MAX_SQL_LENGTH (8192) must be rejected cleanly
  static char longSql[9000];
  memset(longSql, ' ', sizeof(longSql) - 1);
  memcpy(longSql, "SELECT 1", 8);
  longSql[sizeof(longSql) - 1] = '\0';
  CHECK(db.exec(longSql) != SQLITE_OK);
  CHECK(db.execAndGetInt("SELECT 7;") == 7);  // connection still works
  done("large_values", f0);
}

// -------------------------------------------------------- file database (flash)

static const char *DB_PATH = "/littlefs/test.db";

static bool mountFlash() {
  if (LittleFS.begin(false)) return true;
  Serial.println("  LittleFS not mounted: formatting the flash partition");
  return LittleFS.begin(true);
}

static void testFileDatabase() {
  banner("file_database_flash");
  int f0 = g_failed;
  CHECK(mountFlash());
  LittleFS.remove("/test.db");
  LittleFS.remove("/test.db-journal");
  {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    CHECK(db.isOpen());
    db.exec("CREATE TABLE f(id INTEGER PRIMARY KEY, v TEXT, n REAL);");
    SQLite::Transaction tx(db);
    SQLite::Statement ins(db, "INSERT INTO f(v, n) VALUES(?, ?)");
    char buf[40];
    for (int i = 0; i < 1000; i++) {
      snprintf(buf, sizeof(buf), "value-%d", i);
      ins.bind(1, buf);
      ins.bind(2, i * 0.25);
      CHECK(ins.exec() == 1);
      ins.reset();
      ins.clearBindings();
    }
    CHECK(tx.commit());
  }
  // the journal must be gone after a clean close
  CHECK(!LittleFS.exists("/test.db-journal"));
  CHECK(LittleFS.exists("/test.db"));
  {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READWRITE);
    CHECK(db.isOpen());
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f;") == 1000);
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f WHERE v = 'value-777';") == 1);
    CHECK(fabs(db.execAndGetInt("SELECT CAST(SUM(n)*4 AS INTEGER) FROM f;") - 499500.0) < 0.5 * 4);
  }
  // a rolled-back change must not reach flash
  {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READWRITE);
    {
      SQLite::Transaction tx(db);
      db.exec("DELETE FROM f;");
    }
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f;") == 1000);  // DELETE was rolled back
  }
  {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READWRITE);
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f;") == 1000);
  }
  done("file_database_flash", f0);
}

static void testReadOnly() {
  banner("read_only");
  int f0 = g_failed;
  CHECK(mountFlash());
  {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READONLY);
    CHECK(db.isOpen());
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f;") == 1000);
    CHECK(db.exec("INSERT INTO f(v) VALUES('x');") != SQLITE_OK);  // refused
  }
  {
    // read-only open of a missing file must fail and must not create it
    SQLite::Database missing("/littlefs/absent.db", SQLite::OPEN_READONLY);
    CHECK(!missing.isOpen());
    CHECK(!LittleFS.exists("/absent.db"));
  }
  done("read_only", f0);
}

static void testFileBulkAndCachePressure() {
  banner("file_cache_pressure");
  int f0 = g_failed;
  CHECK(mountFlash());
  LittleFS.remove("/cache.db");
  LittleFS.remove("/cache.db-journal");
  const int n = 3000;  // well beyond the 32 KiB page cache
  uint32_t t0 = millis();
  {
    SQLite::Database db("/littlefs/cache.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    CHECK(db.isOpen());
    db.exec("CREATE TABLE p(id INTEGER PRIMARY KEY, payload BLOB);");
    uint8_t payload[120];
    SQLite::Transaction tx(db);
    SQLite::Statement ins(db, "INSERT INTO p(id, payload) VALUES(?, ?)");
    for (int i = 1; i <= n; i++) {
      memset(payload, i & 0xFF, sizeof(payload));
      ins.bind(1, i);
      ins.bind(2, payload, sizeof(payload));
      CHECK(ins.exec() == 1);
      ins.reset();
      ins.clearBindings();
    }
    CHECK(tx.commit());
  }
  uint32_t writeMs = millis() - t0;
  {
    // random reads across the whole file force page-cache eviction
    SQLite::Database db("/littlefs/cache.db", SQLite::OPEN_READONLY);
    CHECK(db.isOpen());
    SQLite::Statement q(db, "SELECT payload FROM p WHERE id = ?");
    uint32_t seed = 12345;
    int ok = 0;
    for (int k = 0; k < 500; k++) {
      seed = seed * 1103515245u + 12345u;
      int id = (int)((seed >> 8) % n) + 1;
      q.bind(1, id);
      if (q.executeStep()) {
        const uint8_t *p = (const uint8_t *)q.getColumn(0).getBlob();
        if (p && p[0] == (uint8_t)(id & 0xFF) && q.getColumn(0).getBytes() == 120) ok++;
      }
      q.reset();
      q.clearBindings();
    }
    CHECK(ok == 500);
    Serial.printf("  file bulk: %d rows written in %u ms; 500 random reads OK\n", n, (unsigned)writeMs);
  }
  done("file_cache_pressure", f0);
}

// ----------------------------------------------------------- stability and heap

static void testRepeatedOpenClose() {
  banner("heap_open_close_loop");
  int f0 = g_failed;
  // warm up: first use allocates SQLite's global state
  {
    SQLite::Database db(":memory:");
    db.exec("CREATE TABLE w(x);");
  }
  size_t before = ESP.getFreeHeap();
  for (int cycle = 0; cycle < 200; cycle++) {
    SQLite::Database db(":memory:");
    db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);");
    {
      SQLite::Transaction tx(db);
      SQLite::Statement ins(db, "INSERT INTO t(v) VALUES(?)");
      for (int i = 0; i < 20; i++) {
        ins.bind(1, "some text value");
        ins.exec();
        ins.reset();
        ins.clearBindings();
      }
      tx.commit();
    }
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM t;") == 20);
  }
  size_t after = ESP.getFreeHeap();
  long drift = (long)before - (long)after;
  Serial.printf("  heap before %u, after 200 cycles %u (drift %ld bytes)\n",
                (unsigned)before, (unsigned)after, drift);
  CHECK(drift < 2048);  // no leak beyond allocator noise
  done("heap_open_close_loop", f0);
}

static void testReopenFileManyTimes() {
  banner("file_reopen_loop");
  int f0 = g_failed;
  CHECK(mountFlash());
  size_t before = ESP.getFreeHeap();
  for (int i = 0; i < 100; i++) {
    SQLite::Database db(DB_PATH, SQLite::OPEN_READONLY);
    CHECK(db.isOpen());
    CHECK(db.execAndGetInt("SELECT COUNT(*) FROM f;") == 1000);
  }
  long drift = (long)before - (long)ESP.getFreeHeap();
  Serial.printf("  heap drift after 100 file reopens: %ld bytes\n", drift);
  CHECK(drift < 2048);
  done("file_reopen_loop", f0);
}

static void testPsramPolicy() {
  banner("psram_policy");
  int f0 = g_failed;
  // The classic ESP32 used here has no PSRAM: the policy must report 0 and
  // the database must still work with the default allocator.
  int enabled = sqlite_memory_use_psram(1);
  Serial.printf("  PSRAM available: %s, policy enabled: %d\n",
                sqlite_memory_psram_available() ? "yes" : "no", enabled);
  CHECK(enabled == sqlite_memory_psram_available());
  SQLite::Database db(":memory:");
  CHECK(db.execAndGetInt("SELECT 40 + 2;") == 42);
  done("psram_policy", f0);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }
  delay(200);
  Serial.printf("\nSQLite %s on %s, free heap %u\n", SQLite::Database::libVersion(),
                ESP.getChipModel(), (unsigned)ESP.getFreeHeap());

  // PSRAM mode is chosen at compile time and must be set before SQLite starts:
  //   default:              allocator = internal RAM only
  //   -DSQLITE_TEST_PSRAM=1: SQLite's dynamic memory goes to PSRAM when present
  // On a board without PSRAM the flag falls back to the default allocator.
#ifdef SQLITE_TEST_PSRAM
  int psramOn = sqlite_memory_use_psram(1);
  Serial.printf("PSRAM mode: requested; policy %s; hardware PSRAM %s\n",
                psramOn ? "ON" : "OFF (fallback)",
                sqlite_memory_psram_available() ? "present" : "absent");
#else
  Serial.println("PSRAM mode: off (internal RAM only)");
#endif
  uint32_t start = millis();

  testSqlBasics();
  testStatementBinding();
  testReuseAndReset();
  testTransactions();
  testErrors();
  testMathJsonDate();
  testBulkInMemory();
  testIndexesAndJoins();
  testLargeValues();
  testFileDatabase();
  testReadOnly();
  testFileBulkAndCachePressure();
  testRepeatedOpenClose();
  testReopenFileManyTimes();
  testPsramPolicy();

  Serial.printf("\nchecks: %d, failed: %d, time: %u ms, min free heap: %u, min free PSRAM: %u\n",
                g_total, g_failed, (unsigned)(millis() - start), (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getFreePsram());
  if (g_failed == 0) {
    Serial.printf("RESULT: PASS %d/%d\n", g_total - g_failed, g_total);
  } else {
    Serial.printf("RESULT: FAIL %d/%d\n", g_failed, g_total);
  }
}

void loop() {
}
