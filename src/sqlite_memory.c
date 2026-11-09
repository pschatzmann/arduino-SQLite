/*
 * SQLite memory allocator with optional PSRAM (external RAM) placement.
 * See SQLiteMemory.h for the policy and the per-platform hooks.
 *
 * Every block carries an 8-byte header with its size and origin, so that
 * xFree/xRealloc/xSize work for blocks from either heap.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SQLiteMemory.h"
#include "sqlite3.h"

#if defined(ARDUINO_ARCH_ESP32) && !defined(SQLITE_PSRAM_ALLOC)
#include "esp_heap_caps.h"
#define SQLITE_PSRAM_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define SQLITE_PSRAM_FREE(p) heap_caps_free(p)
#define SQLITE_PSRAM_AVAILABLE() (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0)
#endif

#if defined(ARDUINO_ARCH_RP2040) && defined(RP2350_PSRAM_CS) && !defined(SQLITE_PSRAM_ALLOC)
/* arduino-pico core (RP2350 boards with PSRAM, RP2350_PSRAM_CS defined). */
/* C-linkage wrappers, defined in sqlite_psram_rp2.cpp. */
void *sqlite_psram_alloc(size_t n);
void sqlite_psram_free(void *p);
size_t sqlite_psram_total(void);
#define SQLITE_PSRAM_ALLOC(n) sqlite_psram_alloc(n)
#define SQLITE_PSRAM_FREE(p) sqlite_psram_free(p)
#define SQLITE_PSRAM_AVAILABLE() (sqlite_psram_total() > 0)
#endif

#ifndef SQLITE_PSRAM_ALLOC
#define SQLITE_PSRAM_ALLOC(n) ((void)(n), (void *)0)
#define SQLITE_PSRAM_FREE(p) ((void)(p))
#define SQLITE_PSRAM_AVAILABLE() 0
#endif

/* Header in front of every block. 8 bytes keeps the user pointer 8-aligned. */
typedef struct {
  uint32_t size;   /* requested size */
  uint32_t psram;  /* 1 if the block lives in external RAM */
} BlockHeader;

#define HEADER_SIZE ((int)sizeof(BlockHeader))

static int g_use_psram = 0;

static void *blockAlloc(sqlite3_int64 n) {
  BlockHeader *h = NULL;
  size_t total;
  if (n <= 0 || n > 0x7FFFFFFF) return NULL;
  total = (size_t)n + HEADER_SIZE;

  if (g_use_psram) {
    h = (BlockHeader *)SQLITE_PSRAM_ALLOC(total);
    if (h) {
      h->psram = 1;
      h->size = (uint32_t)n;
      return (char *)h + HEADER_SIZE;
    }
    /* External RAM exhausted: fall back to internal RAM. */
  }
  h = (BlockHeader *)malloc(total);
  if (h == NULL) return NULL;
  h->psram = 0;
  h->size = (uint32_t)n;
  return (char *)h + HEADER_SIZE;
}

static void *xMalloc(int n) { return blockAlloc((sqlite3_int64)n); }

static void xFree(void *p) {
  BlockHeader *h;
  if (p == NULL) return;
  h = (BlockHeader *)((char *)p - HEADER_SIZE);
  if (h->psram) {
    SQLITE_PSRAM_FREE(h);
  } else {
    free(h);
  }
}

static void *xRealloc(void *p, int n) {
  BlockHeader *h;
  void *q;
  uint32_t keep;
  if (p == NULL) return xMalloc(n);
  if (n <= 0) {
    xFree(p);
    return NULL;
  }
  h = (BlockHeader *)((char *)p - HEADER_SIZE);
  keep = (uint32_t)n < h->size ? (uint32_t)n : h->size;
  q = blockAlloc((sqlite3_int64)n);
  if (q == NULL) return NULL;  /* original block stays valid */
  memcpy(q, p, keep);
  xFree(p);
  return q;
}

static int xSize(void *p) {
  BlockHeader *h;
  if (p == NULL) return 0;
  h = (BlockHeader *)((char *)p - HEADER_SIZE);
  return (int)h->size;
}

static int xRoundup(int n) {
  return (n + 7) & ~7;
}

static int xInit(void *pAppData) {
  (void)pAppData;
  return SQLITE_OK;
}

static void xShutdown(void *pAppData) {
  (void)pAppData;
}

static const sqlite3_mem_methods psramMethods = {
    xMalloc, xFree, xRealloc, xSize, xRoundup, xInit, xShutdown, NULL};

int sqlite_memory_psram_available(void) {
  return SQLITE_PSRAM_AVAILABLE() ? 1 : 0;
}

/* The allocator that was active before we replaced it. */
static sqlite3_mem_methods builtinMethods;
static int haveBuiltin = 0;

int sqlite_memory_use_psram(int enable) {
  if (!enable) {
    /* Restoring the built-in allocator is only possible before init. */
    if (g_use_psram && haveBuiltin &&
        sqlite3_config(SQLITE_CONFIG_MALLOC, &builtinMethods) == SQLITE_OK) {
      g_use_psram = 0;
    }
    return g_use_psram;
  }
  if (g_use_psram) return 1;
  if (!sqlite_memory_psram_available()) return 0;
  if (sqlite3_config(SQLITE_CONFIG_GETMALLOC, &builtinMethods) == SQLITE_OK) {
    haveBuiltin = 1;
  }
  if (sqlite3_config(SQLITE_CONFIG_MALLOC, &psramMethods) != SQLITE_OK) {
    return 0; /* already initialized: too late to change the allocator */
  }
  g_use_psram = 1;
  return 1;
}
