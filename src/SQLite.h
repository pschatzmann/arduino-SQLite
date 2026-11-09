/*
 * SQLite C++ wrapper for Arduino.
 *
 * The API follows SQLiteCpp (https://github.com/SRombauts/SQLiteCpp) so code
 * written for the desktop library maps directly onto this one. Differences:
 *   - no exceptions: failures are reported with hasError() / getErrorCode()
 *     and getErrorMsg(), and methods return false / -1 / the default value;
 *   - no std::string: text is returned as const char*;
 *   - no allocation beyond SQLite's own (no STL containers).
 * The raw C API remains available through handle() and sqlite3.h.
 *
 * Example:
 *   SQLite::Database db("/littlefs/app.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
 *   db.exec("CREATE TABLE IF NOT EXISTS t(id INTEGER PRIMARY KEY, v REAL);");
 *   {
 *     SQLite::Transaction tx(db);
 *     SQLite::Statement ins(db, "INSERT INTO t(v) VALUES(?)");
 *     ins.bind(1, 21.5);
 *     ins.exec();
 *     tx.commit();
 *   }
 *   SQLite::Statement q(db, "SELECT id, v FROM t");
 *   while (q.executeStep()) {
 *     Serial.println(q.getColumn(1).getDouble());
 *   }
 */
#ifndef ARDUINO_SQLITE_H
#define ARDUINO_SQLITE_H

#include "sqlite3.h"

namespace SQLite {

class Database;
class Statement;

/// Open flags (same values as the C API).
constexpr int OPEN_READONLY = SQLITE_OPEN_READONLY;
constexpr int OPEN_READWRITE = SQLITE_OPEN_READWRITE;
constexpr int OPEN_CREATE = SQLITE_OPEN_CREATE;

/// Column type codes (same values as the C API).
constexpr int INTEGER = SQLITE_INTEGER;
constexpr int FLOAT = SQLITE_FLOAT;
constexpr int TEXT = SQLITE_TEXT;
constexpr int BLOB = SQLITE_BLOB;
constexpr int SQL_NULL = SQLITE_NULL;

/// One value of the current row of a Statement.
class Column {
 public:
  Column(sqlite3_stmt *stmt, int index) : stmt_(stmt), index_(index) {}

  const char *getName() const { return sqlite3_column_name(stmt_, index_); }
  int getType() const { return sqlite3_column_type(stmt_, index_); }
  bool isNull() const { return getType() == SQLITE_NULL; }

  int getInt() const { return sqlite3_column_int(stmt_, index_); }
  long long getInt64() const { return sqlite3_column_int64(stmt_, index_); }
  double getDouble() const { return sqlite3_column_double(stmt_, index_); }

  /// Text value; an empty string for NULL.
  const char *getText(const char *def = "") const {
    const unsigned char *t = sqlite3_column_text(stmt_, index_);
    return t ? reinterpret_cast<const char *>(t) : def;
  }

  /// Blob value (or text bytes); nullptr for NULL.
  const void *getBlob() const { return sqlite3_column_blob(stmt_, index_); }
  int getBytes() const { return sqlite3_column_bytes(stmt_, index_); }

 private:
  sqlite3_stmt *stmt_;
  int index_;
};

/// A prepared SQL statement bound to a Database.
class Statement {
 public:
  /// Prepares sql. Check hasError() afterwards.
  Statement(Database &db, const char *sql);
  ~Statement();

  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  // --- binding (1-based index, as in SQLiteCpp) ---
  int bind(int index, int value);
  int bind(int index, long long value);
  int bind(int index, double value);
  int bind(int index, const char *value);                    // copied
  int bind(int index, const void *blob, int size);           // copied
  int bindNull(int index);
  int getBindParameterCount() const;
  /// Index of a named parameter (":name", "@name" or "$name"), or 0.
  int getIndex(const char *name) const;

  // --- execution ---
  /// Steps to the next row. True while rows are available; false when done or on error.
  bool executeStep();
  /// Runs a non-query statement to completion. Returns changed rows, or -1 on error.
  int exec();
  /// Rewinds for another execution (bindings are kept).
  bool reset();
  bool clearBindings();

  // --- results ---
  Column getColumn(int index) const { return Column(stmt_, index); }
  int getColumnCount() const;
  const char *getColumnName(int index) const;
  bool isColumnNull(int index) const;
  const char *getSQL() const;

  // --- errors ---
  bool isValid() const { return stmt_ != nullptr; }
  bool hasError() const { return error_ != SQLITE_OK && error_ != SQLITE_DONE && error_ != SQLITE_ROW; }
  int getErrorCode() const { return error_; }
  const char *getErrorMsg() const;

  sqlite3_stmt *handle() const { return stmt_; }

 private:
  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
  int error_ = SQLITE_OK;
};

/// A database connection.
class Database {
 public:
  /// Opens filename: ":memory:" or a path such as "/littlefs/app.db".
  Database(const char *filename, int flags = OPEN_READWRITE | OPEN_CREATE);
  ~Database();

  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  bool isOpen() const { return opened_; }
  bool hasError() const { return error_ != SQLITE_OK; }
  int getErrorCode() const { return error_; }
  int getExtendedErrorCode() const;
  const char *getErrorMsg() const;

  /// Runs SQL that returns no rows (several statements allowed).
  /// Returns SQLITE_OK on success, else the error code.
  int exec(const char *sql);

  /// Runs a query and returns the first integer of the first row (def if none).
  long long execAndGetInt(const char *sql, long long def = 0);

  bool tableExists(const char *table) const;
  long long getLastInsertRowid() const;
  int getChanges() const;
  int getTotalChanges() const;
  const char *getFilename() const;
  void setBusyTimeout(int ms);

  /// Closes the connection early (also done by the destructor).
  void close();

  static const char *libVersion();
  static int libVersionNumber();

  sqlite3 *handle() const { return db_; }

 private:
  friend class Statement;
  friend class Transaction;

  sqlite3 *db_ = nullptr;
  bool opened_ = false;
  int error_ = SQLITE_OK;  // result of the last operation
};

/// Transaction with rollback on destruction unless committed (RAII).
class Transaction {
 public:
  explicit Transaction(Database &db);
  ~Transaction();

  Transaction(const Transaction &) = delete;
  Transaction &operator=(const Transaction &) = delete;

  /// True if the transaction was committed.
  bool commit();
  /// Rolls back explicitly; returns true on success.
  bool rollback();

 private:
  Database &db_;
  bool active_ = false;
};

}  // namespace SQLite

#endif  // ARDUINO_SQLITE_H
