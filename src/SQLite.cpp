#include "SQLite.h"

namespace SQLite {

// ---------------------------------------------------------------- Database

Database::Database(const char *filename, int flags) {
  error_ = sqlite3_open_v2(filename, &db_, flags, nullptr);
  opened_ = (error_ == SQLITE_OK);
  // On failure db_ is kept so getErrorMsg() can explain why; close() frees it.
}

Database::~Database() { close(); }

void Database::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  opened_ = false;
}

int Database::exec(const char *sql) {
  if (!db_) return error_ = SQLITE_MISUSE;
  error_ = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
  return error_;
}

long long Database::execAndGetInt(const char *sql, long long def) {
  if (!db_) {
    error_ = SQLITE_MISUSE;
    return def;
  }
  sqlite3_stmt *st = nullptr;
  long long value = def;
  error_ = sqlite3_prepare_v2(db_, sql, -1, &st, nullptr);
  if (error_ == SQLITE_OK) {
    error_ = sqlite3_step(st);
    if (error_ == SQLITE_ROW) {
      value = sqlite3_column_int64(st, 0);
      error_ = SQLITE_OK;
    } else if (error_ == SQLITE_DONE) {
      error_ = SQLITE_OK;  // no rows: default value, not an error
    }
  }
  sqlite3_finalize(st);
  return value;
}

bool Database::tableExists(const char *table) const {
  if (!db_) return false;
  sqlite3_stmt *st = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db_,
                         "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?",
                         -1, &st, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(st, 1, table, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) found = sqlite3_column_int(st, 0) > 0;
  }
  sqlite3_finalize(st);
  return found;
}

long long Database::getLastInsertRowid() const {
  return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

int Database::getChanges() const { return db_ ? sqlite3_changes(db_) : 0; }

int Database::getTotalChanges() const { return db_ ? sqlite3_total_changes(db_) : 0; }

const char *Database::getFilename() const {
  return db_ ? sqlite3_db_filename(db_, "main") : "";
}

void Database::setBusyTimeout(int ms) {
  if (db_) sqlite3_busy_timeout(db_, ms);
}

int Database::getExtendedErrorCode() const {
  return db_ ? sqlite3_extended_errcode(db_) : error_;
}

const char *Database::getErrorMsg() const {
  return db_ ? sqlite3_errmsg(db_) : "database not open";
}

const char *Database::libVersion() { return sqlite3_libversion(); }

int Database::libVersionNumber() { return sqlite3_libversion_number(); }

// --------------------------------------------------------------- Statement

Statement::Statement(Database &db, const char *sql) : db_(db.handle()) {
  if (!db_) {
    error_ = SQLITE_MISUSE;
    return;
  }
  error_ = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

int Statement::bind(int index, int value) {
  return error_ = sqlite3_bind_int(stmt_, index, value);
}

int Statement::bind(int index, long long value) {
  return error_ = sqlite3_bind_int64(stmt_, index, value);
}

int Statement::bind(int index, double value) {
  return error_ = sqlite3_bind_double(stmt_, index, value);
}

int Statement::bind(int index, const char *value) {
  return error_ = sqlite3_bind_text(stmt_, index, value, -1, SQLITE_TRANSIENT);
}

int Statement::bind(int index, const void *blob, int size) {
  return error_ = sqlite3_bind_blob(stmt_, index, blob, size, SQLITE_TRANSIENT);
}

int Statement::bindNull(int index) {
  return error_ = sqlite3_bind_null(stmt_, index);
}

int Statement::getBindParameterCount() const {
  return stmt_ ? sqlite3_bind_parameter_count(stmt_) : 0;
}

int Statement::getIndex(const char *name) const {
  return stmt_ ? sqlite3_bind_parameter_index(stmt_, name) : 0;
}

bool Statement::executeStep() {
  if (!stmt_) return false;
  error_ = sqlite3_step(stmt_);
  return error_ == SQLITE_ROW;
}

int Statement::exec() {
  if (!stmt_) return -1;
  error_ = sqlite3_step(stmt_);
  if (error_ == SQLITE_DONE) {
    int changes = sqlite3_changes(db_);
    sqlite3_reset(stmt_);
    error_ = SQLITE_OK;
    return changes;
  }
  if (error_ == SQLITE_ROW) {  // statement returns rows: not an exec() statement
    sqlite3_reset(stmt_);
    error_ = SQLITE_MISUSE;
  }
  return -1;
}

bool Statement::reset() {
  if (!stmt_) return false;
  error_ = sqlite3_reset(stmt_);
  return error_ == SQLITE_OK;
}

bool Statement::clearBindings() {
  if (!stmt_) return false;
  return sqlite3_clear_bindings(stmt_) == SQLITE_OK;
}

int Statement::getColumnCount() const {
  return stmt_ ? sqlite3_column_count(stmt_) : 0;
}

const char *Statement::getColumnName(int index) const {
  return stmt_ ? sqlite3_column_name(stmt_, index) : "";
}

bool Statement::isColumnNull(int index) const {
  return stmt_ ? sqlite3_column_type(stmt_, index) == SQLITE_NULL : true;
}

const char *Statement::getSQL() const {
  return stmt_ ? sqlite3_sql(stmt_) : "";
}

const char *Statement::getErrorMsg() const {
  return db_ ? sqlite3_errmsg(db_) : "database not open";
}

// ------------------------------------------------------------- Transaction

Transaction::Transaction(Database &db) : db_(db) {
  active_ = (db_.exec("BEGIN") == SQLITE_OK);
}

Transaction::~Transaction() {
  if (active_) rollback();
}

bool Transaction::commit() {
  if (!active_) return false;
  if (db_.exec("COMMIT") != SQLITE_OK) return false;  // still active: may retry or roll back
  active_ = false;
  return true;
}

bool Transaction::rollback() {
  if (!active_) return false;
  bool ok = db_.exec("ROLLBACK") == SQLITE_OK;
  active_ = false;
  return ok;
}

}  // namespace SQLite
