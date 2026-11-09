/*
 * SQLite basic usage: an in-memory database and a file database.
 *
 * For the file database, mount a file system first and use a path under it.
 * Example for ESP32 with LittleFS:  LittleFS.begin(true); path "/littlefs/app.db"
 * On boards without a mounted file system, the file database is skipped.
 */
#include <SQLite.h>
#include <SQLiteMemory.h>  // PSRAM policy (no-op on boards without PSRAM)

#ifndef FILE_DB_PATH
#define FILE_DB_PATH "/littlefs/app.db"
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  // Use external RAM (PSRAM) for SQLite's dynamic memory when the board has it.
  // Must be called before the first database is opened.
  Serial.print("SQLite PSRAM: ");
  Serial.println(sqlite_memory_use_psram(1) ? "on" : "off");

  // In-memory database: always available.
  {
    SQLite::Database db(":memory:");
    db.exec("CREATE TABLE readings(id INTEGER PRIMARY KEY, value REAL);");
    db.exec("INSERT INTO readings(value) VALUES (21.5), (22.0), (23.25);");

    Serial.print("rows: ");
    Serial.println((long)db.execAndGetInt("SELECT COUNT(*) FROM readings;"));
    Serial.print("avg*100: ");
    Serial.println((long)db.execAndGetInt(
        "SELECT CAST(AVG(value)*100 AS INTEGER) FROM readings;"));
    Serial.print("json length: ");
    Serial.println((long)db.execAndGetInt("SELECT json_array_length('[1,2,3]');"));
  }

  // File database: requires a mounted file system at FILE_DB_PATH's prefix.
  {
    SQLite::Database db(FILE_DB_PATH, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    if (!db.isOpen()) {
      Serial.print("file database not available: ");
      Serial.println(db.getErrorMsg());
      return;
    }
    db.exec("CREATE TABLE IF NOT EXISTS log(t INTEGER, msg TEXT);");
    db.exec("INSERT INTO log VALUES (strftime('%s','now'), 'boot');");
    Serial.print("log rows in file: ");
    Serial.println((long)db.execAndGetInt("SELECT COUNT(*) FROM log;"));
  }
}

void loop() {
}
