/*
 * Memory allocation policy for SQLite on boards with PSRAM (external RAM).
 *
 * SQLite allocates its page cache, prepared statements and other working
 * memory dynamically. When sqlite_memory_use_psram(1) is called before the
 * first sqlite3_open(), these allocations are served from PSRAM if the board
 * has it, and from internal RAM otherwise. Each block records where it came
 * from, so freeing is always correct.
 *
 * Support per platform:
 *   - ESP32 with PSRAM (heap_caps API): supported.
 *   - RP2350 boards with PSRAM (arduino-pico core, RP2350_PSRAM_CS defined):
 *     supported via __psram_malloc/__psram_free.
 *   - Other boards: no PSRAM hook by default, so it is a no-op (returns 0).
 *     A board port can provide its own by defining these macros before this
 *     header is included (or via build flags):
 *       SQLITE_PSRAM_ALLOC(n)        allocate n bytes in external RAM, or NULL
 *       SQLITE_PSRAM_FREE(p)         free a block from SQLITE_PSRAM_ALLOC
 *       SQLITE_PSRAM_AVAILABLE()     non-zero if external RAM is present
 */
#ifndef SQLITE_MEMORY_H
#define SQLITE_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

/// Routes SQLite's dynamic allocations to PSRAM when available.
/// Call once, before the first sqlite3_open(). Returns 1 if the policy is in
/// effect, 0 if PSRAM is not available or SQLite was already initialized.
int sqlite_memory_use_psram(int enable);

/// Non-zero if the board has usable external RAM.
int sqlite_memory_psram_available(void);

#ifdef __cplusplus
}
#endif

#endif  // SQLITE_MEMORY_H
