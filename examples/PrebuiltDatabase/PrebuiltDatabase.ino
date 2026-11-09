/*
 * Open a database that was built on the desktop and uploaded to flash.
 *
 * 1. Build the database:       python3 tools/make_prebuilt_db.py
 *    (creates examples/PrebuiltDatabase/data/app.db)
 * 2. Upload the data folder to LittleFS with the core's uploader
 *    (Arduino IDE: Tools > ESP32 LittleFS Data Upload).
 * 3. Run this sketch: the database is opened read-only from /littlefs/app.db.
 *
 * ESP32 with LittleFS. See docs/PrebuiltDatabase.md.
 */
#include <LittleFS.h>
#include <SQLite.h>

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
  }

  if (!LittleFS.begin(false)) {  // false: do not format if mounting fails
    Serial.println("LittleFS mount failed: upload the data folder first");
    return;
  }

  SQLite::Database db("/littlefs/app.db", SQLite::OPEN_READONLY);
  if (!db.isOpen()) {
    Serial.print("open failed: ");
    Serial.println(db.getErrorMsg());
    return;
  }

  Serial.print("sensors: ");
  Serial.println((long)db.execAndGetInt("SELECT COUNT(*) FROM sensors;"));

  SQLite::Statement q(db,
      "SELECT s.name, s.unit, c.offset, c.gain "
      "FROM sensors s JOIN calibration c ON c.sensor_id = s.id ORDER BY s.id;");
  while (q.executeStep()) {
    Serial.print(q.getColumn(0).getText());
    Serial.print(" [");
    Serial.print(q.getColumn(1).getText());
    Serial.print("] offset=");
    Serial.print(q.getColumn(2).getDouble());
    Serial.print(" gain=");
    Serial.println(q.getColumn(3).getDouble());
  }
}

void loop() {
}
