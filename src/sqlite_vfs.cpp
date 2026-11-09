/*
 * SQLite VFS that delegates all file access to a SQLiteFileSystem backend
 * (see SQLiteFS.h).
 *
 * SQLite is compiled with SQLITE_OS_OTHER=1 (sqlite3_config.h), so this file
 * provides sqlite3_os_init()/sqlite3_os_end(). It registers one VFS named
 * "arduino" as the default VFS.
 *
 * Limitations (single-process embedded use):
 *   - locking is a no-op: only one connection may write a file at a time.
 *   - journal_mode=WAL is not supported (SQLITE_OMIT_WAL).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "SQLiteFS.h"
#include "sqlite3.h"

#ifndef SQLITE_STDIO_NO_FTRUNCATE
#include <unistd.h>
#endif

#define VFS_MAX_PATHNAME 512

// ---------------------------------------------------------------------------
// Stdio backend (declared in SQLiteFS.h)
// ---------------------------------------------------------------------------

#if SQLITE_ARDUINO_STDIO
bool SQLiteStdioFileSystem::truncate(void* handle, int64_t size) {
#ifndef SQLITE_STDIO_NO_FTRUNCATE
  FILE* fp = static_cast<FILE*>(handle);
  if (fflush(fp) != 0) return false;
  return ftruncate(fileno(fp), (off_t)size) == 0;
#else
  (void)handle;
  (void)size;
  return false;
#endif
}
#endif  // SQLITE_ARDUINO_STDIO

// ---------------------------------------------------------------------------
// VFS state
// ---------------------------------------------------------------------------

namespace {

#if SQLITE_ARDUINO_STDIO
SQLiteStdioFileSystem defaultFileSystem;
#endif
SQLiteFileSystem* defaultBackend() {
#if SQLITE_ARDUINO_STDIO
  return &defaultFileSystem;
#else
  return nullptr;
#endif
}
SQLiteFileSystem* activeFileSystem = defaultBackend();

/// A SQLite file object: the base struct must come first.
struct BackendFile {
  sqlite3_file base;
  SQLiteFileSystem* fs;  // backend that opened the file
  void* handle;          // backend handle
  int readOnly;
};

}  // namespace

namespace SQLiteVFS {
void setFileSystem(SQLiteFileSystem* fs) {
  activeFileSystem = fs ? fs : defaultBackend();
}
SQLiteFileSystem* fileSystem() { return activeFileSystem; }
}  // namespace SQLiteVFS

// ---------------------------------------------------------------------------
// sqlite3_io_methods. C linkage: SQLite calls these through C function pointers.
// ---------------------------------------------------------------------------

extern "C" {

static int vfsClose(sqlite3_file* pFile) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  if (p->handle) {
    p->fs->close(p->handle);
    p->handle = nullptr;
  }
  return SQLITE_OK;
}

static int vfsRead(sqlite3_file* pFile, void* zBuf, int iAmt, sqlite3_int64 iOfst) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  int n = p->fs->read(p->handle, zBuf, iAmt, (int64_t)iOfst);
  if (n < 0) return SQLITE_IOERR_READ;
  if (n == iAmt) return SQLITE_OK;
  // Reads past end of file return zeros and SQLITE_IOERR_SHORT_READ.
  memset((char*)zBuf + n, 0, (size_t)(iAmt - n));
  return SQLITE_IOERR_SHORT_READ;
}

static int vfsWrite(sqlite3_file* pFile, const void* zBuf, int iAmt, sqlite3_int64 iOfst) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  if (p->readOnly) return SQLITE_IOERR_WRITE;
  int n = p->fs->write(p->handle, zBuf, iAmt, (int64_t)iOfst);
  if (n < 0) return SQLITE_IOERR_WRITE;
  if (n != iAmt) return SQLITE_FULL;
  return SQLITE_OK;
}

static int vfsTruncate(sqlite3_file* pFile, sqlite3_int64 size) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  if (p->readOnly) return SQLITE_IOERR_TRUNCATE;
  return p->fs->truncate(p->handle, (int64_t)size) ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int vfsSync(sqlite3_file* pFile, int flags) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  (void)flags;
  return p->fs->sync(p->handle) ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

static int vfsFileSize(sqlite3_file* pFile, sqlite3_int64* pSize) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  int64_t size = p->fs->size(p->handle);
  if (size < 0) return SQLITE_IOERR_FSTAT;
  *pSize = (sqlite3_int64)size;
  return SQLITE_OK;
}

// Locking is a no-op: a single connection owns the file (see header note).
static int vfsLock(sqlite3_file* pFile, int eLock) {
  (void)pFile;
  (void)eLock;
  return SQLITE_OK;
}

static int vfsUnlock(sqlite3_file* pFile, int eLock) {
  (void)pFile;
  (void)eLock;
  return SQLITE_OK;
}

static int vfsCheckReservedLock(sqlite3_file* pFile, int* pResOut) {
  (void)pFile;
  *pResOut = 0;
  return SQLITE_OK;
}

static int vfsFileControl(sqlite3_file* pFile, int op, void* pArg) {
  (void)pFile;
  (void)op;
  (void)pArg;
  return SQLITE_NOTFOUND;
}

static int vfsSectorSize(sqlite3_file* pFile) {
  (void)pFile;
  return 512;
}

static int vfsDeviceCharacteristics(sqlite3_file* pFile) {
  (void)pFile;
  return 0;
}

// Version 1 methods only: shared memory (xShm*) and memory mapping (xFetch)
// are left NULL, which is correct because WAL and mmap are disabled.
static const sqlite3_io_methods ioMethods = {
    .iVersion = 1,
    .xClose = vfsClose,
    .xRead = vfsRead,
    .xWrite = vfsWrite,
    .xTruncate = vfsTruncate,
    .xSync = vfsSync,
    .xFileSize = vfsFileSize,
    .xLock = vfsLock,
    .xUnlock = vfsUnlock,
    .xCheckReservedLock = vfsCheckReservedLock,
    .xFileControl = vfsFileControl,
    .xSectorSize = vfsSectorSize,
    .xDeviceCharacteristics = vfsDeviceCharacteristics,
    .xShmMap = nullptr,
    .xShmLock = nullptr,
    .xShmBarrier = nullptr,
    .xShmUnmap = nullptr,
    .xFetch = nullptr,
    .xUnfetch = nullptr,
};

// ---------------------------------------------------------------------------
// sqlite3_vfs
// ---------------------------------------------------------------------------

static int vfsOpen(sqlite3_vfs* pVfs, const char* zName, sqlite3_file* pFile,
                   int flags, int* pOutFlags) {
  BackendFile* p = reinterpret_cast<BackendFile*>(pFile);
  SQLiteFileSystem* fs = activeFileSystem;
  void* handle = nullptr;
  int readOnly = (flags & SQLITE_OPEN_READONLY) != 0;
  (void)pVfs;
  if (fs == nullptr) return SQLITE_CANTOPEN;  // no backend configured

  memset(p, 0, sizeof(*p));
  pFile->pMethods = &ioMethods;

  if (zName == nullptr) {
    handle = fs->openTemporary();  // anonymous spill file for large sorts
  } else {
    uint8_t mode = readOnly ? SQLiteFileSystem::ReadOnly : SQLiteFileSystem::ReadWrite;
    if (flags & SQLITE_OPEN_CREATE) mode |= SQLiteFileSystem::Create;
    handle = fs->open(zName, mode);
  }
  if (handle == nullptr) return SQLITE_CANTOPEN;

  p->fs = fs;
  p->handle = handle;
  p->readOnly = readOnly;
  if (pOutFlags) *pOutFlags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
  return SQLITE_OK;
}

static int vfsDelete(sqlite3_vfs* pVfs, const char* zName, int syncDir) {
  (void)pVfs;
  (void)syncDir;
  if (activeFileSystem == nullptr) return SQLITE_IOERR_DELETE_NOENT;
  if (!activeFileSystem->exists(zName, false)) return SQLITE_IOERR_DELETE_NOENT;
  return activeFileSystem->remove(zName) ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

static int vfsAccess(sqlite3_vfs* pVfs, const char* zName, int flags, int* pResOut) {
  (void)pVfs;
  *pResOut = (activeFileSystem != nullptr &&
              activeFileSystem->exists(zName, flags == SQLITE_ACCESS_READWRITE)) ? 1 : 0;
  return SQLITE_OK;
}

static int vfsFullPathname(sqlite3_vfs* pVfs, const char* zName, int nOut, char* zOut) {
  (void)pVfs;
  // No working directory concept on MCUs: names are used as given.
  if ((int)strlen(zName) + 1 > nOut) return SQLITE_CANTOPEN;
  memcpy(zOut, zName, strlen(zName) + 1);
  return SQLITE_OK;
}

static int vfsRandomness(sqlite3_vfs* pVfs, int nByte, char* zOut) {
  (void)pVfs;
  for (int i = 0; i < nByte; i++) zOut[i] = (char)(rand() & 0xFF);
  return nByte;
}

static int vfsSleep(sqlite3_vfs* pVfs, int microseconds) {
  (void)pVfs;
  // No portable sleep in the C library; the busy handler simply retries.
  return microseconds;
}

static int vfsCurrentTime(sqlite3_vfs* pVfs, double* pTime) {
  (void)pVfs;
  sqlite3_int64 ms = (sqlite3_int64)time(nullptr) * 1000;
  *pTime = 2440587.5 + ((double)ms / 86400000.0);
  return SQLITE_OK;
}

static int vfsCurrentTimeInt64(sqlite3_vfs* pVfs, sqlite3_int64* pTime) {
  (void)pVfs;
  *pTime = ((sqlite3_int64)time(nullptr) * 1000) + 210866760000000LL;
  return SQLITE_OK;
}

static int vfsGetLastError(sqlite3_vfs* pVfs, int nBuf, char* zBuf) {
  (void)pVfs;
  (void)nBuf;
  if (nBuf > 0) zBuf[0] = '\0';
  return 0;
}

static sqlite3_vfs arduinoVfs = {
    .iVersion = 1,
    .szOsFile = sizeof(BackendFile),
    .mxPathname = VFS_MAX_PATHNAME,
    .pNext = nullptr,
    .zName = "arduino",
    .pAppData = nullptr,
    .xOpen = vfsOpen,
    .xDelete = vfsDelete,
    .xAccess = vfsAccess,
    .xFullPathname = vfsFullPathname,
    .xDlOpen = nullptr,  // dynamic loading omitted
    .xDlError = nullptr,
    .xDlSym = nullptr,
    .xDlClose = nullptr,
    .xRandomness = vfsRandomness,
    .xSleep = vfsSleep,
    .xCurrentTime = vfsCurrentTime,
    .xGetLastError = vfsGetLastError,
    .xCurrentTimeInt64 = vfsCurrentTimeInt64,
    .xSetSystemCall = nullptr,
    .xGetSystemCall = nullptr,
    .xNextSystemCall = nullptr,
};

// Called by sqlite3_initialize() because SQLITE_OS_OTHER=1.
int sqlite3_os_init(void) {
  return sqlite3_vfs_register(&arduinoVfs, 1);
}

int sqlite3_os_end(void) {
  return SQLITE_OK;
}

}  // extern "C"
