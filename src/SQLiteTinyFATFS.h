/*
 * SQLite backend for TinyFATFS (https://github.com/pschatzmann/TinyFATFS).
 *
 * Header-only on purpose: Arduino compiles only the .c/.cpp files of a
 * library, so this file is compiled only by sketches that include it. Boards
 * without TinyFATFS installed are not affected.
 *
 * Usage (after mounting the volume with TinyFATFS):
 *   #include <fatfs.h>            // TinyFATFS
 *   #include <SQLiteTinyFATFS.h>
 *
 *   RamIO drv{200, 512};          // or SdSpiIO, SdmmcIO, ... from TinyFATFS
 *   SQLiteTinyFATFS fatBackend(SD.getFatFs());
 *
 *   SD.begin(drv);
 *   SQLiteVFS::setFileSystem(&fatBackend);
 *   SQLiteDB db;
 *   db.open("/app.db");           // file lives on the FAT volume
 *
 * The backend must outlive every database opened through it.
 */
#ifndef SQLITE_TINYFATFS_H
#define SQLITE_TINYFATFS_H

#include "SQLiteFS.h"
#include "fatfs.h"  // TinyFATFS
#include "sqlite3.h"  // sqlite3_malloc: file handles follow the PSRAM policy

class SQLiteTinyFATFS : public SQLiteFileSystem {
 public:
  /// fs is the volume to use, e.g. SD.getFatFs() after SD.begin(...).
  explicit SQLiteTinyFATFS(fatfs::FatFs* fs) : fs_(fs) {}

  void* open(const char* path, uint8_t mode) override {
    if (fs_ == nullptr) return nullptr;
    BYTE flags = FA_READ;
    if (!(mode & ReadOnly)) flags |= FA_WRITE;
    if (mode & Create) flags |= FA_OPEN_ALWAYS;

    FIL* file = static_cast<FIL*>(sqlite3_malloc(sizeof(FIL)));
    if (file == nullptr) return nullptr;
    if (fs_->f_open(file, path, flags) != FR_OK) {
      sqlite3_free(file);
      return nullptr;
    }
    return file;
  }

  int read(void* handle, void* buf, int n, int64_t offset) override {
    FIL* file = static_cast<FIL*>(handle);
    // f_lseek() past the end of a file opened for writing EXTENDS the file
    // with zeros. Clamp reads to the current size so reading never grows it.
    FSIZE_t fsize = fs_->f_size(file);
    if ((FSIZE_t)offset >= fsize) return 0;
    if ((FSIZE_t)offset + (FSIZE_t)n > fsize) n = (int)(fsize - (FSIZE_t)offset);
    if (fs_->f_lseek(file, (FSIZE_t)offset) != FR_OK) return -1;
    UINT br = 0;
    if (fs_->f_read(file, buf, (UINT)n, &br) != FR_OK) return -1;
    return (int)br;
  }

  int write(void* handle, const void* buf, int n, int64_t offset) override {
    FIL* file = static_cast<FIL*>(handle);
    if (fs_->f_lseek(file, (FSIZE_t)offset) != FR_OK) return -1;
    UINT bw = 0;
    if (fs_->f_write(file, buf, (UINT)n, &bw) != FR_OK) return -1;
    return (int)bw;
  }

  bool truncate(void* handle, int64_t size) override {
    FIL* file = static_cast<FIL*>(handle);
    // FatFs truncates at the current file pointer.
    if (fs_->f_lseek(file, (FSIZE_t)size) != FR_OK) return false;
    return fs_->f_truncate(file) == FR_OK;
  }

  bool sync(void* handle) override {
    return fs_->f_sync(static_cast<FIL*>(handle)) == FR_OK;
  }

  int64_t size(void* handle) override {
    return (int64_t)fs_->f_size(static_cast<FIL*>(handle));
  }

  void close(void* handle) override {
    fs_->f_close(static_cast<FIL*>(handle));
    sqlite3_free(handle);
  }

  bool remove(const char* path) override {
    return fs_->f_unlink(path) == FR_OK;
  }

  bool exists(const char* path, bool writable) override {
    if (fs_ == nullptr) return false;
    FILINFO info;
    if (fs_->f_stat(path, &info) != FR_OK) return false;
    if (info.fattrib & AM_DIR) return false;  // directories are not databases
    if (writable && (info.fattrib & AM_RDO)) return false;
    return true;
  }

 private:
  fatfs::FatFs* fs_;
};

#endif  // SQLITE_TINYFATFS_H
