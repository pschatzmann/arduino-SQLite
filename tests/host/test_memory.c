/*
 * Host test for the PSRAM allocation policy (src/sqlite_memory.c).
 *
 * The host has no PSRAM, so a mock "external RAM" heap is wired in through the
 * SQLITE_PSRAM_* hooks. The mock counts live blocks and can be made to run out,
 * which exercises the fallback to internal RAM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

static int mock_live = 0;    /* blocks currently allocated from the mock heap */
static int mock_max = 0;     /* peak of mock_live */
static int mock_quota = -1;  /* remaining allocations; -1 = unlimited, 0 = exhausted */

static void *mock_alloc(size_t n) {
  void *p;
  if (mock_quota == 0) return NULL;
  if (mock_quota > 0) mock_quota--;
  p = malloc(n);
  if (p) {
    mock_live++;
    if (mock_live > mock_max) mock_max = mock_live;
  }
  return p;
}

static void mock_free(void *p) {
  mock_live--;
  free(p);
}

#define SQLITE_PSRAM_ALLOC(n) mock_alloc(n)
#define SQLITE_PSRAM_FREE(p) mock_free(p)
#define SQLITE_PSRAM_AVAILABLE() 1

/* Compile the unit under test in this translation unit, with the hooks above. */
#include "../../src/sqlite_memory.c"

static int g_failed = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "  CHECK failed at %s:%d: %s\n", __FILE__,      \
              __LINE__, #cond);                                       \
      g_failed++;                                                     \
      return;                                                         \
    }                                                                 \
  } while (0)

static int exec(sqlite3 *db, const char *sql) {
  return sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static void test_enable_before_init(void) {
  CHECK(sqlite_memory_psram_available() == 1);
  CHECK(sqlite_memory_use_psram(1) == 1);
}

static void test_workload_uses_psram(void) {
  sqlite3 *db = NULL;
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE t(x INTEGER, s TEXT);") == SQLITE_OK);
  CHECK(exec(db, "WITH RECURSIVE c(i) AS (SELECT 1 UNION ALL SELECT i+1 "
                 "FROM c WHERE i < 500) INSERT INTO t SELECT i, zeroblob(64) "
                 "FROM c;") == SQLITE_OK);
  CHECK(mock_max > 0);  /* SQLite's dynamic memory came from the mock heap */
  sqlite3_close(db);
}

static void test_fallback_when_psram_exhausted(void) {
  sqlite3 *db = NULL;
  mock_quota = 0;  /* every PSRAM allocation fails from now on */
  CHECK(sqlite3_open(":memory:", &db) == SQLITE_OK);
  CHECK(exec(db, "CREATE TABLE f(x INTEGER);") == SQLITE_OK);
  CHECK(exec(db, "INSERT INTO f VALUES (1),(2),(3);") == SQLITE_OK);
  sqlite3_close(db);
  mock_quota = -1;
}

static void test_size_and_realloc_keep_contents(void) {
  unsigned char *p = (unsigned char *)sqlite3_malloc(100);
  unsigned char *q;
  int i;
  CHECK(p != NULL);
  for (i = 0; i < 100; i++) p[i] = (unsigned char)i;
  CHECK(sqlite3_msize(p) >= 100);  /* SQLite may round up */
  q = (unsigned char *)sqlite3_realloc(p, 300);
  CHECK(q != NULL);
  CHECK(sqlite3_msize(q) >= 300);
  for (i = 0; i < 100; i++) CHECK(q[i] == (unsigned char)i);
  sqlite3_free(q);
}

static void test_blocks_are_released(void) {
  int before = mock_live;
  void *p = sqlite3_malloc(4096);
  CHECK(p != NULL);
  CHECK(mock_live >= before + 1);
  sqlite3_free(p);
  CHECK(mock_live == before);
}

static void test_enable_after_init_is_refused_or_idempotent(void) {
  /* Already in effect: reports 1 and does not reconfigure. */
  CHECK(sqlite_memory_use_psram(1) == 1);
}

/* Runs one test and reports its own result (g_failed is cumulative). */
#define RUN_TEST(fn)                                          \
  do {                                                        \
    int before_ = g_failed;                                   \
    printf("[ RUN  ] %s\n", #fn);                             \
    fn();                                                     \
    printf(g_failed > before_ ? "[FAILED] %s\n" : "[  OK  ] %s\n", #fn); \
  } while (0)

int main(void) {
  RUN_TEST(test_enable_before_init);
  RUN_TEST(test_workload_uses_psram);
  RUN_TEST(test_fallback_when_psram_exhausted);
  RUN_TEST(test_size_and_realloc_keep_contents);
  RUN_TEST(test_blocks_are_released);
  RUN_TEST(test_enable_after_init_is_refused_or_idempotent);

  printf("\nPSRAM allocator: %s (peak mock blocks: %d)\n",
         g_failed ? "FAILED" : "all passed", mock_max);
  return g_failed ? 1 : 0;
}
