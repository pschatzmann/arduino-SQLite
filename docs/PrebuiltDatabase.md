# Prebuilt databases: build on the desktop, ship on flash

A database can be built on your computer, uploaded with the sketch's data, and opened read-only on the board. This is useful for reference data such as sensor calibration tables, lookup tables, or configuration that does not change at runtime.

## 1. Write the schema and data

Put the `CREATE TABLE` and `INSERT` statements in a SQL file. The example is `examples/PrebuiltDatabase/schema.sql`:

```sql
CREATE TABLE sensors (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE,
  unit TEXT NOT NULL
);

INSERT INTO sensors (id, name, unit) VALUES
  (1, 'temperature', 'C'),
  (2, 'humidity', '%');
```

## 2. Build the database file on the desktop

```bash
python3 tools/make_prebuilt_db.py                    # uses the example schema
python3 tools/make_prebuilt_db.py my.sql out/app.db  # or your own schema and output
```

The script creates the file with the settings the board expects:

- `page_size = 4096`, the library's default page size;
- `journal_mode = DELETE`, so no `-wal` or `-shm` files are needed;
- `VACUUM` to remove free pages, then an integrity check.

The script uses only the Python standard library. Requirements: Python 3.

## 3. Upload the file to the board

Place the file in the sketch's `data/` folder (`examples/PrebuiltDatabase/data/app.db`) and upload it to the board's file system:

- **ESP32 (LittleFS):** Arduino IDE *Tools > ESP32 LittleFS Data Upload*. The file is written to `/littlefs/app.db`.

Mount LittleFS in the sketch before opening the database. The ESP32 core mounts it at `/littlefs` by default. See [File Systems](https://arduino-pico.readthedocs.io/en/latest/fs.html) for how the cores handle file systems.

## 4. Open it read-only on the board

```cpp
#include <LittleFS.h>
#include <SQLite.h>

void setup() {
  LittleFS.begin(false);
  SQLite::Database db("/littlefs/app.db", SQLite::OPEN_READONLY);
  if (!db.isOpen()) {
    Serial.println(db.getErrorMsg());
    return;
  }
  Serial.println(db.execAndGetInt("SELECT COUNT(*) FROM sensors;"));
}
```

Open it with `SQLite::OPEN_READONLY`. A read-only database needs no journal and cannot be damaged by a power loss in the middle of a write.

## Limits

- **ESP32** is the platform for which the flash path is implemented (`examples/PrebuiltDatabase` compiles for ESP32-S3; it has not yet been run on hardware). On other platforms, use a file-system backend (for example TinyFATFS) or a board-specific backend.
- Write-ahead logging (`journal_mode=WAL`) is not supported.
- Keep the page size at 4096 when building the file. Other sizes work with SQLite, but the examples and defaults assume 4096.
