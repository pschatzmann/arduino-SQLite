/*
 * C++ shim for the arduino-pico PSRAM heap (RP2350 boards with PSRAM).
 *
 * The core declares __psram_* with C++ linkage, so the C allocator in
 * sqlite_memory.c cannot call them directly. This file wraps them with C
 * linkage. It is empty on all other platforms.
 */
#include <stddef.h>

#if defined(ARDUINO_ARCH_RP2040) && defined(RP2350_PSRAM_CS)
void *__psram_malloc(size_t size);
void __psram_free(void *ptr);
size_t __psram_total_space();

extern "C" void *sqlite_psram_alloc(size_t n) { return __psram_malloc(n); }
extern "C" void sqlite_psram_free(void *p) { __psram_free(p); }
extern "C" size_t sqlite_psram_total(void) { return __psram_total_space(); }
#endif
