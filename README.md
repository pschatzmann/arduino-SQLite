# SQLite for Arduino

## Introduction

This library brings the [SQLite](https://sqlite.org) database engine to Arduino boards with enough RAM and flash, such as ESP32, RP2040/RP2350 and STM32. The engine is compiled from the upstream [sqlite/sqlite](https://github.com/sqlite/sqlite) release, so databases are standard SQLite files that also open on a desktop.

The C++ API follows [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) (`SQLite::Database`, `SQLite::Statement`, `SQLite::Transaction`) without exceptions or `std::string`. The full C API is available through `sqlite3.h`.

Storage is pluggable:

- **stdio** (ESP32, desktop): paths are passed to `fopen()`, so a mounted LittleFS or SD file system works directly.
- **TinyFATFS** (`SQLiteTinyFATFS.h`): FAT volumes on SD cards or RAM disks.
- **In-memory** (`":memory:"`): works on every board, with no file system.

On boards with PSRAM, SQLite's dynamic memory can be placed in external RAM with `sqlite_memory_use_psram(1)`.

## Documentation

- [SQLite documentation](https://sqlite.org/docs.html) 
- [C/C++ interface](https://sqlite.org/c3ref/intro.html)
- [Prebuilt databases: build on the desktop, ship on flash](docs/PrebuiltDatabase.md)
- [File Systems](https://arduino-pico.readthedocs.io/en/latest/fs.html): how the Arduino cores mount LittleFS, SD and other file systems. Use this to pick the path for a database.
- [Examples](examples)

## Installation

Arduino IDE: **Sketch > Include Library > Manage Libraries**, search for *SQLite*, and install it.

Or clone the repository into your Arduino libraries folder:

```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-SQLite.git
```

The TinyFATFS backend needs the [TinyFATFS](https://github.com/pschatzmann/TinyFATFS) library. Install it only if you use `SQLiteTinyFATFS.h`.

To rebuild the SQLite sources from a new upstream release, see `tools/migrate_upstream.py`.
