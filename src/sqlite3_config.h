/*
 * SQLite build configuration for Arduino boards.
 *
 * This header is prepended to the upstream amalgamation (src/sqlite3.c) by
 * tools/migrate_upstream.py, so every macro below is seen before sqlite3.c.
 * Edit it here, never in the generated sqlite3.c.
 *
 * Design goals:
 *   - no dependence on a POSIX VFS: the file system is provided by
 *     src/sqlite_vfs_stdio.c (stdio based), registered by sqlite3_os_init().
 *   - small RAM footprint: small page cache, no mmap, no shared cache, no WAL.
 *   - no hidden allocations in temp storage: temp tables live in memory.
 */
#ifndef SQLITE3_CONFIG_ARDUINO_H
#define SQLITE3_CONFIG_ARDUINO_H

/* --- assertions: off. Arduino force-includes Arduino.h (and thus assert.h)
 *     before this file, so defining NDEBUG here alone has no effect. Make
 *     assert() itself a no-op. SQLite's assert-only helper functions are
 *     compiled only with SQLITE_DEBUG, so live asserts would not link. ----- */
/* Pull in the C library headers that (re)define assert() FIRST, e.g. newlib's
 * sys/reent.h includes assert.h, so later includes cannot undo the override. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef assert
#undef assert
#endif
#define assert(X) ((void)0)

/* --- OS layer: provided by the application / src/sqlite_vfs.cpp ------ */
#define SQLITE_OS_OTHER                 1
#define SQLITE_OS_UNIX                  0
#define SQLITE_OS_WIN                   0

/* --- threading: MCU cores run SQLite from a single context ---------------- */
#define SQLITE_THREADSAFE               0

/* --- features not available on MCUs ---------------------------------------- */
#define SQLITE_OMIT_LOAD_EXTENSION      1
#define SQLITE_OMIT_WAL                 1   /* needs shared-memory (xShmMap)  */
#define SQLITE_OMIT_SHARED_CACHE        1
#define SQLITE_OMIT_LOCALTIME           1   /* no time zone database          */
#define SQLITE_OMIT_DEPRECATED          1
#define SQLITE_OMIT_UTF16               1
#define SQLITE_MAX_MMAP_SIZE            0
#define SQLITE_DEFAULT_MMAP_SIZE        0

/* --- safety / defaults ------------------------------------------------------ */
#define SQLITE_DQS                      0   /* double-quoted strings = errors */
#define SQLITE_DEFAULT_FOREIGN_KEYS     1
#define SQLITE_TEMP_STORE               3   /* temp data always in memory     */
#define SQLITE_DEFAULT_MEMSTATUS        0   /* skip global memory accounting  */
#define SQLITE_DEFAULT_CACHE_SIZE       (-32)   /* page cache: 32 KiB; raise with PRAGMA cache_size */
#define SQLITE_DEFAULT_PCACHE_INITSZ    8
#define SQLITE_DEFAULT_LOOKASIDE        0,0 /* no lookaside allocator         */
#define SQLITE_SMALL_STACK              1

/* --- code the library never uses, removed to save flash ------------------- */
#define SQLITE_OMIT_PROGRESS_CALLBACK   1
#define SQLITE_OMIT_TRACE               1
#define SQLITE_OMIT_COMPILEOPTION_DIAGS 1
#define SQLITE_OMIT_EXPLAIN             1   /* EXPLAIN output is for debugging */
#define SQLITE_OMIT_INTEGRITY_CHECK     1   /* run integrity_check on the desktop */
#define SQLITE_OMIT_LOOKASIDE           1   /* lookaside is disabled anyway */
#define SQLITE_OMIT_XFER_OPT            1   /* INSERT ... SELECT transfer shortcut */

/* --- limits: bound the memory a single statement or value can take ---------
 * The defaults (1 GB strings, 2000 columns, 1000-deep expressions) allow queries
 * that would exhaust a microcontroller's RAM. Raise them if your data needs it. */
#define SQLITE_MAX_LENGTH               262144  /* 256 KiB per string or blob */
#define SQLITE_MAX_SQL_LENGTH           8192    /* SQL text per statement */
#define SQLITE_MAX_COLUMN               64      /* columns per table / result */
#define SQLITE_MAX_EXPR_DEPTH           64      /* parser recursion depth */
#define SQLITE_MAX_VARIABLE_NUMBER      99      /* ? parameters per statement */
#define SQLITE_MAX_COMPOUND_SELECT      16      /* UNION chain length */

/* --- extensions ------------------------------------------------------------
 * Loadable extensions are not available (SQLITE_OMIT_LOAD_EXTENSION), so
 * extensions are compiled in. Defaults:
 *   - JSON functions (json_*)  : always on (part of the core build)
 *   - math functions (sin, log, ...) : on; define ARDUINO_SQLITE_NO_MATH to drop
 *   - R*Tree                   : off; define ARDUINO_SQLITE_ENABLE_RTREE to add
 * Not included: FTS3/4/5, session, RBU, dbstat, dbpage and the ext/misc
 * modules (series, regexp, percentile) which are not in sqlite3.c.
 */
#ifndef ARDUINO_SQLITE_NO_MATH
#define SQLITE_ENABLE_MATH_FUNCTIONS    1
#endif

#ifdef ARDUINO_SQLITE_ENABLE_RTREE
#define SQLITE_ENABLE_RTREE             1
#endif

#endif /* SQLITE3_CONFIG_ARDUINO_H */
