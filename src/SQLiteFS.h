/*
 * File system backends for SQLite.
 *
 * SQLite talks to storage through its VFS layer (src/sqlite_vfs.cpp). That
 * layer calls a SQLiteFileSystem object, so the same database code runs on
 * any storage that can implement this small interface.
 *
 * Built-in backend:
 *   SQLiteStdioFileSystem  - C stdio (fopen/fread/...). Default. Paths are
 *                            passed to fopen(), so a file system must be
 *                            mounted where the core's C library can reach it.
 *
 * Other backends (header-only, included by the sketch):
 *   SQLiteTinyFATFS.h      - TinyFATFS (FAT on SD, RAM disk, ...).
 *
 * Select a backend before the first sqlite3_open():
 *   SQLiteVFS::setFileSystem(&myBackend);
 *   SQLiteVFS::setFileSystem(nullptr);   // back to the stdio default
 */
#ifndef SQLITE_FS_H
#define SQLITE_FS_H

#include <stdint.h>
#include <stdio.h>

/* The stdio backend needs file-system syscalls in the C library (_open,
 * _unlink, ftruncate, ...). Cores that provide them (ESP32, desktop) enable it
 * by default; others (RP2040, STM32) do not and must use another backend. */
#ifndef SQLITE_ARDUINO_STDIO
#if defined(ARDUINO_ARCH_ESP32) || defined(__unix__) || defined(__APPLE__)
#define SQLITE_ARDUINO_STDIO 1
#else
#define SQLITE_ARDUINO_STDIO 0
#endif
#endif

class SQLiteFileSystem {
 public:
  /// Open flags, combined with bitwise or.
  enum Mode : uint8_t {
    ReadOnly = 1,   ///< open for reading only
    ReadWrite = 2,  ///< open for reading and writing (default)
    Create = 4,     ///< create the file if it does not exist
  };

  virtual ~SQLiteFileSystem() = default;

  /// Opens a file. Returns an opaque handle, or nullptr on failure.
  virtual void* open(const char* path, uint8_t mode) = 0;

  /// Anonymous temporary file (used by SQLite for large sorts).
  /// Default: not supported (returns nullptr).
  virtual void* openTemporary() { return nullptr; }

  /// Reads up to n bytes at offset. Returns bytes read (fewer at end of file)
  /// or -1 on error.
  virtual int read(void* handle, void* buf, int n, int64_t offset) = 0;

  /// Writes n bytes at offset. Returns bytes written or -1 on error.
  virtual int write(void* handle, const void* buf, int n, int64_t offset) = 0;

  /// Sets the file length. Returns false on error.
  virtual bool truncate(void* handle, int64_t size) = 0;

  /// Flushes written data to the storage. Returns false on error.
  virtual bool sync(void* handle) = 0;

  /// Current file length in bytes, or -1 on error.
  virtual int64_t size(void* handle) = 0;

  /// Closes a handle returned by open() or openTemporary().
  virtual void close(void* handle) = 0;

  /// Deletes a file. Returns false if it does not exist or cannot be removed.
  virtual bool remove(const char* path) = 0;

  /// True if the file exists. If writable is true, also requires write access.
  virtual bool exists(const char* path, bool writable) = 0;
};

#if SQLITE_ARDUINO_STDIO
/// Backend on the C standard I/O library.
/// Define SQLITE_STDIO_NO_FTRUNCATE if the core has no ftruncate(); then
/// truncate() fails and SQLite cannot shrink database files.
class SQLiteStdioFileSystem : public SQLiteFileSystem {
 public:
  void* open(const char* path, uint8_t mode) override {
    FILE* fp = nullptr;
    if (mode & ReadOnly) {
      fp = fopen(path, "rb");
    } else {
      fp = fopen(path, "r+b");
      if (fp == nullptr && (mode & Create)) fp = fopen(path, "w+b");
    }
    return fp;
  }

  void* openTemporary() override { return tmpfile(); }

  int read(void* handle, void* buf, int n, int64_t offset) override {
    FILE* fp = static_cast<FILE*>(handle);
    if (fseek(fp, (long)offset, SEEK_SET) != 0) return -1;
    return (int)fread(buf, 1, (size_t)n, fp);
  }

  int write(void* handle, const void* buf, int n, int64_t offset) override {
    FILE* fp = static_cast<FILE*>(handle);
    if (fseek(fp, (long)offset, SEEK_SET) != 0) return -1;
    return (int)fwrite(buf, 1, (size_t)n, fp);
  }

  bool truncate(void* handle, int64_t size) override;

  bool sync(void* handle) override {
    return fflush(static_cast<FILE*>(handle)) == 0;
  }

  int64_t size(void* handle) override {
    FILE* fp = static_cast<FILE*>(handle);
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    long end = ftell(fp);
    return end < 0 ? -1 : (int64_t)end;
  }

  void close(void* handle) override { fclose(static_cast<FILE*>(handle)); }

  bool remove(const char* path) override {
    FILE* probe = fopen(path, "rb");
    if (probe == nullptr) return false;
    fclose(probe);
    return ::remove(path) == 0;
  }

  bool exists(const char* path, bool writable) override {
    FILE* fp = fopen(path, writable ? "r+b" : "rb");
    if (fp == nullptr) return false;
    fclose(fp);
    return true;
  }
};

#endif  // SQLITE_ARDUINO_STDIO

namespace SQLiteVFS {
/// Selects the backend used for files opened after this call.
/// Pass nullptr to restore the default: the stdio backend where it is
/// available, otherwise no backend (file databases then fail to open with
/// SQLITE_CANTOPEN; in-memory databases always work). The object must outlive
/// every database opened through it.
void setFileSystem(SQLiteFileSystem* fs);

/// The backend currently in use, or nullptr if none.
SQLiteFileSystem* fileSystem();
}  // namespace SQLiteVFS

#endif  // SQLITE_FS_H
