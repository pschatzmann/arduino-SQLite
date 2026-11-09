-- Schema and seed data for the prebuilt database (see docs/PrebuiltDatabase.md).
-- Build it on the desktop with: python3 tools/make_prebuilt_db.py

CREATE TABLE sensors (
  id    INTEGER PRIMARY KEY,
  name  TEXT NOT NULL UNIQUE,
  unit  TEXT NOT NULL
);

CREATE TABLE calibration (
  sensor_id INTEGER NOT NULL REFERENCES sensors(id),
  offset    REAL NOT NULL DEFAULT 0,
  gain      REAL NOT NULL DEFAULT 1,
  PRIMARY KEY (sensor_id)
);

INSERT INTO sensors (id, name, unit) VALUES
  (1, 'temperature', 'C'),
  (2, 'humidity', '%'),
  (3, 'pressure', 'hPa');

INSERT INTO calibration (sensor_id, offset, gain) VALUES
  (1, -0.5, 1.0),
  (2,  0.0, 1.02),
  (3,  1.3, 1.0);
