#!/usr/bin/env python3
"""Build a SQLite database on the desktop for shipping on a board's flash.

Reads a schema/seed SQL file and writes a compact database file that the
sketch can open read-only from its file system (e.g. LittleFS).

Usage:
  python3 tools/make_prebuilt_db.py                       # example defaults
  python3 tools/make_prebuilt_db.py schema.sql out/app.db

Choices made for the board:
  - page_size 4096: the library's default; the file must not depend on another.
  - journal_mode DELETE, then VACUUM: no -wal/-shm files, minimal size.
Uses only the Python standard library (sqlite3 module).
"""

import argparse
import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent / "examples" / "PrebuiltDatabase"


def build(schema: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    for stale in (output, Path(str(output) + "-journal")):
        if stale.exists():
            stale.unlink()

    con = sqlite3.connect(output)
    try:
        con.execute("PRAGMA page_size = 4096;")  # must precede the first table
        con.execute("PRAGMA journal_mode = DELETE;")
        con.executescript(schema.read_text(encoding="utf-8"))
        con.commit()
        con.execute("VACUUM;")
        con.execute("PRAGMA optimize;")

        tables = [r[0] for r in con.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")]
        for table in tables:
            count = con.execute(f'SELECT COUNT(*) FROM "{table}"').fetchone()[0]
            print(f"  {table}: {count} rows")
        page_size = con.execute("PRAGMA page_size").fetchone()[0]
        integrity = con.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            sys.exit(f"integrity_check failed: {integrity}")
        print(f"wrote {output} ({output.stat().st_size} bytes, page_size {page_size}, integrity ok)")
    finally:
        con.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("schema", nargs="?", type=Path, default=HERE / "schema.sql")
    parser.add_argument("output", nargs="?", type=Path, default=HERE / "data" / "app.db")
    args = parser.parse_args()
    build(args.schema, args.output)


if __name__ == "__main__":
    main()
