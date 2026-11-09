/*
 * SQLite on a FAT volume through TinyFATFS.
 *
 * Requires the TinyFATFS library (https://github.com/pschatzmann/TinyFATFS).
 * Pick the driver for your board: RamIO works everywhere (no card needed);
 * replace it with the SD driver of your board to store the database on a card.
 */
#include <fatfs.h>            // TinyFATFS
#include <SQLite.h>
#include <SQLiteTinyFATFS.h>  // SQLite backend for TinyFATFS

static RamIO drv{400, 512};   // 200 KiB RAM disk; replace with your SD driver

SQLiteTinyFATFS fatBackend(nullptr);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  if (!SD.begin(drv)) {
    Serial.println("volume mount failed");
    return;
  }

  // Route all SQLite file access to the FAT volume. Set this before opening.
  fatBackend = SQLiteTinyFATFS(SD.getFatFs());
  SQLiteVFS::setFileSystem(&fatBackend);

  {
    SQLite::Database db("/app.db");
    if (!db.isOpen()) {
      Serial.print("open failed: ");
      Serial.println(db.getErrorMsg());
      return;
    }
    db.exec("CREATE TABLE IF NOT EXISTS log(id INTEGER PRIMARY KEY, msg TEXT);");
    db.exec("INSERT INTO log(msg) VALUES ('boot');");
    Serial.print("rows: ");
    Serial.println((long)db.execAndGetInt("SELECT COUNT(*) FROM log;"));
  }

  // Back to the default backend (stdio where available).
  SQLiteVFS::setFileSystem(nullptr);
}

void loop() {
}
